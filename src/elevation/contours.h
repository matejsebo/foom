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


#ifndef OPENORIENTEERING_ELEVATION_CONTOURS_H
#define OPENORIENTEERING_ELEVATION_CONTOURS_H

#include <limits>
#include <memory>
#include <vector>

#include <QPointF>
#include <QString>

namespace OpenOrienteering {

class ElevationModel;
class LineSymbol;
class Map;
class MapColor;
class Object;


/**
 * Parameters of contour generation, see the design notes, "Contours".
 */
struct ContourParams
{
	double interval = 1.0;          ///< The smallest interval (m).
	double main_interval = 5.0;     ///< Levels which are multiples of it are main contours.
	double index_interval = 25.0;   ///< Levels which are multiples of it are index contours.
	bool smoothed = false;          ///< Filter the grid with the binomial kernel and smooth the lines (the smoothed set).
	bool curves = false;            ///< Convert the smoothed lines to curves.
	bool depression_symbols = false;///< Give depressions their own symbols.
	double minimum = std::numeric_limits<double>::quiet_NaN();  ///< The lowest level (NaN: the grid's minimum).
	double maximum = std::numeric_limits<double>::quiet_NaN();  ///< The highest level (NaN: the grid's maximum).
	int tile_size = 1000;           ///< Cells per tile side; lines are joined across tile edges.
	double simplify_cells = 0.5;    ///< Simplification tolerance in cells (0: none).
	double smooth_tolerance = 0.05; ///< Line smoothing tolerance for the smoothed set, mm on paper.
};


/**
 * One contour line in projected coordinates, higher ground on the left.
 */
struct ContourLine
{
	enum Class
	{
		Smallest = 0,
		Main     = 1,
		Index    = 2
	};

	double level = 0.0;
	QString tag;                    ///< The level as written to the ele tag.
	Class cls = Smallest;
	bool closed = false;
	bool depression = false;        ///< A closed line around lower ground.
	std::vector<QPointF> points;    ///< Without a repeated first point for closed lines.
};


/// The level as written to the ele tag: up to three decimals, no trailing zeros.
QString contourLevelText(double level);

/**
 * Traces the contours of the grid by marching squares.
 *
 * The grid is used as given: the caller smooths it for the smoothed set
 * (makeContourObjects does).
 */
std::vector<ContourLine> traceContours(const ElevationModel& grid, const ContourParams& params);


/**
 * The contour symbols by class, for hills and depressions.
 */
struct ContourSymbols
{
	const LineSymbol* hill[3] = { nullptr, nullptr, nullptr };
	const LineSymbol* depression[3] = { nullptr, nullptr, nullptr };
};

/**
 * Finds the map's contour symbols of the set, or creates them.
 *
 * The symbols are numbered 10050 to 10052 for the
 * unsmoothed set, 10060 to 10062 for the smoothed one, second component 1 for
 * depressions.
 */
ContourSymbols contourSymbols(Map& map, bool smoothed);

/** foom: The map's brown, which the contours take; made when the map has none. */
const MapColor* contourBrown(Map& map);


struct ContourResult
{
	std::vector<std::unique_ptr<Object>> objects;
	QString error;
};

/**
 * Creates the contour objects for the grid in the map's coordinates.
 *
 * For the smoothed set the grid is filtered first, and the lines are smoothed
 * with ops::smoothPaths and optionally converted to curves. Each object gets
 * the tag ele, and depressions the tag depression=yes.
 */
ContourResult makeContourObjects(const Map& map, const ElevationModel& grid, const ContourParams& params, const ContourSymbols& symbols);

/// Adds the objects to the current part as one undo step and selects them.
bool addContours(Map& map, std::vector<std::unique_ptr<Object>>&& objects);


}  // namespace OpenOrienteering

#endif
