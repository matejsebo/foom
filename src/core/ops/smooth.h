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


#ifndef OPENORIENTEERING_OPS_SMOOTH_H
#define OPENORIENTEERING_OPS_SMOOTH_H

#include <functional>
#include <vector>

#include "core/ops/op.h"

class QPointF;

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of smoothPaths().
 */
struct SmoothParams : public OpParams
{
	/** The largest deviation (mm) the simplification may introduce. */
	double tolerance = 0.1;
	/** Keep vertices where the direction changes by more than corner_angle_deg. */
	bool keep_corners = false;
	double corner_angle_deg = 30.0;
	/** Keep vertices where the curvature changes direction. */
	bool keep_inflections = false;
	/**
	 * The contour option: keep the smoothed line within height_tolerance_m
	 * of its own height (its "ele" tag, else the median of the grid heights
	 * at its vertices), measured against the original line nearby.
	 * See the design notes, "Smooth".
	 */
	bool contour = false;
	double height_tolerance_m = 0.03;
	/** The terrain height in metres at a map position (mm), NaN where unknown; needed by the contour option. */
	std::function<double (const QPointF&)> height_at;
};

/**
 * Simplifies and curve-fits paths ("topo-smooth").
 *
 * See the design notes, "Smooth".
 */
OpResult smoothPaths(const Map& map, const std::vector<const Object*>& in, const SmoothParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
