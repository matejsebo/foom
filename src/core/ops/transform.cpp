/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "transform.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/bounds.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * Builds the result from a copy of every input, edited by the given function.
 */
template< class F >
OpResult transformEach(const std::vector<const Object*>& in, bool duplicate, F edit)
{
	OpResult result;
	if (in.empty())
	{
		result.error = QStringLiteral("No objects selected.");
		return result;
	}
	for (auto const* object : in)
	{
		if (!object)
			continue;
		auto output = std::unique_ptr<Object>(object->duplicate());
		edit(*output, *object);
		result.out.push_back(std::move(output));
		if (!duplicate)
			result.consumed.push_back(object);
	}
	return result;
}

}  // namespace


OpResult moveBy(const Map& /*map*/, const std::vector<const Object*>& in, const MoveParams& params)
{
	auto const offset = MapCoord(params.dx, params.dy);
	return transformEach(in, params.duplicate, [&offset](Object& output, const Object& /*input*/) {
		output.move(offset);
	});
}

OpResult rotateBy(const Map& /*map*/, const std::vector<const Object*>& in, const RotateParams& params)
{
	auto const common_centre = boundingCentre(in);
	auto const angle = qDegreesToRadians(params.angle);
	return transformEach(in, params.duplicate, [&params, &common_centre, angle](Object& output, const Object& input) {
		auto const centre = params.each_object ? boundingCentre({ &input }) : common_centre;
		output.rotateAround(centre, angle);
	});
}

OpResult stretch(const Map& /*map*/, const std::vector<const Object*>& in, const StretchParams& params)
{
	if (qIsNull(params.factor_x) || qIsNull(params.factor_y))
	{
		OpResult result;
		result.error = QStringLiteral("The factors must not be zero.");
		return result;
	}
	auto const centre = boundingCentre(in);
	auto const stretchCoord = [&centre, &params](MapCoord coord) {
		coord.setX(centre.x() + (coord.x() - centre.x()) * params.factor_x);
		coord.setY(centre.y() + (coord.y() - centre.y()) * params.factor_y);
		return coord;
	};
	return transformEach(in, params.duplicate, [&stretchCoord, &params](Object& output, const Object& /*input*/) {
		switch (output.getType())
		{
		case Object::Path:
			{
				auto* path = output.asPath();
				for (MapCoordVector::size_type i = 0; i < path->getCoordinateCount(); ++i)
					path->setCoordinate(i, stretchCoord(path->getCoordinate(i)));
				path->setPatternOrigin(stretchCoord(path->getPatternOrigin()));
			}
			break;
		case Object::Point:
			{
				auto* point = output.asPoint();
				point->setPosition(stretchCoord(point->getCoord()));
			}
			break;
		case Object::Text:
			{
				auto* text = output.asText();
				auto const anchor = stretchCoord(MapCoord(text->getAnchorCoordF()));
				if (text->hasSingleAnchor())
				{
					text->setAnchorPosition(anchor);
				}
				else
				{
					auto const size = text->getBoxSize();
					text->setBox(anchor.nativeX(), anchor.nativeY(), size.x() * std::abs(params.factor_x), size.y() * std::abs(params.factor_y));
				}
			}
			break;
		default:
			break;
		}
	});
}


}  // namespace ops

}  // namespace OpenOrienteering
