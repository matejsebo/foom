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

#ifndef FOOM_CORE_SYMBOL_COMPARE_H
#define FOOM_CORE_SYMBOL_COMPARE_H

#include <vector>

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;


/**
 * Options of compareSymbolSets().
 *
 * See the design notes, "Compare with file".
 */
struct SymbolCompareOptions
{
	bool used_only = false;       ///< Only the symbols used in this map.
	bool list_identical = false;  ///< List the identical symbols too.
	bool descriptions = true;     ///< A different description makes symbols differ.
	bool colours = true;          ///< Compare the colours and their order.
};

/**
 * The differences of this map's symbols and colours from a reference map's.
 */
struct SymbolComparison
{
	struct Change
	{
		QString symbol;           ///< Number and name in this map.
		QStringList differences;  ///< type, name, description, definition
	};
	
	QStringList only_here;        ///< Number and name.
	QStringList only_there;
	QStringList identical;
	std::vector<Change> changed;
	QStringList colours_only_here;
	QStringList colours_only_there;
	QStringList colours_changed;
	bool colour_order_differs = false;
};

/// Compares the public symbols matched by number, and the colours matched by name.
SymbolComparison compareSymbolSets(const Map& map, const Map& reference, const SymbolCompareOptions& options);

/// A text report of the comparison.
QString symbolComparisonText(const SymbolComparison& comparison);


}  // namespace OpenOrienteering

#endif
