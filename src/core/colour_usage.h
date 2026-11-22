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

#ifndef FOOM_CORE_COLOUR_USAGE_H
#define FOOM_CORE_COLOUR_USAGE_H

#include <vector>

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;
class MapColor;


/**
 * Where a colour of the map is used.
 *
 * See the design notes, "Colour usage report".
 */
struct ColourUse
{
	const MapColor* color = nullptr;
	QStringList symbols;     ///< Number and name of the symbols drawn in the colour.
	int objects = 0;         ///< Objects of those symbols, in all parts.
	QStringList mixed_into;  ///< Colours mixed from this spot colour.
	int templates = 0;       ///< Templates printed on this colour's plate.
	bool used = false;       ///< By a symbol, a template, or a used colour mixed from it.
};

/** One entry per colour, in the order of the colour table. */
std::vector<ColourUse> colourUsage(const Map& map);

/** The report as text: one line per colour, the symbols indented below it. */
QString colourUsageText(const std::vector<ColourUse>& usage);

/** Deletes the colours which are not used and returns their names. */
QStringList deleteUnusedColours(Map& map);


}  // namespace OpenOrienteering

#endif
