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
#include <QRegularExpression>
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

// ISOM 2017-2, Revision 6 of January 2024, the symbols' own "Minimum length"
// and "Minimum area" at 1:15,000. An area given as two sides is their product.
// Left out: minimums which are a number of dashes, lines or steps without a
// length (103, 307, 532), those of closed contours and form lines (101, 103),
// widths, and the openings of the forest (405). 416 is in isom2017Minimum().
constexpr Entry isom_entries[] = {
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
	{ 513, 1.4, 0.0 },     // Wall, and the retained wall 513.2 (2.4 mm before Revision 6)
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

// ISSprOM 2019-2, Revision 6 of January 2024, at 1:4,000. Left out as above:
// "2 lines" (307, 308, 407, 409), "3 steps" (532), widths. 533 states a
// ground size, 65 m2, which is 4.0625 mm2 at 1:4,000. A third number other
// than -1 is the second part of the symbol's number, which the entry asks.
struct SprintEntry
{
	int number;
	int sub;        ///< -1: every symbol of the number.
	double length;  ///< mm
	double area;    ///< mm²
};

constexpr SprintEntry sprint_entries[] = {
	{ 105, -1, 1.4, 0.0 },     // Small earth wall 105.1, retaining earth wall 105.2
	{ 107, -1, 2.0, 0.0 },     // Erosion gully
	{ 108, -1, 1.0, 0.0 },     // Small erosion gully: two dots
	{ 201, -1, 1.1, 0.0 },     // Uncrossable cliff
	{ 202, -1, 1.1, 0.0 },     // Passable rock face
	{ 206, -1, 0.0, 0.75 },    // Gigantic boulder or rock pillar
	{ 213, -1, 0.0, 1.0 },     // Open sandy ground
	{ 214, -1, 0.0, 1.0 },     // Bare rock
	{ 301, -1, 0.0, 0.5 },     // Uncrossable body of water, inside
	{ 302, -1, 0.0, 0.5 },     // Shallow body of water, inside
	{ 305, -1, 1.0, 0.0 },     // Small crossable watercourse
	{ 306, -1, 4.11, 0.0 },    // Minor / seasonal watercourse: two dashes
	{ 309, -1, 1.0, 0.0 },     // Narrow marsh: two dots
	{ 401, -1, 0.0, 0.5 },     // Open land
	{ 402, -1, 0.0, 6.25 },    // Open land with scattered trees
	{ 403, -1, 0.0, 1.0 },     // Rough open land
	{ 404, -1, 0.0, 6.25 },    // Rough open land with scattered trees
	{ 406, -1, 0.0, 1.0 },     // Vegetation: slow running
	{ 408, -1, 0.0, 0.5 },     // Vegetation: walk
	{ 410, -1, 0.0, 0.3 },     // Vegetation: fight
	{ 411, -1, 0.0, 0.3 },     // Uncrossable vegetation
	{ 412, -1, 0.0, 9.0 },     // Cultivated land
	{ 413, -1, 0.0, 4.0 },     // Orchard
	{ 414, -1, 0.0, 4.0 },     // Vineyard or similar
	{ 415, -1, 1.0, 0.0 },     // Distinct cultivation boundary
	{ 416, -1, 2.65, 0.0 },    // Distinct vegetation boundary: five dots
	{ 501, 3, 0.0, 6.25 },     // Paved area with scattered trees 501.3
	{ 506, -1, 3.4, 0.0 },     // Small unpaved footpath or track: two dashes
	{ 507, -1, 7.9, 0.0 },     // Less distinct small path: two sections of double dashes
	{ 508, -1, 9.75, 0.0 },    // Narrow ride: two dashes
	{ 509, -1, 4.0, 0.0 },     // Railway: two black dashes
	{ 510, -1, 3.75, 0.0 },    // Power line, cableway or ski lift
	{ 512, 2, 0.7, 0.0 },      // Underpass or tunnel 512.2: two dashes
	{ 513, -1, 1.4, 0.0 },     // Passable wall 513.1, passable retaining wall 513.2
	{ 515, -1, 1.0, 0.0 },     // Uncrossable wall
	{ 516, -1, 2.2, 0.0 },     // Passable fence or railing
	{ 518, -1, 3.0, 0.0 },     // Uncrossable fence or railing
	{ 520, -1, 0.0, 0.25 },    // Area that shall not be entered
	{ 521, -1, 0.0, 0.25 },    // Building
	{ 522, -1, 0.0, 0.25 },    // Canopy
	{ 528, -1, 2.2, 0.0 },     // Prominent line feature
	{ 529, -1, 3.0, 0.0 },     // Prominent uncrossable line feature
	{ 533, -1, 0.0, 4.0625 },  // Area with obstacles: 65 m2 on the ground
	{ 709, -1, 0.0, 9.0 },     // Out-of-bounds area
	{ 714, -1, 0.0, 0.25 },    // Temporary construction or closed area
};

bool isLineOnly(const Symbol& symbol)
{
	auto const types = symbol.getContainedTypes();
	return (types & Symbol::Line) != 0 && (types & Symbol::Area) == 0;
}

/// Symbol sets keep the tags of an earth bank or a cliff as line symbols under the feature's number
/// ("Earth bank (Tag)", "Impassable cliff, tag line"). A tag is not the feature: no minimum.
/// The word is looked for in the singular: "Cliff without tags" is a cliff.
bool isTag(const Symbol& symbol)
{
	return isLineOnly(symbol) && symbol.getName().contains(QRegularExpression(QStringLiteral("\\btag\\b"), QRegularExpression::CaseInsensitiveOption));
}

}  // namespace



