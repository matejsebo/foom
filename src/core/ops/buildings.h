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


#ifndef OPENORIENTEERING_OPS_BUILDINGS_H
#define OPENORIENTEERING_OPS_BUILDINGS_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of makeRectangular().
 */
struct RectangularParams : public OpParams
{
	/** Edges within this angle (degrees) of the principal direction or its perpendicular are snapped. */
	double tolerance_deg = 15.0;
	/** A line object whose chord gives the principal direction; null means the object's own. */
	const Object* reference = nullptr;
	/** Move the result so that its nearest edge parallel to the reference is at distance_mm from it. */
	bool place_at_distance = false;
	double distance_mm = 0.0;
};

/**
 * Snaps the near-axis edges of closed paths square ("make-rectangular").
 *
 * See the design notes, "Make rectangular".
 */
OpResult makeRectangular(const Map& map, const std::vector<const Object*>& in, const RectangularParams& params);

/**
 * Parameters of simplifyBuildings().
 */
struct SimplifyBuildingParams : public OpParams
{
	/** Keep dash points. */
	bool keep_corners = true;
	/** Remove a vertex where the direction changes by less than this (degrees). */
	double angle_deg = 1.0;
	/** Remove a vertex closer than this (mm) to the chord between its neighbours. */
	double offset_mm = 0.1;
	/** Scale the result about its centroid so that the area is unchanged. */
	bool compensate_area = true;
};

/**
 * Removes the vertices of closed paths which change little ("generalize-buildings", simplify).
 *
 * See the design notes, "Generalize buildings".
 */
OpResult simplifyBuildings(const Map& map, const std::vector<const Object*>& in, const SimplifyBuildingParams& params);

/**
 * Parameters of buildingRectangles().
 */
struct RectangleParams : public OpParams
{
	enum Mode { Each, All, Series };
	Mode mode = Each;
	/** For Series: how many rectangles. */
	int count = 2;
	/** The rectangle's area relative to the input's, 1.0 meaning equal. */
	double area_factor = 1.0;
};

/**
 * Replaces closed paths by minimum-area bounding rectangles ("generalize-buildings", rectangle).
 *
 * See the design notes, "Generalize buildings".
 */
OpResult buildingRectangles(const Map& map, const std::vector<const Object*>& in, const RectangleParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
