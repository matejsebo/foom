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


#ifndef FOOM_COURSE_DATA_H
#define FOOM_COURSE_DATA_H

#include <array>
#include <vector>

#include <QChar>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace OpenOrienteering {


/**
 * A field of an OCD record which the course model does not know: its
 * letter and its value. Such fields are kept, so that a record which
 * was read can be written again without loss.
 */
struct CourseField
{
	QChar letter;
	QString value;
};

bool operator==(const CourseField& lhs, const CourseField& rhs);
inline bool operator!=(const CourseField& lhs, const CourseField& rhs) { return !(lhs == rhs); }


/**
 * One thing which courses are made of: a start, a control, a finish, a
 * marked route, a control description, a course title, a variant code,
 * a start number or a text block.
 *
 * See the design notes, "Data model".
 */
struct CourseObject
{
	/** The values are those of the OCD records. */
	enum Kind
	{
		Start       = 0,
		Control     = 10,
		MarkedRoute = 20,
		Finish      = 30,
		Description = 40,
		CourseTitle = 50,
		VariantCode = 60,
		StartNumber = 70,
		TextBlock   = 80
	};
	
	/** The columns C to H of a control description. */
	enum Column { ColumnC, ColumnD, ColumnE, ColumnF, ColumnG, ColumnH, ColumnCount };
	
	/** The map object which carries the geometry; null for a text block. */
	QUuid object;
	Kind kind = Control;
	/** "S1", "31", "M1", "F1", "D1", "T1" ... */
	QString code;
	/** The description columns C to H: symbol number times 1000, 0 for an empty cell. */
	std::array<int, ColumnCount> cell = {};
	/** The text description, or the content of a text block. */
	QString text;
	/** The ids of the punching units. */
	QStringList punch_ids;
	bool locked = false;
	/** Fields of the OCD record which are not understood. */
	std::vector<CourseField> other;
};

bool operator==(const CourseObject& lhs, const CourseObject& rhs);
inline bool operator!=(const CourseObject& lhs, const CourseObject& rhs) { return !(lhs == rhs); }


/**
 * One entry in the sequence of a course.
 */
struct CourseItem
{
	enum Type
	{
		Start, Control, MarkedRoute, Finish, TextBlock,
		CrossingPoint,   ///< mandatory crossing point
		Passage,         ///< mandatory passage through an out-of-bounds area
		MapExchange, MapFlip,
		TeamForkBegin,   ///< count is the number of branches
		ForkBranch, TeamForkEnd,
		LegForkBegin,
		LegBranch,       ///< legs names the legs which run this branch, as "1-2"
		LegForkEnd
	};
	
	Type type = Control;
	/** The code of the course object, for the types which name one. */
	QString code;
	QString legs;
	int count = 0;
	
	/** Returns true for the types which name a course object. */
	bool namesObject() const { return type <= TextBlock; }
};

bool operator==(const CourseItem& lhs, const CourseItem& rhs);
inline bool operator!=(const CourseItem& lhs, const CourseItem& rhs) { return !(lhs == rhs); }


struct Course
{
	enum Type { Normal, Relay, OneManRelay };
	
	QString name;
	Type type = Normal;
	/** The number of legs of a relay; 0 when the record gives none. */
	int legs = 0;
	std::vector<CourseItem> items;
	/** The length which is added to the measured one, in km. */
	double extra_km = 0.0;
	/** The record states an extra length, be it 0. */
	bool extra_set = false;
	int climb_m = 0;
	/** The climb was entered and is not computed. A record which states a climb sets this. */
	bool climb_manual = false;
	std::vector<CourseField> other;
};

bool operator==(const Course& lhs, const Course& rhs);
inline bool operator!=(const Course& lhs, const Course& rhs) { return !(lhs == rhs); }


struct CourseClass
{
	QString name;
	QString course;
	/** The number of runners or teams; -1 when none is given. */
	int runners = -1;
	/** The first and the last start number; -1 when none is given. */
	int first_no = -1;
	int last_no = -1;
	QString comment;
	std::vector<CourseField> other;
};

bool operator==(const CourseClass& lhs, const CourseClass& rhs);
inline bool operator!=(const CourseClass& lhs, const CourseClass& rhs) { return !(lhs == rhs); }


/**
 * A change of the graphics of one course: the place of a control number,
 * or the shape of a connection line. It shows only in that course.
 */
struct CourseOverride
{
	enum What { Number, Line };
	
	QString course;
	What what = Number;
	/** The control of the number, or the item at which the line begins. */
	QString code_from;
	/** The item at which the line ends; empty for a number. */
	QString code_to;
	/** The map object which carries the changed geometry. */
	QUuid object;
};

bool operator==(const CourseOverride& lhs, const CourseOverride& rhs);
inline bool operator!=(const CourseOverride& lhs, const CourseOverride& rhs) { return !(lhs == rhs); }


/**
 * The options of a course project, as the fields of the OCD record, in
 * their order. The letters which are known have named accessors.
 */
struct CourseOptions
{
	std::vector<CourseField> fields;
	
	bool has(QChar letter) const;
	/** The value of a field, or the fallback when the field is absent. */
	QString value(QChar letter, const QString& fallback = {}) const;
	void setValue(QChar letter, const QString& value);
	
	/** 'e' */
	QString eventTitle() const { return value(QLatin1Char('e')); }
	/** 's': the size of a box of the control descriptions, in mm; 6 when absent. */
	double boxSize() const;
	/** 'i': the rows after which the description block wraps; 0 when absent. */
	int maximumRows() const;
	/** 't': 0 classes, 1 course name and classes, 2 course name only. */
	int titleMode() const;
	/** 'h': 0 no thicker rule, 1 every third row, 2 every fourth. */
	int thickerRule() const;
	/** 'n': the distance from the circle to the number, in mm; 1 when absent. */
	double numberDistance() const;
	/** 'l': the distance from the circle to the connection line, in mm. */
	double lineDistance() const;
	/** 'p': a full stop after the number. */
	bool fullStopAfterNumber() const;
	/** 'b': white background under the descriptions. */
	bool whiteBackground() const;
};

bool operator==(const CourseOptions& lhs, const CourseOptions& rhs);
inline bool operator!=(const CourseOptions& lhs, const CourseOptions& rhs) { return !(lhs == rhs); }


/**
 * Everything which makes a map file a course project.
 */
struct CourseData
{
	std::vector<CourseObject> objects;
	std::vector<Course> courses;
	std::vector<CourseClass> classes;
	std::vector<CourseOverride> overrides;
	CourseOptions options;
	
	bool isEmpty() const;
	
	/** The course object with the code, or nullptr. */
	const CourseObject* findObject(const QString& code) const;
	/** The course with the name, or nullptr. */
	const Course* findCourse(const QString& name) const;
};

bool operator==(const CourseData& lhs, const CourseData& rhs);
inline bool operator!=(const CourseData& lhs, const CourseData& rhs) { return !(lhs == rhs); }


}  // namespace OpenOrienteering

#endif
