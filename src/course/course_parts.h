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


#ifndef FOOM_COURSE_PARTS_H
#define FOOM_COURSE_PARTS_H

#include <vector>

#include <QString>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * The parts of a course: a map exchange or a map flip ends a part, and
 * the items behind it are the next one.
 *
 * See the design notes, "The parts of a course: map
 * exchange and flip".
 */
struct CoursePart
{
	enum End { CourseEnd, MapExchange, MapFlip };
	
	/** The index of its first item. */
	int first = 0;
	/** The index behind its last item: that of the exchange or flip which ends it, or the number of items. */
	int end = 0;
	End ends_with = CourseEnd;
};

/** The parts of a course in their order; one part for a course without a map exchange or flip. */
std::vector<CoursePart> courseParts(const Course& course);

/**
 * The part of an item, counted from 0. An exchange or a flip is of the
 * part which it ends. -1 for an index which is none.
 */
int coursePartOfItem(const Course& course, int index);

/**
 * What is wrong with the parts of a course, in words: a part without a
 * control, or a part behind the first which does not begin with a start.
 * The first which is found; empty when there is nothing.
 */
QString coursePartFault(const Course& course);

/**
 * Puts an item which has no course object into a course, so that it has
 * the given index; the index may be the number of items. The types are
 * the mandatory crossing point, the mandatory passage, the map exchange
 * and the map flip.
 */
bool insertCourseMarker(CourseData& data, const QString& course, int index, CourseItem::Type type);


}  // namespace OpenOrienteering

#endif
