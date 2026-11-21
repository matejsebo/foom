/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM.
 *
 *    FOOM is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    FOOM is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "line_text.h"

#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/** Returns the unit vector of the given vector, or (0, 0). */
MapCoordF unit(MapCoordF v)
{
	auto const length = v.length();
	if (length > 0)
		v /= length;
	else
		v = MapCoordF(0.0, 0.0);
	return v;
}

/** Moves the position of the coordinate by the vector, keeping its flags. */
void moveBy(MapCoord& coord, const MapCoordF& v)
{
	auto const p = MapCoordF(coord) + v;
	coord.setX(p.x());
	coord.setY(p.y());
}

}  // namespace


OpResult lengthenLineText(const Map& /*map*/, const std::vector<const Object*>& in, const LengthenLineTextParams& params)
{
	// the design notes, "Lengthen line text".
	OpResult result;
	if (params.by_percent ? !(params.percent > 0.0) : !(params.distance > 0.0))
	{
		result.error = QStringLiteral("The extension must be greater than zero.");
		return result;
	}

	auto found = false;
	auto closed = 0;
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Text || !object->asText()->followsPath())
			continue;
		auto const* text = object->asText();
		auto coords = text->getRawCoordinateVector();
		if (coords.size() < 2)
			continue;
		found = true;

		PathObject anchor_path(nullptr, coords);
		anchor_path.updatePathCoords();
		if (anchor_path.parts().empty())
			continue;
		auto const& part = anchor_path.parts().front();
		if (part.isClosed())
		{
			++closed;
			continue;
		}
		auto const length = double(part.length());
		if (!(length > 0.0))
			continue;

		auto const extension = params.by_percent ? length * params.percent / 100.0 : params.distance;
		auto const alignment = static_cast<const TextSymbol*>(text->getSymbol())->getPathAlignment() & 0x03;
		auto const at_start = alignment == 0 ? 0.0 : (alignment == 2 ? extension : extension / 2);
		auto const at_end = extension - at_start;

		auto const first = part.first_index;
		auto const last = part.last_index;
		// The index of the coordinate where the last segment starts.
		auto last_segment = first;
		for (auto i = first; i < last; i += coords[i].isCurveStart() ? 3 : 1)
			last_segment = i;

		// The end first, so that the indices of the start stay valid.
		if (at_end > 0.0)
		{
			// Along the straight segment, or along the curve's last handle.
			auto const is_curve = coords[last_segment].isCurveStart();
			auto direction = unit(MapCoordF(coords[last]) - MapCoordF(coords[last - 1]));
			if (is_curve && direction.length() == 0)
				direction = unit(MapCoordF(coords[last]) - MapCoordF(coords[last - 2]));
			if (!is_curve)
			{
				moveBy(coords[last], direction * at_end);
			}
			else
			{
				auto added = coords[last];
				added.setFlags(0);
				added.setHolePoint(coords[last].isHolePoint());
				coords[last].setHolePoint(false);
				moveBy(added, direction * at_end);
				coords.insert(coords.begin() + std::ptrdiff_t(last) + 1, added);
			}
		}
		if (at_start > 0.0)
		{
			auto const is_curve = coords[first].isCurveStart();
			auto direction = unit(MapCoordF(coords[first + 1]) - MapCoordF(coords[first]));
			if (is_curve && direction.length() == 0)
				direction = unit(MapCoordF(coords[first + 2]) - MapCoordF(coords[first]));
			if (!is_curve)
			{
				moveBy(coords[first], direction * -at_start);
			}
			else
			{
				auto added = coords[first];
				added.setFlags(0);
				moveBy(added, direction * -at_start);
				coords.insert(coords.begin() + std::ptrdiff_t(first), added);
			}
		}

		auto output = std::unique_ptr<TextObject>(text->duplicate());
		output->setAnchorPath(coords);
		output->setIdentity(text->uuid(), text->createdTime(), text->modifiedTime());
		output->setDerivedFrom(text->derivedFrom());
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}

	if (!found)
		result.error = QStringLiteral("Select a line text object.");
	else if (closed > 0)
		result.warning = QStringLiteral("%1 line text objects with a closed path were left alone.").arg(closed);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
