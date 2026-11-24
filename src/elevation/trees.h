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

#ifndef FOOM_ELEVATION_TREES_H
#define FOOM_ELEVATION_TREES_H

#include <memory>
#include <vector>

#include <QPointF>

namespace OpenOrienteering {

class ElevationModel;
class Map;
class Object;
class PointSymbol;


/**
 * Trees found in a grid of vegetation heights.
 *
 * See the design notes, "Trees".
 */
struct TreeParams
{
	double min_height = 4.0;      ///< m, of the filtered heights.
	double merge_distance = 7.5;  ///< m: closer maxima are one tree.
	/// m: a tree whose crown, where it is at least the minimum height, reaches farther than this from it along
	/// east or north is no single tree (7.5 m is one, 8.5 m is none); 0 for no limit.
	double crown_reach = 8.0;
};

struct Tree
{
	QPointF projected;            ///< In the grid's coordinates.
	double height = 0.0;          ///< The highest filtered height of the tree's maxima, m.
};

/**
 * Filters the heights with the filter of 2 cells, takes the local maxima of at least the
 * minimum height, places each between the cells, and joins those closer than the
 * merge distance into one tree at their mean.
 */
std::vector<Tree> findTrees(const ElevationModel& heights, const TreeParams& params);

/** Finds the point symbol 418, or makes it in the map's green. */
const PointSymbol* treeSymbol(Map& map);

/** Point objects of the trees in the map's coordinates, with the tags feature and height. */
std::vector<std::unique_ptr<Object>> makeTreeObjects(const Map& map, const std::vector<Tree>& trees, const PointSymbol* symbol);


}  // namespace OpenOrienteering

#endif
