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


#ifndef FOOM_PRIVATE_SYMBOL_H
#define FOOM_PRIVATE_SYMBOL_H

#include <memory>

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * Private symbols: symbols which belong to one object or one generated group.
 *
 * See the design notes, "Private symbols".
 */

/**
 * Returns a private copy of the symbol.
 *
 * The public parts of a combined symbol become private parts of the copy,
 * so the copy refers to no other symbol.
 */
std::unique_ptr<Symbol> makePrivateCopy(const Symbol& symbol);

/**
 * Gives every selected object its own private copy of its symbol, as one
 * undo step (to-graphic).
 *
 * Objects which are the only user of a private symbol are left alone.
 * Returns the number of converted objects.
 */
int convertToGraphic(Map& map);

/**
 * Returns the private symbol which all selected objects share, or nullptr.
 */
const Symbol* sharedPrivateSymbol(const Map& map);

/**
 * Removes the private symbols which no object uses and which no undo or redo
 * step could bring back into use. Returns the number of removed symbols.
 */
int purgePrivateSymbols(Map& map);


}  // namespace OpenOrienteering

#endif
