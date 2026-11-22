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

#ifndef FOOM_CORE_DXF_CRT_H
#define FOOM_CORE_DXF_CRT_H

#include <vector>

#include <QByteArray>
#include <QString>

namespace OpenOrienteering {


/**
 * One row of a DXF cross-reference table: a symbol's layer and colour.
 *
 * See the design notes, "DXF export through a cross-reference table".
 */
struct DxfCrtRow
{
	QString symbol;  ///< The symbol number as written, e.g. "502.1".
	QString layer;
	int colour = 0;  ///< AutoCAD colour index 1..255, or 0 for the symbol's own colour.
};

struct DxfCrt
{
	std::vector<DxfCrtRow> rows;  ///< In the file's order, one per symbol number.
	int skipped = 0;              ///< Lines which were neither empty, comments nor rows.

	/** Returns the row of the symbol number, or nullptr. Trailing ".0" parts do not count. */
	const DxfCrtRow* find(const QString& symbol_number) const;
};

/** The symbol number without trailing ".0" parts: "502.0" gives "502". */
QString normalisedSymbolNumber(const QString& number);

/** Reads a table from UTF-8 text. Sets message when lines were skipped or no row was read. */
DxfCrt readDxfCrt(const QByteArray& data, QString* message = nullptr);

/** Reads a table from a file. Sets message also when the file cannot be read. */
DxfCrt readDxfCrtFile(const QString& path, QString* message = nullptr);

/** Writes the table's rows. Returns false with a message. */
bool writeDxfCrt(const QString& path, const DxfCrt& crt, QString* error = nullptr);


}  // namespace OpenOrienteering

#endif
