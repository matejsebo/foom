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


#ifndef OPENORIENTEERING_OPS_TO_CURVE_H
#define OPENORIENTEERING_OPS_TO_CURVE_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of toCurve().
 */
struct ToCurveParams : public OpParams
{
	/** Keep straight segments longer than 4 times the median length ("e.g. roads"). */
	bool keep_straight_parts = false;
	/** The handles are 1 + tangent_factor times the length of the spline's. */
	double tangent_factor = 0.0;
	/** The handles at the ends of open parts point along the end segments. */
	bool keep_end_angles = true;
	/** A vertex where the direction turns by more than corner_angle_deg stays a corner. */
	bool keep_corners = true;
	double corner_angle_deg = 90.0;
};

/**
 * Turns the straight segments of paths into Bézier curves through the same
 * vertices ("to-curve").
 *
 * See the design notes, "Polyline to curve".
 */
OpResult toCurve(const Map& map, const std::vector<const Object*>& in, const ToCurveParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
