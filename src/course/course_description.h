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


#ifndef FOOM_COURSE_DESCRIPTION_H
#define FOOM_COURSE_DESCRIPTION_H

#include <array>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QChar>
#include <QCoreApplication>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "course/course_data.h"

class QPainter;

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;


/**
 * The control descriptions of a course: what they say, and the block
 * which shows them in the map.
 *
 * See the design notes, "The description of a course".
 */

/** One row of the description of a course. */
struct CourseDescriptionRow
{
	enum Kind
	{
		RouteToStart,    ///< a marked route with which the course begins
		Start,
		Control,
		Route,           ///< a marked route between controls
		CrossingPoint,
		Passage,
		TextBlock,
		MapExchange,
		MapFlip,
		Finish
	};
	
	enum Tapes
	{
		NoTapes,
		Taped,
		Funnel
	};
	
	Kind kind = Control;
	/** The part of the course in which the row is; a map exchange or flip ends a part. */
	int part = 0;
	/** The number of a control, counted through the whole course. */
	int number = 0;
	/** The code of the start, the control, the text block or the finish, or of the marked route of a row of its own. */
	QString code;
	/** The description columns C to H of a start or a control: symbol number times 1000. */
	std::array<int, CourseObject::ColumnCount> cell = {};
	/** The size which stands in column F as a text. */
	QString size;
	/** The text of a start, a control or a text block; for the rows with a marked route its own text. */
	QString text;
	/** The distance which the row states, in m, rounded to 10; -1 when it states none. */
	int distance = -1;
	/** For a finish and a map exchange: whether a marked route leads there. */
	Tapes tapes = NoTapes;
};


struct CourseDescription
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::CourseDescription)
	
public:
	/** False for a course which is not there and for one with variations. */
	bool known = false;
	/** The title of the event. */
	QString title;
	/** The classes which run the course. */
	QStringList classes;
	QString course;
	/** "1.5 km": the length rounded to 100 m; empty for a course which cannot be measured. */
	QString length;
	/** "10 m"; empty for a climb of 0. */
	QString climb;
	/** The number of the parts into which map exchanges and flips divide the course. */
	int parts = 0;
	std::vector<CourseDescriptionRow> rows;
};


/** The value of a field of the OCD record of a course object which the model does not name, or an empty text. */
QString courseObjectField(const CourseObject& object, QChar letter);

/** Sets such a field, in its place when the object has it and behind the others when not; an empty value removes it. */
void setCourseObjectField(CourseObject& object, QChar letter, const QString& value);

/**
 * The text which the cells of a course object say: the names of their
 * symbols in the order of the columns, with the size in the place of
 * column F.
 */
QString automaticCourseObjectText(const Map& map, const CourseObject& object);

/**
 * The text of the description of a course object: its own text, and
 * when it has none, the automatic one.
 */
QString courseObjectText(const Map& map, const CourseObject& object);


/**
 * The editing of a description. The functions work on course data and
 * change nothing when they return false; the caller puts the result into
 * the map as an undo step.
 *
 * See the design notes, "The editor of a description".
 */

/** Returns true for the kinds which have cells and a size: start and control. */
bool hasDescriptionCells(CourseObject::Kind kind);

/** Returns true for the kinds which have a text: start, control, marked route and text block. */
bool hasDescriptionText(CourseObject::Kind kind);

/** The number of a symbol as the cells have it: 1.010 is 1010. */
int descriptionCellNumber(const Symbol& symbol);

/** The description symbols of the map which may stand in a column, by number. */
std::vector<const Symbol*> descriptionSymbols(const Map& map, CourseObject::Column column);

/**
 * Puts a symbol into a cell of a start or a control; 0 empties the cell.
 * The symbol must be a description symbol of the map for that column.
 * A symbol in F takes the size away. A text which was the automatic one
 * follows the cells.
 */
bool setCourseObjectCell(const Map& map, CourseData& data, const QString& code, CourseObject::Column column, int number);

/**
 * Sets the size of a start or a control: a text without white space, of
 * which an "m" at its end is left out. A size takes the symbol of F away.
 * A text which was the automatic one follows.
 */
bool setCourseObjectSize(const Map& map, CourseData& data, const QString& code, const QString& size);

/**
 * Sets the text of a start, a control, a marked route or a text block,
 * as one line with single blanks.
 */
bool setCourseObjectText(CourseData& data, const QString& code, const QString& text);

