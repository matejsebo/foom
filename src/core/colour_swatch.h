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

#ifndef FOOM_CORE_COLOUR_SWATCH_H
#define FOOM_CORE_COLOUR_SWATCH_H

#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "core/map_color.h"

namespace OpenOrienteering {

class Map;


/**
 * One row of a colour swatch file.
 *
 * See the design notes, "Swatch import".
 */
struct SwatchEntry
{
	int number = -1;       ///< The colour number of the file, or -1 when the file has none.
	QString name;
	MapColorCmyk cmyk;     ///< 0 to 1.
	int overprint = -1;    ///< 0 or 1, or -1 when the file has no such column.
	double opacity = -1;   ///< 0 to 1, or negative when the file has no such column.
};

/** Reads swatch text files (tab or semicolon separated) and CSV files. */
std::vector<SwatchEntry> readColourSwatch(const QByteArray& data, QString* error);

/** Reads a swatch file from disk. */
std::vector<SwatchEntry> readColourSwatchFile(const QString& path, QString* error);


/** What applyColourSwatch() changes besides the CMYK values. */
struct SwatchOptions
{
	bool overprint = false;    ///< Spot colours take the overprint flag (knockout off for 1).
	bool opacity = false;      ///< Colours take the opacity.
	bool add_missing = false;  ///< Rows without a matching colour become new colours at the bottom.
};

/** The colour names, by what happened to them. */
struct SwatchReport
{
	QStringList updated;
	QStringList unchanged;
	QStringList added;
	QStringList not_found;   ///< Rows without a matching colour, when they are not added.

	QString text() const;
};

/**
 * Gives the map's colours the values of the swatch rows of the same name
 * (case and spaces at the ends ignored; the first row of a name counts).
 * Updates the objects drawn in the changed colours.
 */
SwatchReport applyColourSwatch(Map& map, const std::vector<SwatchEntry>& entries, const SwatchOptions& options);


}  // namespace OpenOrienteering

#endif
