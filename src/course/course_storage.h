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


#ifndef FOOM_COURSE_STORAGE_H
#define FOOM_COURSE_STORAGE_H

#include <QByteArray>
#include <QStringList>

#include "course/course_data.h"

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

class Map;


/**
 * Writes the element course_data with the course data.
 *
 * See the design notes, "Storage".
 */
void saveCourseData(const CourseData& data, QXmlStreamWriter& xml);

/**
 * Reads the element course_data, on whose start the reader stands.
 *
 * Elements and attributes which are not known are passed over. An
 * element whose kind or type is not known is left out and named in
 * problems.
 */
CourseData loadCourseData(QXmlStreamReader& xml, QStringList* problems = nullptr);

/** The element as a document of its own, for the undo step. */
QByteArray courseDataToXml(const CourseData& data);
CourseData courseDataFromXml(const QByteArray& xml);

/**
 * Replaces the course data of the map as one undo step, and marks the
 * map as changed. Does nothing when the data are those the map has.
 */
void changeCourseData(Map& map, CourseData data);


}  // namespace OpenOrienteering

#endif
