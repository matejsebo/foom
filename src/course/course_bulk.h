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


#ifndef FOOM_COURSE_BULK_H
#define FOOM_COURSE_BULK_H

#include <utility>
#include <vector>

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * The commands which change many courses at once, and the renumbering of
 * the controls. They work on the course data alone; the caller puts the
 * result into the map as an undo step.
 *
 * See the design notes, "Composing a course and the bulk
 * commands".
 */

/** Where in a course a course object is put in, or taken out. */
struct CourseCondition
{
	enum Relation
	{
		None,
		All,      ///< every item of the object; for taking out only
		After,    ///< behind an item of first
		Before,   ///< before an item of first
		Between   ///< between an item of first and an item of second which follows at once
	};
	
	Relation relation = None;
	QString first;
	QString second;
};


/** What a bulk command did. */
struct CourseBulkReport
{
	enum Error
	{
		NoError,
		NoObject,      ///< the course object is not there, or cannot be an item of a course
		NoCourses,     ///< no course is chosen, or a name is not that of a course
		NoCondition,   ///< the condition lacks what it needs
		InvalidOrder   ///< the condition names the object itself, or the same object twice
	};
	
	Error error = NoError;
	/** The courses which were changed, in the order in which they were given, with the number of items. */
	std::vector<std::pair<QString, int>> changed;
	/** The courses which were chosen and have no such place. */
	QStringList unchanged;
	
	bool ok() const { return error == NoError; }
	/** The number of items which were put in or taken out. */
	int items() const;
};

/**
 * Puts a course object into courses: behind every item of the condition's
 * first object, before every such item, or wherever an item of the first
 * is followed at once by an item of the second. The object is not put
 * beside an item of itself. When the command is refused, nothing is changed.
 */
CourseBulkReport insertIntoCourses(CourseData& data, const QString& code, const QStringList& courses, const CourseCondition& condition);

/**
 * Takes items of a course object out of courses: all of them, or those
 * which stand at the place which the condition names. The course object
 * stays. When the command is refused, nothing is changed.
 */
CourseBulkReport removeFromCourses(CourseData& data, const QString& code, const QStringList& courses, const CourseCondition& condition);


/**
 * Returns true for the items without a course object which the bulk
 * commands put in and take out: the mandatory crossing point, the
 * mandatory passage, the map exchange and the map flip.
 */
bool isCourseMarker(CourseItem::Type type);

/**
 * Puts a marker into courses, as insertIntoCourses() puts an object. A
 * marker is not put beside one of its kind; a map exchange and a map flip
 * count as one kind. Another type is refused as NoObject.
 */
CourseBulkReport insertMarkerIntoCourses(CourseData& data, CourseItem::Type type, const QStringList& courses, const CourseCondition& condition);

/**
 * Takes the markers of a type out of courses, as removeFromCourses()
 * takes the items of an object.
 */
CourseBulkReport removeMarkerFromCourses(CourseData& data, CourseItem::Type type, const QStringList& courses, const CourseCondition& condition);


/**
 * The lowest code of a control which is a number; 0 when there is none.
 * A code with a leading zero or with anything but digits is no number.
 */
int firstControlCode(const CourseData& data);

/** What renumberControls() did. */
struct CourseRenumberReport
{
	bool done = false;
	/** The number of controls which got another code. */
	int renumbered = 0;
	/** A new code which a course object has which is not renumbered. */
	QString clash;
};

/**
 * Gives new codes to the controls whose code is a number not below the
 * old first code: each is shifted by the new first code less the old one,
 * or, when gaps are closed, they get the new first code and the numbers
 * which follow, in the order of their numbers. Items and overrides follow.
 * When it is refused, nothing is changed.
 */
CourseRenumberReport renumberControls(CourseData& data, int old_first, int new_first, bool close_gaps);


}  // namespace OpenOrienteering

#endif
