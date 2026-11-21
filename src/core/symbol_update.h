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

#ifndef FOOM_CORE_SYMBOL_UPDATE_H
#define FOOM_CORE_SYMBOL_UPDATE_H

#include <QStringList>

namespace OpenOrienteering {

class Map;


/**
 * What updateSymbolsFrom() did: symbols as number and name.
 *
 * See the design notes, "Update from file".
 */
struct SymbolUpdateReport
{
	QStringList added;
	QStringList changed;
	QStringList unchanged;
};

/**
 * Takes the definitions of the reference's public symbols into the map: a
 * symbol with the same number is replaced in place (its objects follow, its
 * hidden and protected state stay), a symbol the map lacks is added; the
 * colours they need come along. New definitions are scaled by the factor.
 */
SymbolUpdateReport updateSymbolsFrom(Map& map, const Map& reference, double scale_factor = 1.0);


}  // namespace OpenOrienteering

#endif
