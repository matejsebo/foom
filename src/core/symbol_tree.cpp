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

#include "symbol_tree.h"

#include <cstddef>

#include "core/map.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

bool valid(const Map& map, int group)
{
	return group >= 0 && group < int(map.symbolTree().size());
}

/// Whether the group lies below the ancestor (or is it).
bool below(const std::vector<SymbolGroup>& groups, int group, int ancestor)
{
	for (int g = group, steps = 0; g >= 0 && steps <= int(groups.size()); g = groups[std::size_t(g)].parent, ++steps)
	{
		if (g == ancestor)
			return true;
	}
	return false;
}

}  // namespace



int addSymbolGroup(Map& map, const QString& name, int parent)
{
	// the design notes, "Symbol tree".
	auto groups = map.symbolTree();
	SymbolGroup group;
	group.name = name;
	group.parent = valid(map, parent) ? parent : -1;
	groups.push_back(group);
	map.setSymbolTree(groups);
	map.setSymbolsDirty();
	return int(groups.size()) - 1;
}


void removeSymbolGroup(Map& map, int group)
{
	if (!valid(map, group))
		return;
	auto const& groups = map.symbolTree();
	std::vector<int> new_index(groups.size(), -1);
	std::vector<SymbolGroup> kept;
	for (std::size_t g = 0; g < groups.size(); ++g)
	{
		if (below(groups, int(g), group))
			continue;
		new_index[g] = int(kept.size());
		kept.push_back(groups[g]);
	}
	for (auto& g : kept)
		g.parent = g.parent >= 0 ? new_index[std::size_t(g.parent)] : -1;
	map.setSymbolTree(kept);
	map.setSymbolsDirty();
}


void renameSymbolGroup(Map& map, int group, const QString& name)
{
	if (!valid(map, group))
		return;
	auto groups = map.symbolTree();
	groups[std::size_t(group)].name = name;
	map.setSymbolTree(groups);
	map.setSymbolsDirty();
}


void addToSymbolGroup(Map& map, int group, const std::vector<const Symbol*>& symbols)
{
	if (!valid(map, group))
		return;
	auto groups = map.symbolTree();
	auto& numbers = groups[std::size_t(group)].numbers;
	for (auto const* symbol : symbols)
	{
		auto const number = symbol->getNumberAsString();
		if (!numbers.contains(number))
			numbers.push_back(number);
	}
	map.setSymbolTree(groups);
	map.setSymbolsDirty();
}


void removeFromSymbolGroup(Map& map, int group, const std::vector<const Symbol*>& symbols)
{
	if (!valid(map, group))
		return;
	auto groups = map.symbolTree();
	for (auto const* symbol : symbols)
		groups[std::size_t(group)].numbers.removeAll(symbol->getNumberAsString());
	map.setSymbolTree(groups);
	map.setSymbolsDirty();
}


QStringList symbolGroupNumbers(const Map& map, int group, bool with_children)
{
	QStringList numbers;
	if (!valid(map, group))
		return numbers;
	auto const& groups = map.symbolTree();
	for (std::size_t g = 0; g < groups.size(); ++g)
	{
		if (int(g) != group && !(with_children && below(groups, int(g), group)))
			continue;
		for (auto const& number : groups[g].numbers)
		{
			if (!numbers.contains(number))
				numbers.push_back(number);
		}
	}
	return numbers;
}


}  // namespace OpenOrienteering
