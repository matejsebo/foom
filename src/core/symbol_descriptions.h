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

#ifndef FOOM_CORE_SYMBOL_DESCRIPTIONS_H
#define FOOM_CORE_SYMBOL_DESCRIPTIONS_H

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;


/**
 * The symbols' names and descriptions as CSV, keyed by number: a header
 * "number;name;description", then one row per public symbol.
 *
 * See the design notes, "Descriptions file".
 */
QString symbolDescriptionsCsv(const Map& map);

/**
 * What applySymbolDescriptionsCsv() did.
 */
struct DescriptionImport
{
	int changed = 0;              ///< Symbols whose name or description changed.
	QStringList unmatched;        ///< Numbers no symbol has.
	int malformed = 0;            ///< Rows without three fields.
};

/// Sets the names and descriptions of the symbols with the numbers of the rows.
DescriptionImport applySymbolDescriptionsCsv(Map& map, const QString& csv);


}  // namespace OpenOrienteering

#endif
