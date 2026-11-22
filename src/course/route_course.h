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


#ifndef FOOM_ROUTE_COURSE_H
#define FOOM_ROUTE_COURSE_H

#include <vector>

#include <QString>
#include <QStringList>

#include "course/course_data.h"
#include "course/route_search.h"
#include "course/route_store.h"

namespace OpenOrienteering {

class Map;


/**
 * The fastest routes of the legs of a course, and of all courses.
 *
 * See the design notes, "Route analysis: the routes of a
 * course" and "Route analysis: all courses, and the routes in the file".
 */

/** A leg of a course with its route, or a marked route which is followed. */
struct CourseRouteLeg
{
	/** The codes of the items between which the leg runs; for a marked route its code twice. */
	QString from;
	QString to;
	/** The leg is a marked route itself: its path, and the time along it. */
	bool marked = false;
	/** False for a leg which has not been analysed, where the routes are those which the map keeps. */
	bool known = true;
	/** The straight length of the leg in m; for a marked route that of its path. */
	double straight_m = 0.0;
	/** One of its ends lies outside the perimeter of the analysis: it has no route. */
	bool outside = false;
	RouteResult route;
	/** The kept routes of the leg over via points and their indexes, where the routes are those which the map keeps. */
	std::vector<RouteResult> via_routes;
	std::vector<int> via_indexes;
};

struct CourseRoutes
{
	/** False for a course which is not there, which has variations, or which has no leg. */
	bool analysed = false;
	QString course;
	/** For a variant of a relay its code, "1AA"; the table is then named by the course and the code. */
	QString variant;
	/** The part of the course, counted from 0; -1 for the whole course. */
	int part = -1;
	std::vector<CourseRouteLeg> legs;
	/** The length of the course as the course box has it, without its extra length, in m. */
	double course_length_m = 0.0;
	/** The length and the time along the fastest routes, and the ascent. */
	double length_m = 0.0;
	double time_s = 0.0;
	double climb_m = 0.0;
	/** The search had heights: the climb is known. */
	bool with_heights = false;
	/** The legs for which no route was found; their straights count in the sums. */
	int missing = 0;
	/** The legs which lie out of the perimeter; they are not among the missing ones. */
	int outside = 0;
	/** The legs which have not been analysed; their straights count in the sums. */
	int unknown = 0;
};

/**
 * Searches the fastest route of every leg of a course, or of one of its
 * parts. The whole course has a leg between every two items with a place
 * which follow each other, from a marked route before the start on and
 * across a map exchange or flip; a part has the legs between its items.
 */
CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search);

/**
 * The same with routes which are kept: a leg whose route is in the store,
 * with its ends where the leg begins and ends, is not searched again, and
 * every route which is searched is put into the store.
 */
CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search, StoredRoutes& store);

/**
 * The routes of a course from those which the course data keep, without
 * a search. A leg whose route is not kept, or no longer ends where the
 * leg does, is not known.
 */
CourseRoutes storedCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part);


/** What addViaRoute() did. */
struct ViaRouteAdded
{
	enum Reason
	{
		Added,
		NoFastestRoute,  ///< the fastest route of the leg is not kept
		NoRoute,         ///< no way leads over the via points
		OtherHeights     ///< the search has heights and the kept routes have none, or the other way round
	};
	Reason reason = NoFastestRoute;
	/** The index under which the route is kept. */
	int index = 0;
	RouteResult route;
	/** The fastest route of the leg, to compare with. */
	double fastest_length_m = 0.0;
	double fastest_time_s = 0.0;
};

/**
 * Searches the route of a leg over via points, between the ends of its
 * fastest route, which must be kept, and keeps it under the next index
 * of the leg.
 */
ViaRouteAdded addViaRoute(StoredRoutes& store, RouteSearch& search, const QString& from, const QString& to, const std::vector<MapCoordF>& via);

/**
 * The leg whose route passes nearest to a place, within a reach in mm of
 * the map; -1 when there is none. A marked route is no leg.
 */
/**
 * Searches up to count alternatives to the kept fastest route of a leg and
 * keeps them in the place of its earlier alternatives; the routes over via
 * points stay. Returns how many were kept: none for a leg whose fastest
 * route is not kept, or was searched with other heights. See
 * the design notes, "Route analysis: alternative routes".
 */
int addAlternativeRoutes(StoredRoutes& store, RouteSearch& search, const QString& from, const QString& to, int count);

/** addAlternativeRoutes() for every leg of a course whose fastest route is kept; returns how many were kept. */
int addCourseAlternatives(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search, StoredRoutes& store, int count);

int routeLegAt(const CourseRoutes& routes, const MapCoordF& place, double reach);

/** The same for the routes over via points: the leg, and which of its routes. False when there is none. */
bool viaRouteAt(const CourseRoutes& routes, const MapCoordF& place, double reach, int& leg, int& via);


/** The routes of all courses. */
struct AllCourseRoutes
{
	/** A table for every course, and for every variant of a relay, in the order of the courses. */
	std::vector<CourseRoutes> tables;
	/** The relays which were left out for a fault of their variations. */
	QStringList skipped;
	/** How many alternative routes were kept. */
	int alternatives = 0;
};

/**
 * Analyses every course, a relay by its variants: those which have a
 * course map, of a one-man relay every loop once. Every leg is searched
 * once; the routes are put into the store, and what the store has and
 * fits is not searched.
 */
AllCourseRoutes analyseAllRoutes(const Map& map, const CourseData& data, RouteSearch& search, StoredRoutes& store);

/** The same from the routes which the course data keep, without a search. */
AllCourseRoutes storedAllRoutes(const Map& map, const CourseData& data);

/** The legs of the tables which lie out of the perimeter, every leg once. */
int routeLegsOutside(const AllCourseRoutes& all);

/**
 * Analyses all courses of a map with its route settings, the rasters of
 * its terrain and the heights of its terrain model. The store is emptied
 * first.
 */
AllCourseRoutes analyseAllRoutesOfMap(Map& map, StoredRoutes& store, int alternatives = 0);

/**
 * As above, with the rasters of the cache at cache_path (none when empty);
 * source says where they came from. See the design notes,
 * "Route analysis: the cache of the rasters".
 */
AllCourseRoutes analyseAllRoutesOfMap(Map& map, StoredRoutes& store, int alternatives, const QString& cache_path, RouteTerrainSource* source);

/**
 * The heights of the terrain model of a map for the search, by the
 * georeferencing of the map; no function for a map without a model.
 * The map and its model must outlive the function.
 */
RouteHeights routeHeightsOf(Map& map);

/** The size of the cells of the terrain model of a map, in its projected units; 0 without a model. */
double routeGridCellOf(Map& map);

/** Gives a search the heights of the map and the size of the cells of its model. */
void setRouteHeights(RouteSearch& search, Map& map);

/** A time as the route tables have it: minutes and seconds, "05:01". */
QString formatRouteTime(double seconds);

/**
 * The table of the routes of a course in the form of the route export: a
 * head, a line for every leg with the length of its route, the sum so far
 * and its time, then the length of the course, the extra length of the
 * routes and the time along the course. When the search had heights, a
 * column with the climb along every route and a line with their sum more.
 */
QString courseRoutesText(const CourseRoutes& routes);

/**
 * The file of the export of the routes: every route of the store once,
 * "Leg S1-31: <tab>291m", in the order of the objects, then for every
 * course a line of dashes and its table. Lines end with a line feed.
 */
QString allRoutesText(const CourseData& data, const StoredRoutes& store, const AllCourseRoutes& all);


}  // namespace OpenOrienteering

#endif
