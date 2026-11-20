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

#include "course/course_data.h"
#include "course/route_search.h"

namespace OpenOrienteering {

class Map;


/**
 * The fastest routes of the legs of a course.
 *
 * See the design notes, "Route analysis: the routes of a course".
 */

/** A leg of a course with its route, or a marked route which is followed. */
struct CourseRouteLeg
{
	/** The codes of the items between which the leg runs; for a marked route its code twice. */
	QString from;
	QString to;
	/** The leg is a marked route itself: its path, and the time along it. */
	bool marked = false;
	/** The straight length of the leg in m; for a marked route that of its path. */
	double straight_m = 0.0;
	RouteResult route;
};

struct CourseRoutes
{
	/** False for a course which is not there, which has variations, or which has no leg. */
	bool analysed = false;
	QString course;
	/** The part of the course, counted from 0; -1 for the whole course. */
	int part = -1;
	std::vector<CourseRouteLeg> legs;
	/** The length of the course along its straights and marked routes, in m. */
	double course_length_m = 0.0;
	/** The length and the time along the fastest routes, and the ascent. */
	double length_m = 0.0;
	double time_s = 0.0;
	double climb_m = 0.0;
	/** The legs for which no route was found; their straights count in the sums. */
	int missing = 0;
};

/**
 * Searches the fastest route of every leg of a course, or of one of its
 * parts. A marked route with which the course begins, before its start,
 * is not a leg, as it is not in the length of the course.
 */
CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search);

/** A time as the route tables have it: minutes and seconds, "05:01". */
QString formatRouteTime(double seconds);

/**
 * The table of the routes of a course in the form of the route export: a
 * head, a line for every leg with the length of its route, the sum so far
 * and its time, then the length of the course, the extra length of the
 * routes and the time along the course.
 */
QString courseRoutesText(const CourseRoutes& routes);


}  // namespace OpenOrienteering

#endif
