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


#ifndef FOOM_COURSE_PUNCH_H
#define FOOM_COURSE_PUNCH_H

#include <cstddef>
#include <vector>

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * The punching unit ids of the controls: reading them from a text, the
 * numbers which are offered, and the default ids.
 *
 * See the design notes, "Punching unit ids".
 */

/** The punching systems of the dialog. */
enum class PunchSystem { SportIdent, Emit, Other };

/** How default ids are added to the controls. */
enum class PunchDefaults
{
	OnlyWithout,  ///< to the controls which have no id
	Replace,      ///< the default becomes the only id of every control
	AddKeep       ///< added to the ids which a control has
};


/**
 * Reads ids from a text, separated by semicolons, commas or blanks. An id
 * is a whole number of digits, whose leading zeros are dropped. The ids
 * are sorted by number, without repeats. Returns false, and leaves ids as
 * they are, when the text holds anything else.
 */
bool parsePunchIds(const QString& text, QStringList* ids);

/** The ids joined by semicolons, as the format has them. */
QString joinPunchIds(const QStringList& ids);

/**
 * The ids with one more, sorted by number and without repeats. Ids which
 * are no numbers go behind the numbers, in the order of their text.
 */
QStringList addPunchId(const QStringList& ids, const QString& id);

/** The numbers which the dialog offers. */
std::vector<int> offeredPunchIds();

/**
 * The default id of a control in a punching system: its code when the
 * code is a number, else none.
 */
QString defaultPunchId(const CourseObject& control, PunchSystem system);

/** The ids of a control after a default was added to them. */
QStringList withDefaultPunchId(const CourseObject& control, const QStringList& ids, PunchDefaults mode, PunchSystem system);

/**
 * Adds the default ids to the controls of the data. Returns the number of
 * controls whose ids changed. Other course objects are left as they are.
 */
int addDefaultPunchIds(CourseData& data, PunchDefaults mode, PunchSystem system);

/**
 * The indexes of the controls among the course objects, ordered by the
 * number with which their code begins (none counting 0), then by the code.
 */
std::vector<std::size_t> punchControlOrder(const CourseData& data);


}  // namespace OpenOrienteering

#endif
