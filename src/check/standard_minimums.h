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
 * The IOF standard whose graphical minimum dimensions a map is checked by.
 */
enum class MapStandard
{
	None,
	Isom2017,     ///< ISOM 2017-2, the forest maps: dimensions at 1:15,000.
	Issprom2019,  ///< ISSprOM 2019-2, the sprint maps: dimensions at 1:4,000.
};

/**
 * A graphical minimum dimension which a standard of the IOF states for a
 * symbol, at the standard's base scale. Both standards are read in their
 * Revision 6 of January 2024.
 *
 * See the design notes, "The standard's minimum sizes"
 * and "The sprint standard's minimum sizes".
 */
struct StandardMinimum
{
	double length = 0.0;  ///< mm; 0 when the standard gives none.
	double area = 0.0;    ///< mm²; 0 when the standard gives none.
};

/**
 * The minimum of the forest standard for a symbol, by its number; zeros when
 * it has none, and for a line symbol which its name calls a tag.
 */
StandardMinimum isom2017Minimum(const Symbol& symbol);

/** The same of the sprint standard, by the symbol's number and, where the standard asks it, its second part. */
StandardMinimum issprom2019Minimum(const Symbol& symbol);

/** The minimum of the given standard; zeros for none. */
StandardMinimum standardMinimum(MapStandard standard, const Symbol& symbol);

/**
 * The smallest opening the forest standard allows in an area of the symbol,
 * in mm² at 1:15,000 (405 Forest: openings in screens of other colours);
 * 0 for a symbol which is no such screen.
 */
double isom2017OpeningMinimum(const Symbol& symbol);

/** The same of the sprint standard, at 1:4,000. */
double issprom2019OpeningMinimum(const Symbol& symbol);

/** The smallest opening of the given standard; 0 for none. */
double standardOpeningMinimum(MapStandard standard, const Symbol& symbol);

/**
 * Whether the standard asks no gap around an area of the symbol: it asks one
 * for areas with an outline and for those which are a solid shape alone
 * (ISOM 2017-2, 2.11.3, Minimum gaps), and so none for the screens of open
 * land and vegetation, sandy ground and bare rock.
 */
bool isom2017AreaWithoutGap(const Symbol& symbol);

/**
 * The standard a map is checked by. A map whose symbol set is named for a
 * standard follows that one, at the scales the standard is for (ISOM 2017 at
 * 1:7,500 and smaller, ISSprOM below that). A map without a name, or read
 * from an OCD file, follows the forest standard at 1:7,500 and smaller and
 * the sprint standard at 1:5,000 and larger. Any other map follows none.
 */
MapStandard mapStandard(const Map& map);

/**
 * How much larger than at its standard's base scale the minimums are in this
 * map (1.5 at 1:10,000 for a forest map, 1.33 at 1:3,000 for a sprint map),
 * or 0 when mapStandard() is none.
 */
double standardEnlargement(const Map& map);


}  // namespace OpenOrienteering

#endif
