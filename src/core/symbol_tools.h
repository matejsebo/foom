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

#ifndef FOOM_CORE_SYMBOL_TOOLS_H
#define FOOM_CORE_SYMBOL_TOOLS_H

#include <vector>

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;
class MapColor;
class Symbol;


/*
 * Selecting symbols, replacing fonts and colours, renumbering, over the map's
 * public symbols in the table's order.
 *
 * See the design notes, "Selecting symbols, replacing fonts and colours, renumbering".
 */

enum class SymbolStatus { Normal, Protected, Hidden };

/// Symbols which draw (themselves or a part) in the colour.
std::vector<const Symbol*> symbolsUsingColor(const Map& map, const MapColor* color);

/// Text symbols of the font family.
std::vector<const Symbol*> symbolsUsingFont(const Map& map, const QString& family);

/// Symbols of the type (a Symbol::Type).
std::vector<const Symbol*> symbolsOfType(const Map& map, int type);

/// Symbols of the status; normal is neither hidden nor protected.
std::vector<const Symbol*> symbolsWithStatus(const Map& map, SymbolStatus status);

/// Symbols whose number, as text, matches the pattern (* any characters, ? one).
std::vector<const Symbol*> symbolsMatchingNumber(const Map& map, const QString& pattern);

/// The font families of the text symbols, sorted, each once.
QStringList fontFamilies(const Map& map);

/// Gives the text symbols of the family from among the scope (all when empty) the family to; returns their number.
int replaceFont(Map& map, const QString& from, const QString& to, const std::vector<const Symbol*>& scope = {});

/// Draws the scope's symbols' elements of the colour from in the colour to; returns the symbols changed.
int replaceColor(Map& map, const MapColor* from, const MapColor* to, const std::vector<const Symbol*>& scope = {});

/// Numbers the scope's symbols start, start + step, ... in the table's order; returns their number.
int renumberSymbols(Map& map, int start, int step, const std::vector<const Symbol*>& scope = {});


}  // namespace OpenOrienteering

#endif
