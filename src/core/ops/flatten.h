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


#ifndef OPENORIENTEERING_OPS_FLATTEN_H
#define OPENORIENTEERING_OPS_FLATTEN_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of flatten().
 */
struct FlattenParams : public OpParams
{
	/** The largest allowed deviation of the segments from the curve, in mm. */
	double tolerance = 0.01;
};


/**
 * Replaces the curves of path objects by straight segments ("to polyline").
 *
 * For every input which is a path object with at least one curve, the
 * result holds a copy without curves, with the input's symbol, tags and
 * identity, and the input is consumed. Curves are subdivided until the
 * control points lie within params.tolerance of the chord, so that the
 * segments deviate from the curve by no more than the tolerance. Vertices
 * keep their dash, gap, hole and close flags. Other inputs are left alone.
 *
 * See the design notes, "To polyline".
 */
OpResult flatten(const Map& map, const std::vector<const Object*>& in, const FlattenParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
