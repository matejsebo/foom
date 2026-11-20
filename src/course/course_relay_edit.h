/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef FOOM_COURSE_RELAY_EDIT_H
#define FOOM_COURSE_RELAY_EDIT_H

#include <vector>

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * The changes which make a course a relay and give it its variations.
 * They work on the data alone; the caller puts the result into the map
 * as an undo step. What is refused changes nothing and says its reason.
 *
 * See the design notes, "Editing relays: type, legs and
 * variations in the dock".
 */

/** The largest number of legs: a branch has a letter in the code of a variant. */
constexpr int max_relay_legs = 26;

/**
 * The rule for a change of a course: the fault which the change gives a
 * course which had none, in words; empty when the course has no fault
 * after the change, and when it had one before, so that it can be mended.
 */
QString relayFaultOfChange(const CourseData& before, const CourseData& after, const QString& course);

/**
 * The legs which are run at a place of a relay, before the item with the
 * index: those of the course, or of the branch of a leg variation in which
 * the place is. None for a course which is no relay.
 */
std::vector<int> relayLegsAt(const Course& course, int index);

/**
 * Makes a course a normal course, a relay or a one-man relay of so many
 * legs; a normal course has no legs. Refused by the rule, for a number of
 * legs which is not 2 to max_relay_legs, and for a relay with more than
 * one class.
 */
bool setCourseType(CourseData& data, const QString& course, Course::Type type, int legs, QString* why = nullptr);

/**
 * Puts a team variation with empty branches into a course, so that its
 * begin has the given index; the index may be the number of items.
 * Refused when the course has a fault with it.
 */
bool insertTeamVariation(CourseData& data, const QString& course, int index, int branches, QString* why = nullptr);

/**
 * Puts a leg variation with empty branches into a course. For every
 * branch a text names its legs, with dashes between them ("1-2").
 * Refused when the course has a fault with it.
 */
bool insertLegVariation(CourseData& data, const QString& course, int index, const QStringList& branch_legs, QString* why = nullptr);

/**
 * The variation of a marker, which is its begin, one of its branch
 * markers or its end: the indexes of its begin and its end. A marker
 * whose variation is not whole stands for itself. False for an item
 * which is no marker.
 */
bool variationRange(const Course& course, int index, int& first, int& last);

/**
 * Removes the variation of a marker with all that is in it, or the marker
 * alone when its variation is not whole. False for an item which is no marker.
 */
bool removeVariation(CourseData& data, const QString& course, int index);


}  // namespace OpenOrienteering

#endif
