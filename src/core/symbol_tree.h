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

#ifndef FOOM_CORE_SYMBOL_TREE_H
#define FOOM_CORE_SYMBOL_TREE_H

#include <vector>

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * A group of the symbol tree: a name, the index of its parent group (-1 at the
 * top), whether it is shown expanded, and the numbers of its symbols. A symbol
 * may be in several groups.
 *
 * See the design notes, "Symbol tree".
 */
struct SymbolGroup
{
	QString name;
	int parent = -1;
	bool expanded = true;
	QStringList numbers;
};

inline bool operator==(const SymbolGroup& lhs, const SymbolGroup& rhs)
{
	return lhs.name == rhs.name && lhs.parent == rhs.parent && lhs.expanded == rhs.expanded && lhs.numbers == rhs.numbers;
}

inline bool operator!=(const SymbolGroup& lhs, const SymbolGroup& rhs)
{
	return !(lhs == rhs);
}

/// Adds a group under the parent (-1 at the top); returns its index.
int addSymbolGroup(Map& map, const QString& name, int parent = -1);

/// Removes the group and the groups below it; the parents of the others follow.
void removeSymbolGroup(Map& map, int group);

/// Renames the group.
void renameSymbolGroup(Map& map, int group, const QString& name);

/// Adds the symbols' numbers to the group, each once.
void addToSymbolGroup(Map& map, int group, const std::vector<const Symbol*>& symbols);

/// Removes the symbols' numbers from the group.
void removeFromSymbolGroup(Map& map, int group, const std::vector<const Symbol*>& symbols);

/// The numbers of the group's symbols, and with children those of the groups below it, each once.
QStringList symbolGroupNumbers(const Map& map, int group, bool with_children = true);


}  // namespace OpenOrienteering

#endif
