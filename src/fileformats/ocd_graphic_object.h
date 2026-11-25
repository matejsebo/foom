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

#ifndef FOOM_OCD_GRAPHIC_OBJECT_H
#define FOOM_OCD_GRAPHIC_OBJECT_H

#include <memory>

#include <QtGlobal>
#include <QString>

#include "core/map_color.h"

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * The objects without a symbol, as OCD 2018 files hold them.
 *
 * A graphic object has the symbol number -2, the number of a colour of the
 * map in the colour field of its header and, for a line, the width there.
 * A layout object has the symbol number -4, cyan, magenta, yellow and black
 * as the four bytes of the colour field, the width of a line, and an object
 * string of type 3 with its opacity and, for a text, its font.
 *
 * See the design notes, "Graphic and layout objects in OCD files".
 */

/** The symbol number of OCD graphic objects. */
constexpr int ocd_graphic_object_symbol = -2;

/** The symbol number of OCD layout objects. */
constexpr int ocd_layout_object_symbol = -4;

/** The type of the object string of a layout object. */
constexpr int ocd_layout_object_string_type = 3;


/** What the object string of a layout object says. */
struct OcdLayoutStyle
{
	QString font;          ///< The first field: the font of a text, else empty.
	double opacity = 1.0;  ///< o, percent in the file.
	double size_pt = 0.0;  ///< s, the size of a text in points; 0 when the string has none.
	bool bold = false;     ///< b
	bool italic = false;   ///< i
	int alignment = 3;     ///< a: 0 left, 1 centre, 2 right, 3 justified, which is the usual value.
};

/** Reads the object string of a layout object; missing fields keep their defaults. */
OcdLayoutStyle parseOcdLayoutStyle(const QString& object_string);

/** The colour of a layout object from the colour field of its header: the bytes are C, M, Y and K, 0 to 255. */
MapColorCmyk ocdLayoutCmyk(quint32 color_field);

/** The name FOOM gives the colour of layout objects, e.g. "Layout C0 M100 Y100 K0, 50 %". */
QString ocdLayoutColorName(const MapColorCmyk& cmyk, double opacity);

/** Finds the colour of that name, or makes it at the top of the map's colour table. */
const MapColor* ocdLayoutColor(Map& map, const MapColorCmyk& cmyk, double opacity);

/**
 * Makes the private symbol of a graphic or layout object, which is not yet
 * in a map: a line of the width (in 0.01 mm) for the object type 2, an area
 * for 3, and for the unformatted text 4 a text of the style's font. Returns
 * nullptr for other types, and for a text without a style.
 */
std::unique_ptr<Symbol> makeOcdGraphicSymbol(int object_type, const MapColor* color, int line_width, const OcdLayoutStyle* style, const QString& name);


/** The colour and the width of an object without a symbol. */
struct OcdGraphicStyle
{
	const MapColor* color = nullptr;
	int line_width = 0;  ///< In 0.01 mm; 0 for an area.
};

/**
 * Says whether the objects of a symbol can be written as objects without a
 * symbol: a private line symbol which is one plain line, or a private area
 * symbol which is one plain fill, each with a colour.
 */
bool isOcdGraphicSymbol(const Symbol* symbol, OcdGraphicStyle* style = nullptr);

/** The colour field of a layout object, C, M, Y and K as bytes: the reverse of ocdLayoutCmyk(). */
quint32 ocdLayoutColorField(const MapColorCmyk& cmyk);

/** The object string of a layout object: all fields for a text, else the opacity alone. */
QString ocdLayoutObjectString(const OcdLayoutStyle& style, bool text);

/**
 * The entry of a layout object in the file's list of layout objects, which is a
 * string of the type of the layout images: the name, `r0`, `s1` and the
 * object's place in the object index, counted from 1.
 */
QString ocdLayoutListEntry(int object_type, const QString& text, int place);


}  // namespace OpenOrienteering

#endif
