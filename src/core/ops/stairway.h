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


#ifndef OPENORIENTEERING_OPS_STAIRWAY_H
#define OPENORIENTEERING_OPS_STAIRWAY_H

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Symbol;

namespace ops {

/** The footprint of a stairway: three corners of a rectangle. */
struct StairwayGeometry
{
	MapCoordF first;   ///< A: the first tread runs from here ...
	MapCoordF second;  ///< B: ... to here.
	MapCoordF last;    ///< D: the corner next to A on the other side; the run is D - A.
};

struct StairwayParams : public OpParams
{
	/** The line symbol of the treads. */
	const Symbol* symbol = nullptr;
	/** The tread depth in mm on paper. */
	double tread_mm = 0.5;
	/** The group of the treads, 0 for none. */
	int group = 0;
};

/**
 * Makes the treads of a stairway: N + 1 lines across the footprint.
 *
 * See the design notes, "Stairway".
 */
OpResult stairway(const Map& map, const StairwayGeometry& geometry, const StairwayParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
