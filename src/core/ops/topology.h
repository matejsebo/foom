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


#ifndef OPENORIENTEERING_OPS_TOPOLOGY_H
#define OPENORIENTEERING_OPS_TOPOLOGY_H

#include <vector>

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;

namespace ops {

/**
 * Parameters of joinLines().
 */
struct JoinParams : public OpParams
{
	/** Ends closer than this (mm) are joined. */
	double tolerance = 0.25;
};

/**
 * Joins open line objects of the same symbol whose ends meet within the
 * tolerance, nearest pair first ("topo-join").
 *
 * See the design notes, "Topology commands".
 */
OpResult joinLines(const Map& map, const std::vector<const Object*>& in, const JoinParams& params);

/**
 * Joins the lines as joinLines() does within groups of one height (the "ele" tag),
 * so that contour lines of different heights never join ("dem-merge-contours").
 *
 * See the design notes, "Merging contour lines".
 */
OpResult mergeContourLines(const Map& map, const std::vector<const Object*>& in, const JoinParams& params);

/**
 * Closes the open parts of area objects ("close-areas").
 */
OpResult closeAreas(const Map& map, const std::vector<const Object*>& in, const OpParams& params);

/**
 * Makes outer rings of area objects run anticlockwise on paper and holes
 * clockwise ("areas-ccw").
 */
OpResult areasAnticlockwise(const Map& map, const std::vector<const Object*>& in, const OpParams& params);

/**
 * Drops vertices equal to their predecessor ("remove-duplicate-vertices").
 */
OpResult removeDuplicateVertices(const Map& map, const std::vector<const Object*>& in, const OpParams& params);

/**
 * Returns the coordinates without consecutive repeated vertices; flags are
 * kept by OR, curve handles are left alone. Sets changed when a vertex was
 * dropped.
 */
MapCoordVector withoutRepeatedVertices(const MapCoordVector& coords, bool& changed);

/**
 * Returns the signed area of a closed ring of coordinates with y up, so
 * that a ring running anticlockwise on paper has a positive area.
 */
double signedAreaOnPaper(const MapCoordVector& coords, MapCoordVector::size_type first, MapCoordVector::size_type last);

/**
 * Parameters of fixOvershoots().
 */
struct OvershootParams : public OpParams
{
	bool remove_overshoots = true;
	bool remove_undershoots = true;
	/** How far an end may overshoot or fall short, in mm. */
	double tolerance = 0.25;
};

/**
 * Trims line ends which overshoot another input path by less than the
 * tolerance and extends ends which fall short of one by less than it
 * ("fix-overshoots").
 */
OpResult fixOvershoots(const Map& map, const std::vector<const Object*>& in, const OvershootParams& params);

/**
 * Parameters of intersections().
 */
struct IntersectionParams : public OpParams
{
	enum Action { InsertVertex, Cut, AddPoint };
	Action action = InsertVertex;
	/** For InsertVertex: make the new vertices dash points. */
	bool dash = false;
	/** For AddPoint: the symbol of the new point objects. */
	const Symbol* point_symbol = nullptr;
	/** Count crossings at an end of one of the two paths. */
	bool include_touching = true;
};

/**
 * Acts at every crossing of two different input paths: inserts vertices,
 * cuts the paths or adds point objects ("intersections").
 */
OpResult intersections(const Map& map, const std::vector<const Object*>& in, const IntersectionParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