StandardMinimum isom2017Minimum(const Symbol& symbol)
{
	auto const number = symbol.getNumberComponent(0);
	auto const line = isLineOnly(symbol);
	auto const area = (symbol.getContainedTypes() & Symbol::Area) != 0;
	StandardMinimum minimum;
	if (isTag(symbol))
		return minimum;
	if (number == 416 && line)
	{
		// Distinct vegetation boundary: 4 dashes of the green line, 5 dots of the black one
		// (2.5 mm before Revision 6).
		minimum.length = colourGroupOf(symbol.guessDominantColor()) == ColourGroup::Green ? 1.8 : 2.0;
		return minimum;
	}
	for (auto const& entry : isom_entries)
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


StandardMinimum issprom2019Minimum(const Symbol& symbol)
{
	auto const number = symbol.getNumberComponent(0);
	auto const line = isLineOnly(symbol);
	auto const area = (symbol.getContainedTypes() & Symbol::Area) != 0;
	StandardMinimum minimum;
	if (isTag(symbol))
		return minimum;
	for (auto const& entry : sprint_entries)
	{
		if (entry.number != number || (entry.sub >= 0 && entry.sub != symbol.getNumberComponent(1)))
			continue;
		if (line)
			minimum.length = entry.length;
		if (area)
			minimum.area = entry.area;
		break;
	}
	return minimum;
}


StandardMinimum standardMinimum(MapStandard standard, const Symbol& symbol)
{
	switch (standard)
	{
	case MapStandard::Isom2017:
		return isom2017Minimum(symbol);
	case MapStandard::Issprom2019:
		return issprom2019Minimum(symbol);
	case MapStandard::None:
		break;
	}
	return {};
}


namespace {

/// The opening minimums of 405 Forest: for the two light screens, for green 100 %, for the other screens.
double openingMinimum(const Symbol& symbol, double light, double fight, double other)
{
	if ((symbol.getContainedTypes() & Symbol::Area) == 0)
		return 0.0;
	switch (symbol.getNumberComponent(0))
	{
	case 401:  // Open land
	case 408:  // Vegetation: walk
		return light;
	case 410:  // Vegetation: fight
		return fight;
	case 402:
	case 403:
	case 404:
	case 406:
	case 407:
	case 409:  // the other screens
		return other;
	default:
		return 0.0;
	}
}

}  // namespace


double isom2017OpeningMinimum(const Symbol& symbol)
{
	// 0.7 mm x 0.7 mm, 0.55 mm x 0.55 mm, 1 mm x 1 mm.
	return openingMinimum(symbol, 0.49, 0.3025, 1.0);
}


double issprom2019OpeningMinimum(const Symbol& symbol)
{
	return openingMinimum(symbol, 0.5, 0.3, 1.0);
}


double standardOpeningMinimum(MapStandard standard, const Symbol& symbol)
{
	switch (standard)
	{
	case MapStandard::Isom2017:
		return isom2017OpeningMinimum(symbol);
	case MapStandard::Issprom2019:
		return issprom2019OpeningMinimum(symbol);
	case MapStandard::None:
		break;
	}
	return 0.0;
}


bool isom2017AreaWithoutGap(const Symbol& symbol)
{
	if (symbol.getContainedTypes() != Symbol::Area)
		return false;
	// 411 is left out: in the sprint standard it is uncrossable vegetation, which needs its passages.
	auto const number = symbol.getNumberComponent(0);
	return number == 213 || number == 214 || (number >= 401 && number <= 414 && number != 411);
}


MapStandard mapStandard(const Map& map)
{
	auto const scale = map.getScaleDenominator();
	auto const id = map.symbolSetId();
	// A map read from an OCD file calls its symbol set "OCD": it does not say which standard it
	// follows, and neither does a map without a name. Their scale says it.
	if (id.isEmpty() || id == QLatin1String("OCD"))
	{
		if (scale >= 7500)
			return MapStandard::Isom2017;
		if (scale <= 5000)
			return MapStandard::Issprom2019;
		return MapStandard::None;
	}
	if (id.startsWith(QLatin1String("ISOM 2017")) || id.startsWith(QLatin1String("ISOM2017")))
		return scale >= 7500 ? MapStandard::Isom2017 : MapStandard::None;
	if (id.startsWith(QLatin1String("ISSprOM")))
		return scale < 7500 ? MapStandard::Issprom2019 : MapStandard::None;
	return MapStandard::None;
}


double standardEnlargement(const Map& map)
{
	switch (mapStandard(map))
	{
	case MapStandard::Isom2017:
		return 15000.0 / map.getScaleDenominator();
	case MapStandard::Issprom2019:
		return 4000.0 / map.getScaleDenominator();
	case MapStandard::None:
		break;
	}
	return 0.0;
}


}  // namespace OpenOrienteering
