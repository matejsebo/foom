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

#ifndef FOOM_CORE_SAVED_SELECTIONS_H
#define FOOM_CORE_SAVED_SELECTIONS_H

#include <vector>

#include <QString>
#include <QUuid>

namespace OpenOrienteering {

/**
 * A named selection: the uuids of its objects.
 *
 * See the design notes, "Saved selections".
 */
struct SavedSelection
{
	QString name;
	std::vector<QUuid> uuids;
};

inline bool operator==(const SavedSelection& lhs, const SavedSelection& rhs)
{
	return lhs.name == rhs.name && lhs.uuids == rhs.uuids;
}

inline bool operator!=(const SavedSelection& lhs, const SavedSelection& rhs)
{
	return !(lhs == rhs);
}


}  // namespace OpenOrienteering

#endif
