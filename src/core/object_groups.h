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


#ifndef OPENORIENTEERING_OBJECT_GROUPS_H
#define OPENORIENTEERING_OBJECT_GROUPS_H

namespace OpenOrienteering {

class Map;


/**
 * Returns the smallest group number above every group in the map's parts.
 *
 * See the design notes, "Groups".
 */
int nextObjectGroupId(const Map& map);

/**
 * Adds every object of the current part which shares a group with a
 * selected object to the selection. Returns true when the selection grew;
 * the change is signalled then.
 */
bool expandSelectionToGroups(Map& map);


}  // namespace OpenOrienteering

#endif
