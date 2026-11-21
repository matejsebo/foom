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

#include "symbol_update.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <QHash>
#include <QLatin1Char>
#include <QString>

#include "core/map.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

QString label(const Symbol* symbol)
{
	return symbol->getNumberAsString() + QLatin1Char(' ') + symbol->getPlainTextName();
}

int findByNumber(const Map& map, const QString& number)
{
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		if (map.getSymbol(i)->getNumberAsString() == number)
			return i;
	}
	return -1;
}

}  // namespace



SymbolUpdateReport updateSymbolsFrom(Map& map, const Map& reference, double scale_factor)
{
	// the design notes, "Update from file".
	SymbolUpdateReport report;
	std::vector<bool> filter(std::size_t(reference.getNumSymbols()), false);
	std::vector<const Symbol*> to_replace;   // reference symbols which replace one of the map
	std::vector<const Symbol*> to_add;
	for (int i = 0; i < reference.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = reference.getSymbol(i);
		auto const index = findByNumber(map, symbol->getNumberAsString());
		if (index < 0)
		{
			filter[std::size_t(i)] = true;
			to_add.push_back(symbol);
			report.added << label(symbol);
		}
		else if (std::abs(scale_factor - 1.0) > 1e-9 || !map.getSymbol(index)->equals(symbol))
		{
			filter[std::size_t(i)] = true;
			to_replace.push_back(symbol);
			report.changed << label(map.getSymbol(index));
		}
		else
		{
			report.unchanged << label(symbol);
		}
	}
	if (to_add.empty() && to_replace.empty())
		return report;
	
	// The symbols come with the colours they need, appended after the map's.
	auto const imported = map.importMap(reference, Map::MinimalSymbolImport, &filter, -1, false);
	for (auto* symbol : imported)
	{
		if (std::abs(scale_factor - 1.0) > 1e-9)
			symbol->scale(scale_factor);
	}
	
	// Replacements take the place, the objects and the status of the map's symbol.
	std::vector<int> appended;
	for (auto const* reference_symbol : to_replace)
	{
		auto* imported_symbol = imported.value(reference_symbol);
		if (!imported_symbol)
			continue;
		auto const index = findByNumber(map, reference_symbol->getNumberAsString());
		auto* replacement = Symbol::duplicate<Symbol>(*imported_symbol).release();
		replacement->setHidden(map.getSymbol(index)->isHidden());
		replacement->setProtected(map.getSymbol(index)->isProtected());
		map.setSymbol(replacement, index);
		appended.push_back(map.findSymbolIndex(imported_symbol));
	}
	std::sort(appended.begin(), appended.end());
	for (auto it = appended.rbegin(); it != appended.rend(); ++it)
	{
		if (*it >= 0)
			map.deleteSymbol(*it);
	}
	map.updateAllObjects();
	map.setSymbolsDirty();
	return report;
}


}  // namespace OpenOrienteering
