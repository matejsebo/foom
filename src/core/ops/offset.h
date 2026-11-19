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


#ifndef OPENORIENTEERING_OPS_OFFSET_H
#define OPENORIENTEERING_OPS_OFFSET_H

#include <vector>

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class PathObject;

namespace ops {

/**
 * Parameters of offset().
 */
struct OffsetParams : public OpParams
{
	/** The distance in mm, positive to the left of travel on paper; not zero. */
	double distance = 0.0;
	
	/** Keep the inputs and add the outputs, instead of replacing the inputs. */
	bool duplicate = false;
	
	/** The flattening tolerance and the chord error of round joins, mm. */
	double tolerance = 0.01;
	
	/** Convert the output back to curves where the input had curves. */
	bool refit_curves = true;
	
	/** The simplification threshold of the curve fit, mm. */
	double curve_threshold = 0.05;
};


/**
 * Offsets the path objects among the inputs by the signed distance
 * ("move-parallel").
 *
 * Closed parts stay closed; the holes of a closed object are offset so that
 * the area grows or shrinks uniformly. The outputs carry the inputs' symbol,
 * tags, pattern and identity; without params.duplicate the inputs are
 * consumed. Objects which are not paths are ignored. An input without a path
 * and a zero distance are errors.
 *
 * See the design notes, "Parallel offset".
 */
OpResult offset(const Map& map, const std::vector<const Object*>& in, const OffsetParams& params);

/**
 * Returns the signed distance of the position from the path: positive to
 * the left of travel on paper, as offset() takes it.
 */
double signedDistance(const PathObject& path, const MapCoordF& position);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
