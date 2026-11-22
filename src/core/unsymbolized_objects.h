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

#ifndef OPENORIENTEERING_UNSYMBOLIZED_OBJECTS_H
#define OPENORIENTEERING_UNSYMBOLIZED_OBJECTS_H

#include <vector>

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;


/** Returns true for the map's placeholders of unknown symbols. */
bool isUnsymbolized(const Symbol* symbol);

/**
 * The objects of every part whose symbol is a placeholder for an unknown
 * symbol, part by part in the parts' order.
 * 
 * See the design notes, "Show objects without symbol".
 */
std::vector<const Object*> findUnsymbolizedObjects(const Map& map);


}  // namespace OpenOrienteering

#endif