/** Says whether a marked route is the funnel only, or taped all the way. */
bool setMarkedRouteFunnel(CourseData& data, const QString& code, bool funnel);

/** The description of a course. */
CourseDescription describeCourse(const Map& map, const CourseData& data, const QString& course);

/** The line of a row in the description in text. */
QString courseDescriptionLine(const CourseDescriptionRow& row);

/**
 * The description in text: three lines of the head, and a line for
 * every row of the part, or of all parts when the part is negative.
 */
QStringList courseDescriptionLines(const CourseDescription& description, int part = -1);


/**
 * Where everything of a description stands, in mm from the upper left
 * corner of its first block, the y axis pointing down.
 */
struct CourseDescriptionLayout
{
	struct Row
	{
		/** The index of the row in the description; -1 for a row of the head. */
		int index = -1;
		int block = 0;
		QRectF rect;
		/** The rule above the row is thick. */
		bool thick_above = true;
	};
	
	struct Rule
	{
		QPointF begin;
		QPointF end;
		bool thick = false;
	};
	
	struct Pictogram
	{
		/** The number of the symbol times 1000, as in the cells. */
		int symbol = 0;
		QPointF centre;
	};
	
	struct Text
	{
		enum Style
		{
			Number,     ///< symbol 19.001
			Code,       ///< 19.002
			Title,      ///< 19.003
			Distance,   ///< 19.004
			Size,       ///< 19.005
			TextTitle,  ///< 19.006, a line of the head of a description in text
			TextRow     ///< 19.007, a line of a description in text
		};
		
		Style style = Title;
		/** On the base line: its middle, and for TextTitle and TextRow its left end. */
		QPointF anchor;
		QString text;
		/** The width of its cells, 0 for no limit; what is drawn plainly is made smaller to fit them. */
		double width = 0;
	};
	
	/** The size of a box in mm. */
	double box = 6.0;
	/** By what the symbols are scaled, which are drawn for a box of 6 mm. */
	double scale = 1.0;
	/** A description in text; then there are only texts. */
	bool in_text = false;
	/** The frames of the blocks. */
	std::vector<QRectF> blocks;
	/** The white areas under the blocks. */
	std::vector<QRectF> backgrounds;
	std::vector<Row> rows;
	std::vector<Rule> rules;
	std::vector<Pictogram> pictograms;
	std::vector<Text> texts;
};

/**
 * Lays out the block of a description with the options of a course
 * project: of one part, or of all parts when the part is negative.
 */
CourseDescriptionLayout layoutCourseDescription(const CourseDescription& description, const CourseOptions& options, int part = -1);

/** Lays out the description in text. */
CourseDescriptionLayout layoutCourseDescriptionText(const CourseDescription& description, const CourseOptions& options, int part = -1);


/** The descriptions of a course at the description objects of a map. */
struct CourseDescriptionDrawing
{
	CourseDescriptionDrawing();
	CourseDescriptionDrawing(CourseDescriptionDrawing&&) noexcept;
	CourseDescriptionDrawing& operator=(CourseDescriptionDrawing&&) noexcept;
	~CourseDescriptionDrawing();
	
	/** False for a course which has no description. */
	bool drawn = false;
	/** The layouts, moved to their description objects. */
	std::vector<CourseDescriptionLayout> layouts;
	/** For every layout: it is drawn plainly, because the map has not the symbols of its rules and texts. */
	std::vector<bool> plain;
	/** Copies of symbols of the map which are scaled to the size of the box; the objects refer to them. */
	std::vector<std::unique_ptr<Symbol>> symbols;
	/** Rules, pictograms, texts and backgrounds as objects of the symbols of the map or of their copies. */
	std::vector<std::unique_ptr<Object>> objects;
};

/**
 * Draws the description of a course at every description object of the
 * map: of one part, counted from 0, or of all parts when the part is
 * negative. A part which the course has not is not drawn.
 */
CourseDescriptionDrawing drawCourseDescriptions(const Map& map, const CourseData& data, const QString& course, int part = -1);

/**
 * Paints the descriptions with a painter which is set up for map coordinates.
 *
 * @param bounding_box  the part of the map which is painted, in mm
 * @param scaling       pixels per mm
 * @param opacity       of everything painted
 */
void paintCourseDescriptions(QPainter* painter, Map* map, const CourseDescriptionDrawing& drawing, const QRectF& bounding_box, qreal scaling, qreal opacity = 1.0);


}  // namespace OpenOrienteering

#endif
