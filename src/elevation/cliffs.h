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
	int min_area = 3;             ///< Corners of a component, at least.
	int min_length = 3;           ///< The longer side of a component's bounding box, in corners, at least.
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
 * A cliff area: rings in projected coordinates, outlines and holes, and the
 * number of corners whose squares it covers.
 */
struct CliffArea
{
	std::vector<std::vector<QPointF>> rings;
	int pixels = 0;
};


/// The filter: a 3 by 3 Gaussian of one cell's standard deviation; border cells and cells beside a void cell keep their value.
std::unique_ptr<ElevationModel> gaussianFiltered(const ElevationModel& grid);

/// The gradient above which the slope raster is black for the threshold in degrees.
double steepGradient(double threshold);

/// The gradient which the steepest corner of a region of steep corners needs for the region to be a cliff.
double cliffGradient(double threshold);

/// The gradient at every interior corner of the filtered grid, and whether it is steep or in a cliff region.
CliffRaster cliffRaster(const ElevationModel& grid, double threshold);

/// The areas of the components of cliff corners which pass the minimums.
std::vector<CliffArea> cliffAreas(const CliffRaster& raster, const CliffParams& params);

/// cliffAreas() of cliffRaster().
std::vector<CliffArea> findCliffs(const ElevationModel& grid, const CliffParams& params);

/// Finds the area symbol 10080, or makes it in the map's black.
const AreaSymbol* cliffSymbol(Map& map);

/// Area objects of the cliffs in the map's coordinates, with the tag feature = cliff.
std::vector<std::unique_ptr<Object>> makeCliffObjects(const Map& map, const std::vector<CliffArea>& areas, const AreaSymbol* symbol);


}  // namespace OpenOrienteering

#endif
