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


#include "vertex_type.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

OpResult setVertexType(const Map& /*map*/, const std::vector<const Object*>& in, const VertexTypeParams& params)
{
	OpResult result;
	auto found_path = false;
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path)
			continue;
		found_path = true;
		auto const* path = object->asPath();
		auto coords = path->getRawCoordinateVector();
		auto changed = false;
		for (auto const& part : path->parts())
		{
			for (auto i = part.first_index; i <= part.last_index; ++i)
			{
				auto const is_end = i == part.first_index || i == part.last_index;
				if (!(params.keep_ends && is_end))
				{
					auto const before = coords[i].flags();
					coords[i].setDashPoint(params.type == VertexType::Dash || params.type == VertexType::Corner);
					coords[i].setCornerPoint(params.type == VertexType::Corner);
					coords[i].setVirtualGap(params.type == VertexType::VirtualGap);
					changed |= coords[i].flags() != before;
				}
				if (coords[i].isCurveStart())
					i += 2;  // skip the control points
			}
		}
		if (!changed)
			continue;
		auto output = std::unique_ptr<PathObject>(new PathObject(path->getSymbol(), std::move(coords)));
		output->setTags(path->tags());
		output->setPatternRotation(path->getPatternRotation());
		output->setPatternOrigin(path->getPatternOrigin());
		output->setIdentity(path->uuid(), path->createdTime(), path->modifiedTime());
		output->setDerivedFrom(path->derivedFrom());
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	if (!found_path)
		result.error = QStringLiteral("Select a line or area object.");
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
