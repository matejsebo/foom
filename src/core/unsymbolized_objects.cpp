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

#include "unsymbolized_objects.h"

#include <cstddef>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"


namespace OpenOrienteering {

bool isUnsymbolized(const Symbol* symbol)
{
	return !symbol
	       || symbol == Map::getUndefinedLine()
	       || symbol == Map::getUndefinedPoint()
	       || symbol == Map::getUndefinedText();
}


std::vector<const Object*> findUnsymbolizedObjects(const Map& map)
{
	std::vector<const Object*> result;
	for (std::size_t p = 0; p < std::size_t(map.getNumParts()); ++p)
	{
		auto const* part = map.getPart(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (isUnsymbolized(object->getSymbol()))
				result.push_back(object);
		}
	}
	return result;
}


}  // namespace OpenOrienteering
