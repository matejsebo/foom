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
	
	/**
	 * The fastest route. A place on an impassable pixel is taken to the
	 * nearest passable one within a few pixels. Not found when a place lies
	 * outside the terrain or no way leads from the one to the other.
	 */
	RouteResult fastest(const MapCoordF& from, const MapCoordF& to);
	
	/** The time of the straight line between two places as if nothing were impassable, in s: what a course costs without routes. */
	double straightTime(const MapCoordF& from, const MapCoordF& to) const;
	
private:
	struct Data;
	Data* d;
};


}  // namespace OpenOrienteering

#endif
