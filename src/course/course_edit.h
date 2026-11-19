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


#ifndef FOOM_COURSE_EDIT_H
#define FOOM_COURSE_EDIT_H

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * The changes of course data which the course dock makes. They work on
 * the data alone; the caller puts the result into the map as an undo step.
 *
 * See the design notes, "Course dock".
 */

/**
 * The code which a new course object of the kind gets: the letter of the
 * kind and the highest number in use with that letter plus one. A control
 * has no letter and begins with 31. A gap is not filled.
 */
QString nextCourseCode(const CourseData& data, CourseObject::Kind kind);

/** A code is not empty and has no white space. */
bool isValidCourseCode(const QString& code);

/** The names of the courses which have an item naming the code, in their order. */
QStringList coursesUsing(const CourseData& data, const QString& code);

/**
 * Gives a course object another code, in its items and overrides too.
 * Returns false, and changes nothing, when there is no such object, or
 * the new code is not valid or is that of another object.
 */
bool renameCourseObject(CourseData& data, const QString& old_code, const QString& new_code);

/**
 * Removes a course object, the items which name it and the overrides
 * which name it. Returns the names of the courses which lost items.
 */
QStringList removeCourseObject(CourseData& data, const QString& code);

/** A name of a new course: not blank, without a tab, and not the name of a course. */
bool isValidCourseName(const CourseData& data, const QString& name);

/** A name of a new class: not blank, without a tab, and not the name of a class. */
bool isValidClassName(const CourseData& data, const QString& name);

/** Adds a normal course without items behind the others. */
bool addCourse(CourseData& data, const QString& name);

/** Adds a copy of a course behind the others. It has no classes and no overrides. */
bool duplicateCourse(CourseData& data, const QString& original, const QString& name);

/** Renames a course; its classes and overrides follow. */
bool renameCourse(CourseData& data, const QString& old_name, const QString& new_name);

/** Removes a course with its classes and overrides. */
bool removeCourse(CourseData& data, const QString& name);

/**
 * Returns true for the kinds which can stand in a course: start, control,
 * marked route, finish and text block; and gives the type of their item.
 */
bool canBeCourseItem(CourseObject::Kind kind, CourseItem::Type* type = nullptr);

/** Returns true for the items which begin, divide and end a variation. */
bool isVariationItem(CourseItem::Type type);

/**
 * Puts the course object with the code into the course, so that its item
 * has the given index; the index may be the number of items.
 */
bool insertCourseItem(CourseData& data, const QString& course, int index, const QString& code);

/** Removes the item with the index. An item of a variation is not removed. */
bool removeCourseItem(CourseData& data, const QString& course, int index);

/** Moves an item so that it has the index to. An item of a variation is not moved. */
bool moveCourseItem(CourseData& data, const QString& course, int from, int to);


}  // namespace OpenOrienteering

#endif
