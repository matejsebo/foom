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

#include "status_sets.h"

#include "core/map.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

StatusSet currentStatusSet(const Map& map, const QString& name)
{
	// the design notes, "Status sets".
	StatusSet set;
	set.name = name;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		auto const number = symbol->getNumberAsString();
		if (symbol->isHidden() && !set.hidden.contains(number))
			set.hidden.push_back(number);
		if (symbol->isProtected() && !set.protected_symbols.contains(number))
			set.protected_symbols.push_back(number);
	}
	return set;
}


int applyStatusSet(Map& map, const StatusSet& set)
{
	int changed = 0;
	auto selection_changed = false;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		auto const number = symbol->getNumberAsString();
		auto const hidden = set.hidden.contains(number);
		auto const is_protected = set.protected_symbols.contains(number);
		if (symbol->isHidden() == hidden && symbol->isProtected() == is_protected)
			continue;
		symbol->setHidden(hidden);
		symbol->setProtected(is_protected);
		if (hidden || is_protected)
			selection_changed |= map.removeSymbolFromSelection(symbol, false);
		emit map.symbolChanged(i, symbol, symbol);
		++changed;
	}
	if (selection_changed)
		map.emitSelectionChanged();
	if (changed > 0)
	{
		map.updateAllMapWidgets();
		map.setSymbolsDirty();
	}
	return changed;
}


}  // namespace OpenOrienteering
