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

#ifndef OPENORIENTEERING_GLYPH_CODE_H
#define OPENORIENTEERING_GLYPH_CODE_H

#include <QString>

namespace OpenOrienteering {

/**
 * Returns the character of a hexadecimal character code, as UTF-16 (a
 * surrogate pair above FFFF).
 * 
 * White space at the ends is ignored, and the digits may follow "U+",
 * "u+", "0x" or a backslash and "u". A text without digits, with other characters, the
 * code 0, a code above 10FFFF or a surrogate gives an empty string, and
 * the reason in error when it is given.
 * 
 * See the design notes, "Insert glyphs".
 */
QString glyphFromHex(const QString& text, QString* error = nullptr);


}  // namespace OpenOrienteering

#endif
