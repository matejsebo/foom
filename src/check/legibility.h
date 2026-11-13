/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_CHECK_LEGIBILITY_H
#define OPENORIENTEERING_CHECK_LEGIBILITY_H

#include <vector>

#include <QRectF>

#include "check/issue.h"

namespace OpenOrienteering {

class Map;
class Symbol;

namespace rule {

constexpr char line_short[] = "line-short";
constexpr char area_small[] = "area-small";
constexpr char gap[] = "gap";

}  // namespace rule


/**
 * Parameters of checkLegibility().
 *
 * See the design notes, "Legibility".
 */
struct LegibilityParams
{
	/** Test lines against their symbol's minimum length (and dashed lines for two dashes). */
	bool minimum_length = true;
	/** Test areas against their symbol's minimum area. */
	bool minimum_area = true;
	/** Test the gap between objects. */
	bool gap = true;
	/** The smallest acceptable gap in mm; see defaultGap(). */
	double gap_mm = 0.15;
	/** Test gaps only between objects of the same colour group. */
	bool same_colour_group_only = true;
	/** Test gaps between two contour-class objects too. */
	bool between_contours = false;
	/** The symbols to test; empty means all. */
	std::vector<const Symbol*> symbols;
	/** The area (mm) to test; a null rectangle means the whole map. */
	QRectF extent;
};


/**
 * Returns the default gap for the map: 0.15 mm at 1:15,000, scaled to the
 * map's scale and rounded to 0.01 mm.
 */
double defaultGap(const Map& map);

/**
 * Runs the legibility scan over every part and returns the issues.
 *
 * Issues carry the measured value and the limit in mm (line length), mm²
 * (area) or mm (gap), and the objects concerned; a gap issue names both.
 * See the design notes, "Legibility".
 */
std::vector<Issue> checkLegibility(const Map& map, const LegibilityParams& params);


}  // namespace OpenOrienteering

#endif
