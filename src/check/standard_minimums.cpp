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

#include "standard_minimums.h"

#include <QLatin1String>
#include <QString>

#include "core/color_group.h"
#include "core/map.h"
#include "core/symbols/symbol.h"

namespace OpenOrienteering {

namespace {

struct Entry
{
	int number;
	double length;  ///< mm
	double area;    ///< mm²
};

// ISOM 2017-2, version of 2022-01-24, the symbols' own "Minimum length" and
// "Minimum area" at 1:15,000. An area given as two sides is their product.
// Left out: minimums which are a number of dashes without a length (103,
// 532), those of closed contours and form lines (101, 103), widths, and the
// openings of the forest (405). 416 and 513 are in isom2017Minimum().
constexpr Entry entries[] = {
	{ 104, 0.6, 0.0 },     // Earth bank
	{ 105, 1.4, 0.0 },     // Earth wall
	{ 106, 3.65, 0.0 },    // Ruined earth wall: two dashes
	{ 107, 1.15, 0.0 },    // Erosion gully
	{ 108, 0.7, 0.0 },     // Small erosion gully: two dots
	{ 201, 0.6, 0.0 },     // Impassable cliff
	{ 202, 0.6, 0.0 },     // Cliff
	{ 206, 0.0, 0.3 },     // Gigantic boulder or rock pillar
	{ 213, 0.0, 1.0 },     // Sandy ground: 1 mm x 1 mm
	{ 214, 0.0, 1.0 },     // Bare rock: 1 mm x 1 mm
	{ 215, 1.0, 0.0 },     // Trench
	{ 301, 0.0, 0.3025 },  // Uncrossable body of water: 0.55 mm x 0.55 mm inside
	{ 302, 0.0, 0.49 },    // Shallow body of water: 0.7 mm x 0.7 mm inside
	{ 304, 1.0, 0.0 },     // Crossable watercourse
	{ 305, 1.0, 0.0 },     // Small crossable watercourse
	{ 306, 2.75, 0.0 },    // Minor/seasonal water channel: two dashes
	{ 307, 0.0, 0.5 },     // Uncrossable marsh, inside
	{ 308, 0.0, 0.2 },     // Marsh: 0.5 mm x 0.4 mm
	{ 309, 0.7, 0.0 },     // Narrow marsh: two dots
	{ 310, 0.0, 1.4 },     // Indistinct marsh: 2.0 mm x 0.7 mm
	{ 401, 0.0, 0.3025 },  // Open land: 0.55 mm x 0.55 mm
	{ 402, 0.0, 4.0 },     // Open land with scattered trees: 2 mm x 2 mm
	{ 403, 0.0, 1.0 },     // Rough open land: 1 mm x 1 mm
	{ 404, 0.0, 6.25 },    // Rough open land with scattered trees: 2.5 mm x 2.5 mm
	{ 406, 0.0, 1.0 },     // Vegetation: slow running: 1 mm x 1 mm
	{ 407, 0.0, 1.5 },     // Vegetation, slow running, good visibility: 1.5 mm x 1 mm
	{ 408, 0.0, 0.49 },    // Vegetation: walk: 0.7 mm x 0.7 mm
	{ 409, 0.0, 1.0 },     // Vegetation: walk, good visibility: 1 mm x 1 mm
	{ 410, 0.0, 0.3025 },  // Vegetation: fight: 0.55 mm x 0.55 mm
	{ 412, 0.0, 9.0 },     // Cultivated land: 3 mm x 3 mm
	{ 413, 0.0, 4.0 },     // Orchard: 2 mm x 2 mm
	{ 414, 0.0, 4.0 },     // Vineyard or similar: 2 mm x 2 mm
	{ 415, 2.0, 0.0 },     // Distinct cultivation boundary
	{ 501, 0.0, 1.0 },     // Paved area: 1 mm x 1 mm
	{ 504, 6.25, 0.0 },    // Vehicle track: two dashes
	{ 505, 4.25, 0.0 },    // Footpath: two dashes
	{ 506, 2.25, 0.0 },    // Small footpath: two dashes
	{ 507, 5.3, 0.0 },     // Less distinct small footpath: two sections of double dashes
	{ 508, 3.25, 0.0 },    // Narrow ride or linear trace: two dashes
	{ 509, 4.0, 0.0 },     // Railway: two dashes
	{ 510, 5.0, 0.0 },     // Power line, cableway or skilift
	{ 512, 0.4, 0.0 },     // Bridge / tunnel: the baseline
	{ 514, 3.65, 0.0 },    // Ruined wall: two dashes
	{ 515, 3.0, 0.0 },     // Impassable wall
	{ 516, 1.5, 0.0 },     // Fence
	{ 517, 3.65, 0.0 },    // Ruined fence: two dashes
	{ 518, 2.0, 0.0 },     // Impassable fence
	{ 520, 0.0, 1.0 },     // Area that shall not be entered: 1 mm x 1 mm
	{ 521, 0.0, 0.25 },    // Building: 0.5 mm x 0.5 mm
	{ 522, 0.0, 0.36 },    // Canopy: 0.6 mm x 0.6 mm
	{ 528, 1.5, 0.0 },     // Prominent line feature
	{ 529, 2.0, 0.0 },     // Prominent uncrossable line feature
	{ 707, 4.5, 0.0 },     // Marked route: 2 dashes
	{ 708, 1.0, 0.0 },     // Out-of-bounds boundary
	{ 709, 0.0, 9.0 },     // Out-of-bounds area: 3 mm x 3 mm
	{ 711, 6.0, 0.0 },     // Out-of-bounds route: 2 symbols
};

}  // namespace



StandardMinimum isom2017Minimum(const Symbol& symbol)
{
	auto const number = symbol.getNumberComponent(0);
	auto const types = symbol.getContainedTypes();
	auto const line = (types & Symbol::Line) != 0 && (types & Symbol::Area) == 0;
	auto const area = (types & Symbol::Area) != 0;
	StandardMinimum minimum;
	if (number == 416 && line)
	{
		// Distinct vegetation boundary: 4 dashes of the green line, 5 dots of the black one.
		minimum.length = colourGroupOf(symbol.guessDominantColor()) == ColourGroup::Green ? 1.8 : 2.5;
		return minimum;
	}
	if (number == 513 && line)
	{
		// Wall 513.1, retained wall 513.2.
		minimum.length = symbol.getNumberComponent(1) == 2 ? 2.4 : 1.4;
		return minimum;
	}
	for (auto const& entry : entries)
	{
		if (entry.number != number)
			continue;
		if (line)
			minimum.length = entry.length;
		if (area)
			minimum.area = entry.area;
		break;
	}
	return minimum;
}


double standardEnlargement(const Map& map)
{
	auto const scale = map.getScaleDenominator();
	if (scale < 7500)
		return 0.0;
	// A map read from an OCD file calls its symbol set "OCD": it does not say which standard it follows.
	auto const id = map.symbolSetId();
	if (!id.isEmpty() && id != QLatin1String("OCD") && !id.startsWith(QLatin1String("ISOM 2017")) && !id.startsWith(QLatin1String("ISOM2017")))
		return 0.0;
	return 15000.0 / scale;
}


}  // namespace OpenOrienteering
