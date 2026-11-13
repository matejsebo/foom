/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_COLOR_GROUP_H
#define OPENORIENTEERING_COLOR_GROUP_H

#include <QString>

namespace OpenOrienteering {

class MapColor;


/**
 * The colour group of a map colour.
 *
 * The legibility gap test compares only objects of the same group when asked
 * to. See the design notes, "Colours", and
 * the design notes, "Legibility".
 */
enum class ColourGroup : int
{
	Undefined = 0,  ///< No group stored: guessColourGroup() decides.
	Black,
	Brown,
	Blue,
	Green,
	Yellow,
	Purple,
	Other,
};

/**
 * Guesses the colour group from the colour's name, else from its CMYK value.
 *
 * The rule is in the design notes, "Legibility".
 */
ColourGroup guessColourGroup(const MapColor& color);

/**
 * Returns the colour's stored group, or the guess when none is stored.
 *
 * A null colour is in the group Other.
 */
ColourGroup colourGroupOf(const MapColor* color);

/** Returns the key of a group as stored in files ("brown"); empty for Undefined. */
QString colourGroupKey(ColourGroup group);

/** Returns the group of a stored key; Undefined for an unknown or empty key. */
ColourGroup colourGroupFromKey(const QString& key);

/** Returns the translated name of a group for dialogs. */
QString colourGroupName(ColourGroup group);


}  // namespace OpenOrienteering

#endif
