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

#ifndef FOOM_ELEVATION_LANDFORMS_H
#define FOOM_ELEVATION_LANDFORMS_H

#include <memory>
#include <vector>

#include <QPointF>

namespace OpenOrienteering {

class ElevationModel;
class Map;
class Object;
class PointSymbol;


/**
 * Knolls and small depressions found in a terrain model.
 *
 * See the design notes, "Knolls and small depressions".
 */
struct LandformParams
{
	bool knolls = true;
	double min_height = 1.0;      ///< m
	bool depressions = true;
	double min_depth = 1.0;       ///< m
	double max_area = 2000.0;     ///< m2: the widest region about a summit or pit in which its height or depth must be reached.
	bool cliffs = false;          ///< Also cliffs from the slope gradient, see cliffs.h (MapEditorController::extractLandforms).
	double cliff_threshold = 45.0; ///< The slope threshold, degrees.
	int cliff_min_area = 3;       ///< Pixels.
	int cliff_min_length = 3;     ///< Pixels.
	bool trees = false;           ///< Also trees from the vegetation heights, see trees.h (MapEditorController::extractLandforms).
	double tree_min_height = 4.0; ///< m.
};

struct Landform
{
	enum Kind { Knoll, Depression };
	Kind kind = Knoll;
	QPointF projected;            ///< The centre of the cell, in the grid's coordinates.
	double size = 0.0;            ///< The prominence of a knoll, the depth of a depression to its spill point, m.
};

/**
 * Filters the grid once with a Gaussian and takes every cell which
 * is higher (lower) than its eight neighbours and at least the minimum above
 * the lowest (below the highest) cell of a ring around it.
 */
std::vector<Landform> findLandforms(const ElevationModel& grid, const LandformParams& params);


struct LandformSymbols
{
	const PointSymbol* knoll = nullptr;
	const PointSymbol* depression = nullptr;
};

/** Finds the point symbols 109 and 111, or makes them in the map's brown. */
LandformSymbols landformSymbols(Map& map);

/** Point objects of the features in the map's coordinates, with the tags feature and size. */
std::vector<std::unique_ptr<Object>> makeLandformObjects(const Map& map, const std::vector<Landform>& landforms, const LandformSymbols& symbols);


}  // namespace OpenOrienteering

#endif
