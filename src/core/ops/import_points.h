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


#ifndef OPENORIENTEERING_OPS_IMPORT_POINTS_H
#define OPENORIENTEERING_OPS_IMPORT_POINTS_H

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QStringList>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Symbol;

namespace ops {

/**
 * Returns the delimiter of delimited text.
 *
 * This is the first of tab, semicolon and comma which occurs the same
 * nonzero number of times, outside double quotes, on each of the first five
 * non-empty lines; otherwise a blank, meaning runs of blanks.
 *
 * See the design notes, "Import options", Delimited text.
 */
QChar detectDelimiter(const QStringList& lines);

/**
 * Splits a line at the delimiter, honouring double quotes, and trims the fields.
 *
 * A blank delimiter splits at runs of blanks.
 */
QStringList splitDelimited(const QString& line, QChar delimiter);

/**
 * Returns true when the first non-empty line looks like a header:
 * it has text where the second has a number in the x or y column.
 */
bool detectHeader(const QStringList& lines, QChar delimiter, int x_column, int y_column);


/**
 * The parameters of importPoints().
 */
struct ImportPointsParams : public OpParams
{
	enum Crs
	{
		MapCrs,            ///< The map's projected coordinates.
		Wgs84,             ///< x longitude, y latitude, decimal degrees.
		OtherCrs,          ///< crs_spec: an EPSG code or a PROJ specification.
		PaperMillimetres,  ///< Map millimetres, y up.
	};
	
	QChar delimiter = QLatin1Char(',');
	bool header = false;
	int x_column = 0;
	int y_column = 1;
	int z_column = -1;     ///< -1: none
	int text_column = -1;  ///< -1: none
	int code_column = -1;  ///< -1: none
	Crs crs = MapCrs;
	QString crs_spec;
	const Symbol* default_symbol = nullptr;  ///< A point symbol of the map, for rows without a known code.
	const Symbol* text_symbol = nullptr;     ///< A text symbol of the map, needed with a text column.
};


/**
 * Creates point objects, and text objects for a text column, from delimited text.
 *
 * The z value goes to the tag `ele` as written. Rows whose x or y does not
 * parse are skipped and named by line number in the warning.
 *
 * See the design notes, "Import options", Delimited text.
 */
OpResult importPoints(const Map& map, const QString& text, const ImportPointsParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
