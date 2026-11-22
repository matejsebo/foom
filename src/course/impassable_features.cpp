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

#include "impassable_features.h"

#include <cstddef>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "course/route_settings.h"
#include "course/route_terrain.h"


namespace OpenOrienteering {

std::vector<const Object*> findImpassableObjects(const Map& map, const RouteSettings& settings)
{
	std::vector<const Object*> result;
	for (std::size_t p = 0; p < std::size_t(map.getNumParts()); ++p)
	{
		auto const* part = map.getPart(p);
		if (part->role() != MapPart::Role::Map)
			continue;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			auto const* symbol = object->getSymbol();
			if (!symbol || symbol->isHidden())
				continue;
			if (runnabilityOf(settings, *symbol).kind == Runnability::Impassable)
				result.push_back(object);
		}
	}
	return result;
}


}  // namespace OpenOrienteering
