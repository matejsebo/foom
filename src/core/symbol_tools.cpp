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

#include "symbol_tools.h"

#include <algorithm>

#include <QRegularExpression>

#include "core/map.h"
#include "core/map_color.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"


namespace OpenOrienteering {

namespace {

template <class Test>
std::vector<const Symbol*> collect(const Map& map, Test test)
{
	std::vector<const Symbol*> found;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (test(symbol))
			found.push_back(symbol);
	}
	return found;
}

bool inScope(const std::vector<const Symbol*>& scope, const Symbol* symbol)
{
	return scope.empty() || std::find(scope.begin(), scope.end(), symbol) != scope.end();
}

/// Redraws what the changed symbols draw and marks the symbols changed.
void changed(Map& map, const std::vector<int>& indices)
{
	if (indices.empty())
		return;
	for (auto const i : indices)
	{
		auto* symbol = map.getSymbol(i);
		symbol->resetIcon();
		emit map.symbolChanged(i, symbol, symbol);
	}
	map.updateAllObjects();
	map.setSymbolsDirty();
}

}  // namespace



std::vector<const Symbol*> symbolsUsingColor(const Map& map, const MapColor* color)
{
	return collect(map, [color](const Symbol* symbol) { return color && symbol->containsColor(color); });
}


std::vector<const Symbol*> symbolsUsingFont(const Map& map, const QString& family)
{
	return collect(map, [&family](const Symbol* symbol) {
		return symbol->getType() == Symbol::Text && symbol->asText()->getFontFamily() == family;
	});
}


std::vector<const Symbol*> symbolsOfType(const Map& map, int type)
{
	return collect(map, [type](const Symbol* symbol) { return symbol->getType() == type; });
}


std::vector<const Symbol*> symbolsWithStatus(const Map& map, SymbolStatus status)
{
	return collect(map, [status](const Symbol* symbol) {
		switch (status)
		{
		case SymbolStatus::Hidden:    return symbol->isHidden();
		case SymbolStatus::Protected: return symbol->isProtected();
		case SymbolStatus::Normal:    return !symbol->isHidden() && !symbol->isProtected();
		}
		return false;
	});
}


std::vector<const Symbol*> symbolsMatchingNumber(const Map& map, const QString& pattern)
{
	QRegularExpression const expression(QRegularExpression::anchoredPattern(QRegularExpression::wildcardToRegularExpression(pattern)));
	return collect(map, [&expression](const Symbol* symbol) { return expression.match(symbol->getNumberAsString()).hasMatch(); });
}


QStringList fontFamilies(const Map& map)
{
	QStringList families;
	for (auto const* symbol : symbolsOfType(map, Symbol::Text))
	{
		auto const& family = symbol->asText()->getFontFamily();
		if (!families.contains(family))
			families.push_back(family);
	}
	families.sort();
	return families;
}


int replaceFont(Map& map, const QString& from, const QString& to, const std::vector<const Symbol*>& scope)
{
	std::vector<int> indices;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Text && inScope(scope, symbol) && symbol->asText()->getFontFamily() == from)
		{
			symbol->asText()->setFontFamily(to);
			indices.push_back(i);
		}
	}
	changed(map, indices);
	return int(indices.size());
}


int replaceColor(Map& map, const MapColor* from, const MapColor* to, const std::vector<const Symbol*>& scope)
{
	if (!from || !to || from == to)
		return 0;
	MapColorMap colors;
	colors[from] = to;
	std::vector<int> indices;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (inScope(scope, symbol) && symbol->containsColor(from))
		{
			symbol->replaceColors(colors);
			indices.push_back(i);
		}
	}
	changed(map, indices);
	return int(indices.size());
}


int renumberSymbols(Map& map, int start, int step, const std::vector<const Symbol*>& scope)
{
	std::vector<int> indices;
	auto number = start;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (!inScope(scope, symbol))
			continue;
		symbol->setNumberComponent(0, number);
		symbol->setNumberComponent(1, -1);
		symbol->setNumberComponent(2, -1);
		number += step;
		indices.push_back(i);
	}
	changed(map, indices);
	return int(indices.size());
}


}  // namespace OpenOrienteering
