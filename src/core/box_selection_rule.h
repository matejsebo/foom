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

#ifndef OPENORIENTEERING_BOX_SELECTION_RULE_H
#define OPENORIENTEERING_BOX_SELECTION_RULE_H

#include <vector>

#include "core/map_coord.h"

class QRectF;

namespace OpenOrienteering {

class Object;


/**
 * Returns true when the object's geometry lies entirely in the box, edges
 * included: a point object by its position, a path object by every
 * coordinate of its flattened outline, a text object by the corners of its
 * extent. The symbol's width does not count.
 * 
 * See the design notes, "Rectangle selection rule".
 */
bool objectEntirelyInBox(const Object& object, const QRectF& box);

/**
 * Returns true when the object's geometry lies entirely in the polygon, by
 * the even-odd rule; the geometry is that of objectEntirelyInBox().
 */
bool objectEntirelyInPolygon(const Object& object, const std::vector<MapCoordF>& polygon);

/**
 * Returns true when the setting asks for objects entirely inside the
 * rectangle (or the lasso's outline), not for those partly inside.
 */
bool rectangleSelectsEntirelyInside();


}  // namespace OpenOrienteering

#endif
