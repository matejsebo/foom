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


#ifndef FOOM_OBJECT_STYLE_H
#define FOOM_OBJECT_STYLE_H

#include <memory>

#include <QString>

namespace OpenOrienteering {

class Map;
class MapColor;
class Symbol;


/**
 * The values of the reduced object style panel of private symbols.
 *
 * See the design notes, "Object style panel".
 */
struct ObjectStyle
{
	/** The symbol is a line symbol or has line parts. */
	bool has_line = false;
	const MapColor* line_color = nullptr;
	/** The line width in mm. */
	double line_width = 0;

	/** The symbol is an area symbol or has area parts. */
	bool has_fill = false;
	const MapColor* fill_color = nullptr;

	/** The symbol is a text symbol. */
	bool has_text = false;
	const MapColor* text_color = nullptr;
	QString font_family;
	/** The font size in mm. */
	double font_size = 0;

	/** Returns true if the rows and their values are the same. */
	bool operator==(const ObjectStyle& other) const;
	bool operator!=(const ObjectStyle& other) const { return !(*this == other); }
};


/** The values of a symbol, or of its first line and first area part. */
ObjectStyle objectStyleOf(const Symbol& symbol);

/**
 * Returns a private copy of the symbol with the values of the style, set on
 * the symbol or on all its line parts and all its area parts.
 *
 * Widths and sizes below the smallest allowed value are raised to it.
 */
std::unique_ptr<Symbol> styledCopy(const Symbol& symbol, const ObjectStyle& style);

/**
 * Gives the selected objects, which share one private symbol, a new private
 * symbol with the values of the style, as one undo step (object-style).
 *
 * Returns false and changes nothing if the objects share no private symbol
 * or if the style changes no value.
 */
bool setObjectStyle(Map& map, const ObjectStyle& style);


}  // namespace OpenOrienteering

#endif
