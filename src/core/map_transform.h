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

#ifndef OPENORIENTEERING_MAP_TRANSFORM_H
#define OPENORIENTEERING_MAP_TRANSFORM_H

#include <QTransform>

namespace OpenOrienteering {

class Map;


/**
 * Options of the map transformations.
 */
struct MapTransformOptions
{
	/// Also transform the layout parts' objects, the named extents, the bookmarks
	/// and the templates which are not georeferenced.
	bool carry = true;
};


/**
 * Applies an affine transformation of map coordinates (mm, y growing south) to the map,
 * as one undo step. The georeferencing does not change.
 *
 * Objects are transformed by Object::transform(): coordinates only, rotations and the
 * order of coordinates kept, texts by their anchors. The course data's stored routes
 * always follow. Returns false for a transformation which cannot be inverted.
 *
 * See the design notes, "Move, stretch and mirror".
 */
bool transformMap(Map& map, const QTransform& transform, const MapTransformOptions& options);

/// The translation which moves the map by the given metres east and north, through its georeferencing.
QTransform mapMoveTransform(const Map& map, double east_m, double north_m);

/// Scaling by the factors, with the mirrors, about the map's origin.
QTransform mapStretchTransform(double factor_x, double factor_y, bool mirror_horizontally, bool mirror_vertically);


}  // namespace OpenOrienteering

#endif
