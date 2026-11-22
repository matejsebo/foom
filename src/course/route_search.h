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


#ifndef FOOM_ROUTE_SEARCH_H
#define FOOM_ROUTE_SEARCH_H

#include <cstddef>
#include <functional>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"
#include "course/route_terrain.h"

namespace OpenOrienteering {


/**
 * The fastest route between two places of the map, over the rasters of
 * the terrain.
 *
 * See the design notes, "Route analysis: the search".
 */

/**
 * By what the time of a stretch is multiplied for its gradient, the
 * climb over the horizontal length;
 * see "Route timing: measured slope model".
 */
double slopeFactor(double gradient);

/**
 * Which part of the full extra time across a slope a step has at this
 * angle to the fall line, in radians: 0 straight up or down, 1/3 from
 * atan(1/2) to atan(2), 1 along the contour, and straight lines between.
 * See "Route analysis: the extra time across a slope".
 */
double sideSlopeFactor(double angle_to_fall_line);

/**
 * The extra time across a slope per m of horizontal length, in units of
 * the pace: 5.0 times the cube of the slope of the terrain times
 * sideSlopeFactor() of the angle.
 */
double sideSlopeExtra(double slope, double angle_to_fall_line);

/**
 * The slope factor which an edge has on the average over
 * its places: its heading (ux, uy), a unit vector to the east and to the
 * south, its horizontal length in m, the gradient (gx to the east, gy to
 * the south) and the size of the cells of the grid in m, whose nearest
 * cell gives the height of a node. Without a cell size, slopeFactor() of
 * the gradient along the heading.
 */
double edgeSlopeFactor(double gx, double gy, double ux, double uy, double edge_m, double cell_m);

/** The height of a place of the map in m, or NaN where it is not known. */
using RouteHeights = std::function<double(const MapCoordF&)>;

struct RouteResult
{
	bool found = false;
	/** The route from the one place to the other, in mm of the map. */
	std::vector<MapCoordF> points;
	/** Its horizontal length in m. */
	double length_m = 0.0;
	/** The time in s. */
	double time_s = 0.0;
	/** The sum of the ascents along it in m; 0 without heights. */
	double climb_m = 0.0;
};

/**
 * Searches routes on a terrain. Every passable pixel is a place; from a
 * pixel a runner goes to its 16 neighbours of the 16 headings.
 * The time of a step is its length times the base pace,
 * times 100 over the runnability of the pixels it touches, times the
 * factor of its gradient.
 */
class RouteSearch
{
public:
	/** The terrain must outlive the search. */
	RouteSearch(const RouteTerrain& terrain, const RouteSettings& settings, unsigned int scale_denominator);
	~RouteSearch();
	RouteSearch(const RouteSearch&) = delete;
	RouteSearch& operator=(const RouteSearch&) = delete;
	
	/** Gives the heights; they count when the settings use the elevation. */
	void setHeights(RouteHeights heights);
	/** Heights were given and the settings use them. */
	bool usesHeights() const;
	
	/**
	 * The size of the cells of the grid of the heights in m; 0 for none.
	 * With a size the slope factor of a step is the expected one of an
	 * edge of its heading, edgeSlopeFactor().
	 */
	void setGridCell(double cell_m);
	
	/**
	 * The fastest route. A place on an impassable pixel is taken to the
	 * nearest passable one within a few pixels. Not found when a place lies
	 * outside the terrain or no way leads from the one to the other.
	 */
	RouteResult fastest(const MapCoordF& from, const MapCoordF& to);
	
	/**
	 * The route which is led over via points: the fastest routes from the
	 * beginning over every via point, in their order, to the end, joined.
	 * Found when all of them are.
	 */
	RouteResult fastestVia(const MapCoordF& from, const std::vector<MapCoordF>& via, const MapCoordF& to);
	
	/**
	 * Alternatives to the fastest route between two places: routes which
	 * keep away from it and from each other over more than a fifth of their
	 * length, and take at most a quarter longer. At most count of them, by
	 * their time. See the design notes, "Route analysis:
	 * alternative routes".
	 */
	std::vector<RouteResult> alternatives(const MapCoordF& from, const MapCoordF& to, const RouteResult& fastest, int count);
	
	/** The time of the straight line between two places as if nothing were impassable, in s: what a course costs without routes. */
	double straightTime(const MapCoordF& from, const MapCoordF& to) const;
	
	/**
	 * Along a straight piece of a marked route: its length, its time, which
	 * with heights is the flat time plus the correction of the climb times
	 * what the slope adds, and its climb.
	 */
	RouteResult followLine(const MapCoordF& from, const MapCoordF& to) const;
	
private:
	/** The search; with chain, the pixels of the route from the beginning. */
	RouteResult search(const MapCoordF& from, const MapCoordF& to, std::vector<std::size_t>* chain);
	
	struct Data;
	Data* d;
};


}  // namespace OpenOrienteering

#endif
