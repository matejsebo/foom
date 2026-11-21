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

#include "symbol_descriptions.h"

#include <vector>

#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>

#include "core/map.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

constexpr QChar separator = QLatin1Char(';');
constexpr QChar quote = QLatin1Char('"');
constexpr QChar newline = QLatin1Char('\n');

QString field(const QString& value)
{
	if (!value.contains(separator) && !value.contains(quote) && !value.contains(newline) && !value.contains(QLatin1Char('\r')))
		return value;
	auto doubled = value;
	doubled.replace(quote, QStringLiteral("\"\""));
	return quote + doubled + quote;
}

/// The rows of the CSV text: fields split at separators outside quotes, quotes undone.
std::vector<QStringList> rows(const QString& text)
{
	std::vector<QStringList> out;
	QStringList row;
	QString current;
	auto quoted = false;
	auto any = false;
	for (int i = 0; i < text.size(); ++i)
	{
		auto const c = text.at(i);
		if (quoted)
		{
			if (c == quote)
			{
				if (i + 1 < text.size() && text.at(i + 1) == quote)
				{
					current += quote;
					++i;
				}
				else
				{
					quoted = false;
				}
			}
			else if (!(c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == newline))
			{
				// Windows line ends inside a field read as line breaks.
				current += c;
			}
			continue;
		}
		if (c == quote)
		{
			quoted = true;
			any = true;
		}
		else if (c == separator)
		{
			row << current;
			current.clear();
			any = true;
		}
		else if (c == newline)
		{
			if (any || !current.isEmpty())
			{
				row << current;
				out.push_back(row);
			}
			row.clear();
			current.clear();
			any = false;
		}
		else if (c != QLatin1Char('\r'))
		{
			current += c;
		}
	}
	if (any || !current.isEmpty())
	{
		row << current;
		out.push_back(row);
	}
	return out;
}

}  // namespace



QString symbolDescriptionsCsv(const Map& map)
{
	// the design notes, "Descriptions file".
	QString text = QStringLiteral("number;name;description\n");
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		text += field(symbol->getNumberAsString()) + separator + field(symbol->getName()) + separator
		        + field(symbol->getDescription()) + newline;
	}
	return text;
}


DescriptionImport applySymbolDescriptionsCsv(Map& map, const QString& csv)
{
	DescriptionImport result;
	auto const all = rows(csv);
	for (std::size_t r = 0; r < all.size(); ++r)
	{
		auto const& row = all[r];
		if (r == 0 && !row.isEmpty() && row.front() == QLatin1String("number"))
			continue;
		if (row.size() < 3)
		{
			++result.malformed;
			continue;
		}
		auto matched = false;
		for (int i = 0; i < map.getNumPublicSymbols(); ++i)
		{
			auto* symbol = map.getSymbol(i);
			if (symbol->getNumberAsString() != row.at(0))
				continue;
			matched = true;
			if (symbol->getName() == row.at(1) && symbol->getDescription() == row.at(2))
				continue;
			symbol->setName(row.at(1));
			symbol->setDescription(row.at(2));
			emit map.symbolChanged(i, symbol, symbol);
			++result.changed;
		}
		if (!matched)
			result.unmatched << row.at(0);
	}
	if (result.changed > 0)
		map.setSymbolsDirty();
	return result;
}


}  // namespace OpenOrienteering
