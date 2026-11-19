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


#ifndef FOOM_COURSE_SYNC_H
#define FOOM_COURSE_SYNC_H

#include <memory>

#include <QStringList>

#include "core/symbols/symbol.h"
#include "course/course_data.h"

namespace OpenOrienteering {

class Map;
class UndoStep;


/** What syncCourseObjects() did. */
struct CourseSyncReport
{
	/** The codes of the course objects which were added. */
	QStringList added;
	/** The codes of the course objects which were removed. */
	QStringList removed;
	/** Those of the removed ones which courses used. */
	QStringList removed_from_courses;
	/** The names of the courses which lost items. */
	QStringList courses;
};


/**
 * The kind of course object which objects of a symbol with the role are.
 * Returns false for the roles which make no course object.
 */
bool courseKindOfRole(Symbol::CourseRole role, CourseObject::Kind* kind = nullptr);

/**
 * Brings the course objects in step with the objects of the map.
 *
 * An object whose symbol has the role of a course object, and which has
 * none, gets one with the next code. A course object whose map object is
 * not in the map, or whose symbol has the role of another kind, is removed
 * with the items and overrides which name it; so is one whose symbol has
 * no role, when the map has a symbol with the role of its kind. The part
 * of the overrides is left aside, and so are the text blocks, which have
 * no map object.
 *
 * Returns true when the data were changed. A map without such symbols and
 * without course objects is not looked through.
 *
 * See the design notes, "Course dock".
 */
bool syncCourseObjects(const Map& map, CourseData& data, CourseSyncReport* report = nullptr);

/**
 * Applies syncCourseObjects() to the map for an editing step which is
 * about to be pushed. When the course data change, the step which is
 * returned takes back the edit and that change together; otherwise it is
 * the step which was given.
 */
std::unique_ptr<UndoStep> courseSyncedStep(Map& map, std::unique_ptr<UndoStep> step);


}  // namespace OpenOrienteering

#endif
