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


#ifndef FOOM_COURSE_RENDER_H
#define FOOM_COURSE_RENDER_H

#include <memory>
#include <vector>

#include <QtGlobal>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"
#include "course/course_data.h"

class QPainter;

namespace OpenOrienteering {

class LineSymbol;
class Map;
class Object;
class Symbol;
class TextSymbol;


/**
 * The drawing of a course, its length and climb.
 *
 * See the design notes, "Drawing a course, its length and climb".
 */

/**
 * The extent of a symbol in mm: for a point symbol the
 * farthest reach of its elements from its centre plus half their line
 * width; for a line symbol half its width; 0 for anything else.
 */
double courseSymbolExtent(const Symbol* symbol);

/** The line symbol 705 of the connection lines, or nullptr. */
const LineSymbol* courseLineSymbol(const Map& map);

/** The text symbol 704 of the control numbers, or nullptr. */
const TextSymbol* courseNumberSymbol(const Map& map);


struct CourseLength
{
	/** False for a course with variations and for one with an item whose object is gone. */
	bool known = false;
	/** The straights and the marked routes, in m. */
	double measured_m = 0.0;
	/** With the extra length of the course, in m. */
	double total_m = 0.0;
};

/**
 * The length of a course: the straights between the places of its items
 * and the marked routes along their paths, times the scale of the map.
 * The straight from a marked route with which the course begins to the
 * start which follows is left out.
 */
CourseLength measureCourse(const Map& map, const CourseData& data, const Course& course);

/** A length as the course dock shows it: in km, rounded to 10 m ("1.500 km"). */
QString formatCourseLength(double metres);


/** The connection between two items which follow each other. */
struct CourseLegDrawing
{
	QString from;
	QString to;
	/** The straight between the two places, shortened by the gaps of the symbols. */
	MapCoordF begin;
	MapCoordF end;
	/** False when nothing is left of the straight, or when overrides take its place. */
	bool has_line = false;
	bool overridden = false;
};

/** The number of a control. */
struct CourseNumberDrawing
{
	QString code;
	/** "3", or "3/6/9" for a control which is visited more than once. */
	QString text;
	/** The left end of the base line of the text. */
	MapCoordF anchor;
	/** The box of the text, in map coordinates. */
	QRectF box;
	/** The free direction in degrees, from east, anticlockwise; in (-180, 180]. */
	double direction = 0.0;
	bool overridden = false;
};

/** The angle to which a start symbol is turned. */
struct CourseStartDrawing
{
	QString code;
	/** Degrees, anticlockwise; 0 leaves the symbol as it is drawn, its tip up. */
	double angle = 0.0;
};

struct CourseDrawing
{
	CourseDrawing();
	CourseDrawing(CourseDrawing&&) noexcept;
	CourseDrawing& operator=(CourseDrawing&&) noexcept;
	~CourseDrawing();
	
	/** False for a course which is not there and for one with variations. */
	bool drawn = false;
	std::vector<CourseLegDrawing> legs;
	std::vector<CourseNumberDrawing> numbers;
	std::vector<CourseStartDrawing> starts;
	/**
	 * The lines and the numbers as objects of the symbols 705 and 704,
	 * and copies of the objects of overrides. Empty when the map has not
	 * both symbols.
	 */
	std::vector<std::unique_ptr<Object>> objects;
};

/**
 * Draws a course: its legs, its numbers and the angles of its starts.
 * An item whose object is gone is passed over.
 */
CourseDrawing drawCourse(const Map& map, const CourseData& data, const QString& course);

/**
 * Paints a drawing with a painter which is set up for map coordinates:
 * the objects, when there are any, as the map views paint objects, and
 * otherwise plain lines and numbers in purple.
 *
 * @param bounding_box  the part of the map which is painted, in mm
 * @param scaling       pixels per mm
 */
void paintCourseDrawing(QPainter* painter, Map* map, const CourseDrawing& drawing, const QRectF& bounding_box, qreal scaling);


}  // namespace OpenOrienteering

#endif
