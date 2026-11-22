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

#ifndef FOOM_CORE_COLOUR_LOAD_H
#define FOOM_CORE_COLOUR_LOAD_H

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;


/**
 * How loadColoursFrom() treats the map's colour table.
 *
 * See the design notes, "Load colours from file".
 */
enum class ColourLoadMode
{
	Replace,  ///< Colours of the same name take the file's definition; the table takes the file's order.
	Append,   ///< Only the file's colours which the map lacks are added, at the bottom.
};

/** The colour names, by what happened to them. */
struct ColourLoadReport
{
	QStringList changed;    ///< Replace: took a definition which differs.
	QStringList unchanged;  ///< Replace: the definition was the same.
	QStringList existing;   ///< Append: the map has a colour of the name, which stays as it is.
	QStringList added;
	QStringList kept;       ///< Replace: colours of the map which the file lacks; they follow the file's.

	QString text() const;
};

/**
 * Loads the colours of another map into this one, matched by name
 * (case and spaces at the ends ignored). The map's colours keep their
 * identity, so symbols and objects stay with them.
 */
ColourLoadReport loadColoursFrom(Map& map, const Map& source, ColourLoadMode mode);


}  // namespace OpenOrienteering

#endif
