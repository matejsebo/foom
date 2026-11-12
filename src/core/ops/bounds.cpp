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


#include "bounds.h"

#include <algorithm>
#include <vector>

#include "core/map_coord.h"
#include "core/objects/object.h"


namespace OpenOrienteering {

namespace ops {

MapCoordF boundingCentre(const std::vector<const Object*>& in)
{
	auto left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
	auto first = true;
	auto include = [&](const MapCoord& coord) {
		if (first)
		{
			left = right = coord.x();
			top = bottom = coord.y();
			first = false;
		}
		else
		{
			left = std::min(left, coord.x());
			right = std::max(right, coord.x());
			top = std::min(top, coord.y());
			bottom = std::max(bottom, coord.y());
		}
	};
	for (auto const* object : in)
	{
		if (!object)
			continue;
		auto const& coords = object->getRawCoordinateVector();
		if (object->getType() == Object::Text)
		{
			if (!coords.empty())
				include(coords.front());
		}
		else
		{
			for (auto const& coord : coords)
				include(coord);
		}
	}
	return MapCoordF((left + right) / 2, (top + bottom) / 2);
}


}  // namespace ops

}  // namespace OpenOrienteering
