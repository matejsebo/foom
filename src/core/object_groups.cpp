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


#include "object_groups.h"

#include <algorithm>
#include <set>
#include <vector>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"


namespace OpenOrienteering {

int nextObjectGroupId(const Map& map)
{
	auto next = 1;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
			next = std::max(next, part->getObject(i)->group() + 1);
	}
	return next;
}

bool expandSelectionToGroups(Map& map)
{
	std::set<int> groups;
	for (auto const* object : map.selectedObjects())
	{
		if (object->group() != 0)
			groups.insert(object->group());
	}
	if (groups.empty())
		return false;
	
	std::vector<Object*> additions;
	auto* part = map.getCurrentPart();
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto* object = part->getObject(i);
		if (groups.count(object->group()) && !map.isObjectSelected(object))
			additions.push_back(object);
	}
	for (std::size_t i = 0; i < additions.size(); ++i)
		map.addObjectToSelection(additions[i], i + 1 == additions.size());
	return !additions.empty();
}


}  // namespace OpenOrienteering
