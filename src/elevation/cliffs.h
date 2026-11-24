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

#ifndef FOOM_ELEVATION_CLIFFS_H
#define FOOM_ELEVATION_CLIFFS_H

#include <cstddef>
#include <memory>
#include <vector>

#include <QPointF>

namespace OpenOrienteering {

class AreaSymbol;
class ElevationModel;
class LineSymbol;
class Map;
class Object;


/**
 * Cliffs from the slope gradient.
 *
 * See the design notes, "Cliffs from the slope gradient".
 */
struct CliffParams
{
	double threshold = 45.0;      ///< The slope threshold, degrees.
	int min_area = 3;             ///< Corners of a region which is drawn as an area, at least.
	int min_length = 3;           ///< The length, in cells, of a region which is drawn as a line, at least.
	/// Regions at most this wide, in cells, and at least twice as long become lines along their middle; 0 for areas only.
	double line_width = 3.0;
};


/**
 * The slope at the interior corners of a grid's cells.
 */
struct CliffRaster
{
	enum State : unsigned char { Flat = 0, Steep = 1, Cliff = 2 };
	
	int width = 0;                ///< Corners per row: the grid's columns less one.
	int height = 0;               ///< Rows of corners: the grid's rows less one.
	QPointF first;                ///< The projected position of corner (0, 0), between the grid's first two columns and rows.
	double cell = 1.0;
	std::vector<float> gradient;  ///< Per corner, row by row from the north.
	std::vector<unsigned char> state;
	
	State at(int i, int j) const { return State(state[std::size_t(j) * std::size_t(width) + std::size_t(i)]); }
	QPointF position(int i, int j) const { return { first.x() + i * cell, first.y() - j * cell }; }
};


/**
 * A cliff. An area has rings in projected coordinates, outlines and holes;
 * a narrow region has a line along its middle instead, and no rings.
 * Both have the number of corners whose squares the region covers.
 *
 * See the design notes, "Cliff lines".
 */
struct CliffArea
{
	std::vector<std::vector<QPointF>> rings;
	std::vector<QPointF> line;
	int pixels = 0;

	bool isLine() const { return !line.empty(); }
};


/// The filter of n cells: a Gaussian of n cells' standard deviation over 2n + 1 by 2n + 1 cells; cells within n of the border or near a void cell keep their value.
std::unique_ptr<ElevationModel> gaussianFiltered(const ElevationModel& grid, int n = 1);

/// The gradient above which the slope raster is black for the threshold in degrees.
double steepGradient(double threshold);

/// The gradient which the steepest corner of a region of steep corners needs for the region to be a cliff.
double cliffGradient(double threshold);

/// The gradient at every interior corner of the filtered grid, and whether it is steep or in a cliff region.
CliffRaster cliffRaster(const ElevationModel& grid, double threshold);

/// The areas and lines of the regions of cliff corners which pass the minimum of their kind.
std::vector<CliffArea> cliffAreas(const CliffRaster& raster, const CliffParams& params);

/// cliffAreas() of cliffRaster(), the lines turned so that the lower ground is on their right.
std::vector<CliffArea> findCliffs(const ElevationModel& grid, const CliffParams& params);

/// Finds the area symbol 10080, or makes it in the map's black.
const AreaSymbol* cliffSymbol(Map& map);

/// Finds the line symbol 10082, or makes it in the map's black, 0.18 mm wide.
const LineSymbol* cliffLineSymbol(Map& map);

/// Objects of the cliffs in the map's coordinates, areas and open paths, with the tag feature = cliff.
/// A cliff whose symbol is missing is left out.
std::vector<std::unique_ptr<Object>> makeCliffObjects(const Map& map, const std::vector<CliffArea>& cliffs, const AreaSymbol* symbol, const LineSymbol* line_symbol = nullptr);


}  // namespace OpenOrienteering

#endif
