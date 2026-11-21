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

#ifndef FOOM_CORE_STATUS_SETS_H
#define FOOM_CORE_STATUS_SETS_H

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;


/**
 * A named status of the symbols: the numbers of the hidden and of the
 * protected symbols; every other symbol is normal.
 *
 * See the design notes, "Status sets".
 */
struct StatusSet
{
	QString name;
	QStringList hidden;           ///< Symbol numbers as text, 101.1.
	QStringList protected_symbols;
};

inline bool operator==(const StatusSet& lhs, const StatusSet& rhs)
{
	return lhs.name == rhs.name && lhs.hidden == rhs.hidden && lhs.protected_symbols == rhs.protected_symbols;
}

inline bool operator!=(const StatusSet& lhs, const StatusSet& rhs)
{
	return !(lhs == rhs);
}

/// The status of the map's public symbols now, under the name.
StatusSet currentStatusSet(const Map& map, const QString& name);

/// Gives the map's public symbols the set's status; returns the number of symbols changed.
int applyStatusSet(Map& map, const StatusSet& set);


}  // namespace OpenOrienteering

#endif
