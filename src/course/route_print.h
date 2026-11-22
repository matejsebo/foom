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


#ifndef FOOM_ROUTE_PRINT_H
#define FOOM_ROUTE_PRINT_H

#include <QString>
#include <QStringList>

class QPainter;

namespace OpenOrienteering {

struct CourseRoutes;
class Map;
class MapView;


/**
 * A print of the routes of the route analysis on their own: the map with
 * the kept routes of a course on a page, without the course.
 *
 * See the design notes, "Route analysis: a print of the routes".
 */

/**
 * Paints the captions of the routes of a course, in mm of the map: the
 * caption of every route of 240 m or more, as in OCD files, beside its
 * middle, in the colour of its line with a white framing.
 */
void paintRouteCaptions(QPainter* painter, const CourseRoutes& routes, bool with_heights);

struct RouteMapOptions
{
	/** The PDF file. */
	QString path;
	/** The courses which get a page; all with kept routes when empty. */
	QStringList courses;
	/** The name of an extent of the map which is printed; the print area or the whole map when empty. */
	QString extent;
	bool captions = true;
	/** The view whose template visibility applies; all templates when none. */
	const MapView* view = nullptr;
};

struct RouteMapReport
{
	bool ok = false;
	QString error;
	int pages = 0;
};

/** Writes the PDF file with a page for every course, or variant of a relay, of which routes are kept. */
RouteMapReport exportRoutesMap(Map& map, const RouteMapOptions& options);


}  // namespace OpenOrienteering

#endif
