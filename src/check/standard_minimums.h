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

#ifndef FOOM_STANDARD_MINIMUMS_H
#define FOOM_STANDARD_MINIMUMS_H

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * The graphical minimum dimensions which the IOF's forest map standard,
 * ISOM 2017-2 (the version of 2022-01-24), states for its symbols, at the
 * base scale of 1:15,000.
 *
 * See the design notes, "The standard's minimum sizes".
 */
struct StandardMinimum
{
	double length = 0.0;  ///< mm; 0 when the standard gives none.
	double area = 0.0;    ///< mm²; 0 when the standard gives none.
};

/**
 * The minimum of the standard for a symbol, by its number; zeros when it has
 * none, and for a line symbol which its name calls a tag.
 */
StandardMinimum isom2017Minimum(const Symbol& symbol);

/**
 * How much larger than at 1:15,000 the minimums are in this map (1.5 at
 * 1:10,000), or 0 when the standard is not the map's: when its symbol set
 * is named and is not ISOM 2017 (a map read from an OCD file names none),
 * or when its scale is larger than 1:7,500.
 */
double standardEnlargement(const Map& map);


}  // namespace OpenOrienteering

#endif
