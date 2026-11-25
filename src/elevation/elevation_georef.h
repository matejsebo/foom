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


#ifndef FOOM_ELEVATION_GEOREF_H
#define FOOM_ELEVATION_GEOREF_H

#include <QString>

#include "core/georeferencing.h"

namespace OpenOrienteering {

class Map;


/**
 * The georeferencing by which elevation data meets a map.
 *
 * Elevation grids are kept in metres, in the plane and in the height: the
 * slopes, the areas and the lengths of everything made from them are metres.
 * For a map whose projected system counts in another unit (the feet of the
 * State Plane systems) this is the map's georeferencing in the same projection
 * with the metre as its unit: the same place, declination and scale, so that a
 * point of a grid lands on the map where it belongs. For every other map it is
 * a copy of the map's own.
 *
 * See the design notes, "State Plane and feet".
 */
Georeferencing elevationGeoreferencing(const Georeferencing& georef);
Georeferencing elevationGeoreferencing(const Map& map);

/**
 * The projected system of elevationGeoreferencing(): the system which the
 * map's grids are warped into.
 */
QString elevationCrs(const Georeferencing& georef);
QString elevationCrs(const Map& map);


}  // namespace OpenOrienteering

#endif
