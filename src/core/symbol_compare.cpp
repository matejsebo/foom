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

#include "symbol_compare.h"

#include <memory>

#include <QLatin1String>

#include "core/map.h"
#include "core/map_color.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

QString label(const Symbol* symbol)
{
	return symbol->getNumberAsString() + QLatin1Char(' ') + symbol->getPlainTextName();
}

const Symbol* findByNumber(const Map& map, const QString& number)
{
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		if (map.getSymbol(i)->getNumberAsString() == number)
			return map.getSymbol(i);
	}
	return nullptr;
}

const MapColor* findColor(const Map& map, const QString& name)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == name)
			return map.getColor(i);
	}
	return nullptr;
}

QStringList differences(const Symbol* mine, const Symbol* theirs, bool descriptions)
{
	QStringList out;
	if (mine->getType() != theirs->getType())
		return { QStringLiteral("type") };
	if (mine->getName() != theirs->getName())
		out << QStringLiteral("name");
	if (descriptions && mine->getDescription() != theirs->getDescription())
		out << QStringLiteral("description");
	// The definition: the reference's symbol with this map's name and description.
	auto copy = Symbol::duplicate<Symbol>(*theirs);
	copy->setName(mine->getName());
	copy->setDescription(mine->getDescription());
	if (!mine->equals(copy.get()))
		out << QStringLiteral("definition");
	return out;
}

}  // namespace



SymbolComparison compareSymbolSets(const Map& map, const Map& reference, const SymbolCompareOptions& options)
{
	// the design notes, "Compare with file".
	SymbolComparison result;
	std::vector<bool> in_use;
	if (options.used_only)
		map.determineSymbolsInUse(in_use);
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (options.used_only && !(std::size_t(i) < in_use.size() && in_use[std::size_t(i)]))
			continue;
		auto const* other = findByNumber(reference, symbol->getNumberAsString());
		if (!other)
		{
			result.only_here << label(symbol);
			continue;
		}
		auto const diff = differences(symbol, other, options.descriptions);
		if (diff.isEmpty())
			result.identical << label(symbol);
		else
			result.changed.push_back({ label(symbol), diff });
	}
	for (int i = 0; i < reference.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = reference.getSymbol(i);
		if (!findByNumber(map, symbol->getNumberAsString()))
			result.only_there << label(symbol);
	}
	if (!options.list_identical)
		result.identical.clear();
	
	if (options.colours)
	{
		QStringList common_here, common_there;
		for (int i = 0; i < map.getNumColors(); ++i)
		{
			auto const* color = map.getColor(i);
			auto const* other = findColor(reference, color->getName());
			if (!other)
			{
				result.colours_only_here << color->getName();
				continue;
			}
			common_here << color->getName();
			if (!color->equals(*other))
				result.colours_changed << color->getName();
		}
		for (int i = 0; i < reference.getNumColors(); ++i)
		{
			auto const* color = reference.getColor(i);
			if (findColor(map, color->getName()))
				common_there << color->getName();
			else
				result.colours_only_there << color->getName();
		}
		result.colour_order_differs = common_here != common_there;
	}
	return result;
}


QString symbolComparisonText(const SymbolComparison& comparison)
{
	QString text;
	auto const section = [&text](const char* title, const QStringList& lines) {
		if (lines.isEmpty())
			return;
		text += QLatin1String(title) + QLatin1Char('\n');
		for (auto const& line : lines)
			text += QStringLiteral("  ") + line + QLatin1Char('\n');
	};
	section("Symbols only in this map:", comparison.only_here);
	section("Symbols only in the other map:", comparison.only_there);
	QStringList changed;
	for (auto const& change : comparison.changed)
		changed << change.symbol + QStringLiteral(": ") + change.differences.join(QStringLiteral(", "));
	section("Symbols which differ:", changed);
	section("Identical symbols:", comparison.identical);
	section("Colours only in this map:", comparison.colours_only_here);
	section("Colours only in the other map:", comparison.colours_only_there);
	section("Colours which differ:", comparison.colours_changed);
	if (comparison.colour_order_differs)
		text += QStringLiteral("The colours common to both maps are in another order.\n");
	if (text.isEmpty())
		text = QStringLiteral("No differences.\n");
	return text;
}


}  // namespace OpenOrienteering
