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

#ifndef OPENORIENTEERING_IMPASSABLE_FEATURES_H
#define OPENORIENTEERING_IMPASSABLE_FEATURES_H

#include <vector>

namespace OpenOrienteering {

class Map;
class Object;
struct RouteSettings;


/**
 * The objects of the map's parts with the map role whose symbol is not
 * hidden and is impassable for the route analysis, by its defaults and
 * the given settings; part by part in the parts' order.
 * 
 * See the design notes, "Show impassable features".
 */
std::vector<const Object*> findImpassableObjects(const Map& map, const RouteSettings& settings);


}  // namespace OpenOrienteering

#endif
