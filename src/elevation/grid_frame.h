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


#ifndef FOOM_ELEVATION_GRID_FRAME_H
#define FOOM_ELEVATION_GRID_FRAME_H

#include <vector>

#include <QPolygonF>
#include <QString>

#include "elevation/elevation_set.h"

namespace OpenOrienteering {

class Map;


/**
 * The footprint of a grid file: the outer edges of its cells, each edge cut
 * into the given number of pieces, from the north-west corner clockwise,
 * projected from the file's system into the target system.
 *
 * Without a target system, or for a file without one, the points stay in the
 * file's coordinates. Empty when GDAL cannot read the file or the points cannot
 * be projected.
 *
 * See the design notes, "The grid's frame".
 */
QPolygonF gridFootprint(const QString& path, const QString& target_crs, int pieces = 16);


/**
 * The outline of one grid of a map's elevation set, in map coordinates (mm).
 */
struct ElevationFrame
{
	ElevationSet::Kind kind;
	QPolygonF ring;
};

/**
 * The outlines of the grids of the map's elevation set, terrain first.
 *
 * A grid whose file GDAL cannot read gets the rectangle of the loaded grid's
 * extent; a grid which cannot be loaded either gets none.
 */
std::vector<ElevationFrame> elevationFrames(Map& map, int pieces = 16);


}  // namespace OpenOrienteering

#endif
