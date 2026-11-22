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

#ifndef OPENORIENTEERING_ELEVATION_COMMANDS_H
#define OPENORIENTEERING_ELEVATION_COMMANDS_H

#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"

namespace OpenOrienteering {

class Map;
class Object;
struct GriddingParams;
struct HypsometricParams;
struct LandformParams;
struct PointCloudProducts;
struct RasterProduct;
struct VegetationHeightOptions;


/**
 * The commands of the Elevation menu on a map, without asking.
 *
 * The editor's commands and the elevation wizard both call these, so that
 * neither holds a copy of the other (the design notes,
 * "Wizard"). map_path is the map's file, or empty while it is unsaved; the
 * files are then written beside the grid or the first input.
 */
namespace ElevationCommands {

/**
 * Opens a grid in the map's system and makes it the kind's grid (dem-open).
 *
 * With a unit option on, the converted grid is saved beside the map as
 * `<map>_<kind>.tif`, which becomes the href; otherwise the href is the file.
 */
bool importGrid(Map& map, const QString& map_path, const QString& path, ElevationSet::Kind kind, ElevationModel::OpenOptions options, QString* href, QString* error);

/**
 * Opens the grids with the options and merges them, later grids winning,
 * into the terrain grid saved as `<map>_terrain.tif` (dem-merge).
 */
bool importMergedGrids(Map& map, const QString& map_path, const QStringList& paths, ElevationModel::OpenOptions options, QString* error);

/**
 * Saves the grid beside the map as `<map>_<kind>.tif` (named after the
 * kind's grid, or the terrain grid, while the map is unsaved) and makes it the
 * kind's grid.
 */
bool storeGrid(Map& map, const QString& map_path, ElevationSet::Kind kind, std::unique_ptr<ElevationModel> grid, QString* error);

/**
 * Grids the point clouds into the map's terrain and surface models and
 * makes the cloud's products (dem-point-cloud), attached as templates when
 * asked; grids and rasters receive the paths of the files written.
 *
 * Grids which do not fit under params.memory_limit are written to disk as
 * `<map>_DTM.tif` and `<map>_DSM.tif` and set by href; params.progress
 * reports the rows of tiles and may cancel, which removes the files written
 * and leaves the map unchanged (the design notes,
 * "Grids on disk, a tile cache, and a progress bar").
 */
bool importPointClouds(Map& map, const QString& map_path, const QStringList& paths, const GriddingParams& params, const PointCloudProducts& products, bool attach, QStringList* grids, QStringList* rasters, QString* error);

/**
 * Writes a product of the terrain grid as `<map>_<suffix>.tif` and attaches
 * it when asked; returns its path, empty on failure.
 */
QString writeTerrainProduct(Map& map, const QString& map_path, const RasterProduct& product, const QString& suffix, bool attach, QString* error);

/// The hypsometric map of the terrain grid (dem-hypsometric).
QString writeHypsometricMap(Map& map, const QString& map_path, const HypsometricParams& params, bool attach, QString* error);

/// The vegetation height raster of the map's grids (dem-vegetation-height).
QString writeVegetationHeightMap(Map& map, const QString& map_path, const VegetationHeightOptions& options, bool attach, QString* error);


/**
 * The objects of feature extraction (dem-features).
 */
struct Features
{
	std::vector<std::unique_ptr<Object>> objects;   ///< Knolls and depressions, then cliffs, then trees.
	int knolls = 0;
	int depressions = 0;
	int cliffs = 0;
	int trees = 0;
	bool trees_without_surface = false;              ///< Trees were asked for, but the map has no surface grid.
};

/**
 * Finds the knolls, small depressions, cliffs and trees of the map's grids
 * and makes their objects, not yet added to the map.
 */
Features findFeatures(Map& map, const LandformParams& params);

}  // namespace ElevationCommands

}  // namespace OpenOrienteering

#endif
