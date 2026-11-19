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


#ifndef FOOM_OCD_COURSE_RECORDS_H
#define FOOM_OCD_COURSE_RECORDS_H

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {

class Map;


/**
 * The course records, as they stand in OCD 2018 files: reading them
 * into the course model and writing them from it.
 *
 * A record is a line of fields which are separated by tabs. The first
 * field is a name, or the kind and code of a course object; every other
 * field begins with a letter which says what it is. A file may have the
 * fields in any order. The writers here use a
 * fixed order, and fields which are not understood are kept behind the
 * known ones.
 *
 * See the design notes, "OCD file mapping".
 */
namespace OcdCourse {

/** A symbol number of a description cell, as "12.401", times 1000; 0 when the text is none. */
int symbolNumberFromText(const QString& text);
QString symbolNumberToText(int number);

/**
 * Reads a course object: the object string of type 1 of a map object,
 * or the parameter string of type 31 of a text block.
 * Returns false when the record does not begin with a known kind.
 */
bool parseObject(const QString& record, CourseObject& object);
QString formatObject(const CourseObject& object);

/** Reads a course: the parameter string of type 2. Returns false when it has no name. */
bool parseCourse(const QString& record, Course& course);
QString formatCourse(const Course& course);

/** Reads a class: the parameter string of type 3. Returns false when it has no name. */
bool parseClass(const QString& record, CourseClass& course_class);
QString formatClass(const CourseClass& course_class);

/**
 * Reads the object string of type 2: the place of a control number
 * ("<course> d<code>") or a connection line ("<course> f<from> t<to>").
 */
bool parseOverride(const QString& record, CourseOverride& override);
QString formatOverride(const CourseOverride& override);

/** Reads the options: the parameter string of type 1031. */
CourseOptions parseOptions(const QString& record);
QString formatOptions(const CourseOptions& options);

/**
 * Collects the course data of a map which was imported from an OCD 2018
 * file: the object strings, which the importer keeps as tags of the
 * objects, and the parameter strings of the types 2, 3, 31 and 1031,
 * which it keeps in Map::foreignData().
 *
 * Records which cannot be read are named in problems.
 */
CourseData readFromImportedMap(const Map& map, QStringList* problems = nullptr);

}  // namespace OcdCourse


}  // namespace OpenOrienteering

#endif
