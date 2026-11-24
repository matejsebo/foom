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

#include "elevation_commands.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "elevation/cliffs.h"
#include "elevation/grid_tiles.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "elevation/point_cloud_products.h"
#include "elevation/raster_products.h"
#include "elevation/trees.h"

namespace OpenOrienteering {

namespace ElevationCommands {

namespace {

/// The editor's translation context, which these messages had before they moved here.
QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::MapEditorController", text);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

QString mapCrs(const Map& map)
{
	return map.getGeoreferencing().getProjectedCRSSpec();
}

/// Writes the product and attaches it when asked; the message on failure.
QString writeProduct(Map& map, const RasterProduct& product, const QString& path, bool attach)
{
	QString message = product.error;
	if (message.isEmpty() && !writeGeoTiff(product, path, &message))
		message = tr("Cannot write %1: %2").arg(path, message);
	if (message.isEmpty() && attach && !attachProductTemplate(map, path, &message))
		message = tr("Cannot attach %1: %2").arg(path, message);
	return message;
}

}  // namespace


bool importGrid(Map& map, const QString& map_path, const QString& path, ElevationSet::Kind kind, ElevationModel::OpenOptions options, QString* href, QString* error)
{
	options.target_crs = mapCrs(map);
	auto grid = ElevationModel::open(path, options);
	if (!grid)
	{
		setError(error, ElevationModel::lastError());
		return false;
	}
	auto grid_href = path;
	if (options.feet_to_metres || options.millimetres_to_metres || options.shift_below_sea_level)
	{
		// The converted grid is kept beside the map, so that it reloads as imported.
		grid_href = productPath(map_path, path, ElevationSet::kindName(kind));
		if (!grid->save(grid_href))
		{
			setError(error, ElevationModel::lastError());
			return false;
		}
	}
	map.elevationSet().setModel(kind, std::move(grid), grid_href);
	map.setHasUnsavedChanges(true);
	if (href)
		*href = grid_href;
	return true;
}


bool importMergedGrids(Map& map, const QString& map_path, const QStringList& paths, ElevationModel::OpenOptions options, QString* error)
{
	if (paths.isEmpty())
	{
		setError(error, tr("No elevation grid was chosen."));
		return false;
	}
	options.target_crs = mapCrs(map);
	std::vector<std::unique_ptr<ElevationModel>> opened;
	std::vector<const ElevationModel*> grids;
	for (auto const& path : paths)
	{
		auto grid = ElevationModel::open(path, options);
		if (!grid)
		{
			setError(error, ElevationModel::lastError());
			return false;
		}
		grids.push_back(grid.get());
		opened.push_back(std::move(grid));
	}
	auto merged = ElevationModel::merge(grids);
	if (!merged)
	{
		setError(error, ElevationModel::lastError());
		return false;
	}
	auto const path = productPath(map_path, paths.front(), ElevationSet::kindName(ElevationSet::Terrain));
	if (!merged->save(path))
	{
		setError(error, ElevationModel::lastError());
		return false;
	}
	map.elevationSet().setModel(ElevationSet::Terrain, std::move(merged), path);
	map.setHasUnsavedChanges(true);
	return true;
}


bool storeGrid(Map& map, const QString& map_path, ElevationSet::Kind kind, std::unique_ptr<ElevationModel> grid, QString* error)
{
	auto& set = map.elevationSet();
	auto base = set.href(kind);
	if (base.isEmpty())
		base = set.href(ElevationSet::Terrain);
	auto const path = productPath(map_path, base, ElevationSet::kindName(kind));
	if (!grid->save(path))
	{
		setError(error, ElevationModel::lastError());
		return false;
	}
	set.setModel(kind, std::move(grid), path);
	map.setHasUnsavedChanges(true);
	return true;
}


// the design notes, "Reading point clouds and gridding them".
bool importPointClouds(Map& map, const QString& map_path, const QStringList& paths, const GriddingParams& params, const PointCloudProducts& products, bool attach, QStringList* grids, QStringList* rasters, QString* error)
{
	if (paths.isEmpty())
	{
		setError(error, tr("No point cloud was chosen."));
		return false;
	}
	// the design notes, "Grids on disk, a tile cache, and a progress bar":
	// a cancelled run removes what it wrote and leaves the map unchanged.
	bool cancelled = false;
	TileProgress progress;
	if (params.progress)
	{
		progress = [&params, &cancelled](const QString& step, int done, int total) {
			auto const go_on = params.progress(step, done, total);
			cancelled = cancelled || !go_on;
			return go_on;
		};
	}
	QStringList written;
	std::unique_ptr<GridTileCache> dtm_cache, dsm_cache;
	auto const fail = [&](const QString& message) {
		if (cancelled)
		{
			// Closed first: Windows does not remove an open file.
			dtm_cache.reset();
			dsm_cache.reset();
			for (auto const& path : written)
				QFile::remove(path);
		}
		setError(error, message);
		return false;
	};

	auto grid_params = params;
	grid_params.dtm = params.dtm || products.vegetation_height || products.vegetation_map
	                  || products.feature_map || products.intensity_map || products.classification_map;
	grid_params.dsm = params.dsm || products.vegetation_height;
	// Named before gridding, so that the grids can be written on disk.
	grid_params.dtm_path = productPath(map_path, paths.front(), QStringLiteral("DTM"));
	grid_params.dsm_path = productPath(map_path, paths.front(), QStringLiteral("DSM"));
	grid_params.progress = progress;
	auto result = PointCloud::grid(paths, grid_params);
	if (!result.error.isEmpty())
		return fail(result.error);
	auto const on_disk = !result.dtm_path.isEmpty() || !result.dsm_path.isEmpty();
	if (on_disk)
	{
		for (auto const* path : { &result.dtm_path, &result.dsm_path })
		{
			if (!path->isEmpty())
				written.push_back(*path);
		}
		QString message;
		if (!result.dtm_path.isEmpty() && !(dtm_cache = GridTileCache::open(result.dtm_path, 0, &message)))
			return fail(message);
		if (!result.dsm_path.isEmpty() && !(dsm_cache = GridTileCache::open(result.dsm_path, 0, &message)))
			return fail(message);
	}
	auto const has_dtm = bool(result.dtm) || bool(dtm_cache);
	auto const has_dsm = bool(result.dsm) || bool(dsm_cache);

	auto const target_crs = mapCrs(map);
	QStringList grid_paths;
	QStringList raster_paths;
	// The products, from the grids in the clouds' system; attached at the end.
	auto const product = [&](const RasterProduct& raster, const QString& suffix) {
		if (!raster.error.isEmpty())
			return fail(raster.error);
		auto const path = productPath(map_path, paths.front(), suffix);
		QString message;
		if (!writeGeoTiff(raster, path, &message))
			return fail(message);
		written.push_back(path);
		raster_paths.push_back(path);
		return true;
	};
	auto const file_product = [&](const QString& suffix, const std::function<bool (const QString&, QString*)>& write) {
		auto const path = productPath(map_path, paths.front(), suffix);
		QString message;
		if (!write(path, &message))
			return fail(message);
		written.push_back(path);
		raster_paths.push_back(path);
		return true;
	};
	if (products.vegetation_height && has_dtm && has_dsm)
	{
		if (on_disk)
		{
			if (!file_product(QStringLiteral("vegetation_height"), [&](const QString& path, QString* message) {
				return vegetationHeightToFile(*dsm_cache, *dtm_cache, VegetationHeightOptions(), path, grid_params.tile_size, progress, message);
			}))
				return false;
		}
		else
		{
			auto const heights = ElevationModel::difference(*result.dsm, *result.dtm);
			if (!heights)
				return fail(tr("The surface and terrain models do not overlap."));
			if (!product(vegetationHeightRaster(*heights, defaultHeightClasses()), QStringLiteral("vegetation_height")))
				return false;
		}
	}
	if (products.vegetation_map && has_dtm)
	{
		if (on_disk)
		{
			if (!file_product(QStringLiteral("vegetation"), [&](const QString& path, QString* message) {
				return PointCloud::vegetationBaseMapToFile(paths, *dtm_cache, products.vegetation, path, progress, message);
			}))
				return false;
		}
		else if (!product(PointCloud::vegetationBaseMap(paths, *result.dtm, products.vegetation, progress), QStringLiteral("vegetation")))
		{
			return false;
		}
	}
	// "Feature map, intensity and classification maps".
	if (products.feature_map && has_dtm)
	{
		FeatureMapParams features;
		features.cell = params.cell;
		features.band_from = products.feature_from;
		features.band_to = products.feature_to;
		features.legacy = products.feature_legacy;
		if (!product(on_disk ? featureMap(paths, *dtm_cache, features) : featureMap(paths, *result.dtm, features), QStringLiteral("features")))
			return false;
	}
	if (products.intensity_map && has_dtm
	    && !product(on_disk ? intensityMap(paths, *dtm_cache, params.cell) : intensityMap(paths, *result.dtm, params.cell), QStringLiteral("intensity")))
		return false;
	if (products.classification_map && has_dtm
	    && !product(on_disk ? classificationMap(paths, *dtm_cache, params.cell) : classificationMap(paths, *result.dtm, params.cell), QStringLiteral("classes")))
		return false;

	// The grids become the map's.
	auto const store = [&](std::unique_ptr<ElevationModel> grid, ElevationSet::Kind kind, const QString& suffix) {
		if (!grid)
			return true;
		auto const href = productPath(map_path, paths.front(), suffix);
		if (!grid->save(href))
		{
			setError(error, ElevationModel::lastError());
			return false;
		}
		if (!grid->crsSpec().isEmpty() && !target_crs.isEmpty() && !ElevationModel::sameCrs(grid->crsSpec(), target_crs))
		{
			// Warped into the map's system, as an imported grid is.
			ElevationModel::OpenOptions options;
			options.target_crs = target_crs;
			grid = ElevationModel::open(href, options);
			if (!grid)
			{
				setError(error, ElevationModel::lastError());
				return false;
			}
		}
		map.elevationSet().setModel(kind, std::move(grid), href);
		grid_paths.push_back(href);
		return true;
	};
	dtm_cache.reset();
	dsm_cache.reset();
	if (on_disk)
	{
		// A model made only for the products is not kept, as in memory.
		if (!params.dtm && !result.dtm_path.isEmpty())
			QFile::remove(result.dtm_path);
		if (!params.dsm && !result.dsm_path.isEmpty())
			QFile::remove(result.dsm_path);
		// Set by href, loaded (and warped into the map's system) when a command uses them.
		if (params.dtm && !result.dtm_path.isEmpty())
		{
			map.elevationSet().setHref(ElevationSet::Terrain, result.dtm_path);
			grid_paths.push_back(result.dtm_path);
		}
		if (params.dsm && !result.dsm_path.isEmpty())
		{
			map.elevationSet().setHref(ElevationSet::Surface, result.dsm_path);
			grid_paths.push_back(result.dsm_path);
		}
	}
	else
	{
		if (!params.dtm)
			result.dtm.reset();
		if (!params.dsm)
			result.dsm.reset();
		if (!store(std::move(result.dtm), ElevationSet::Terrain, QStringLiteral("DTM"))
		    || !store(std::move(result.dsm), ElevationSet::Surface, QStringLiteral("DSM")))
			return false;
	}
	if (attach)
	{
		for (auto const& path : raster_paths)
		{
			QString message;
			if (!attachProductTemplate(map, path, &message))
			{
				setError(error, tr("Cannot attach %1: %2").arg(path, message));
				return false;
			}
		}
	}
	map.setHasUnsavedChanges(true);
	if (grids)
		*grids = grid_paths;
	if (rasters)
		*rasters = raster_paths;
	return true;
}


QString writeTerrainProduct(Map& map, const QString& map_path, const RasterProduct& product, const QString& suffix, bool attach, QString* error)
{
	auto const path = productPath(map_path, map.elevationSet().href(ElevationSet::Terrain), suffix);
	auto const message = writeProduct(map, product, path, attach);
	if (!message.isEmpty())
	{
		setError(error, message);
		return {};
	}
	return path;
}


// the design notes, "Hypsometric map".
QString writeHypsometricMap(Map& map, const QString& map_path, const HypsometricParams& params, bool attach, QString* error)
{
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, mapCrs(map));
	if (!model)
	{
		setError(error, tr("The map has no terrain grid."));
		return {};
	}
	return writeTerrainProduct(map, map_path, hypsometricRaster(*model, params), QStringLiteral("hypsometric"), attach, error);
}


// the design notes, "Vegetation height from the map's grids".
QString writeVegetationHeightMap(Map& map, const QString& map_path, const VegetationHeightOptions& options, bool attach, QString* error)
{
	auto const crs = mapCrs(map);
	auto& set = map.elevationSet();
	std::unique_ptr<ElevationModel> difference;
	auto const* heights = set.model(ElevationSet::Canopy, crs);
	if (!heights)
	{
		auto const* surface = set.model(ElevationSet::Surface, crs);
		auto const* terrain = set.model(ElevationSet::Terrain, crs);
		if (surface && terrain)
			difference = ElevationModel::difference(*surface, *terrain);
		heights = difference.get();
	}
	if (!heights)
	{
		setError(error, tr("The map needs a canopy grid, or a surface grid and a terrain grid."));
		return {};
	}
	auto base = set.href(ElevationSet::Terrain);
	if (base.isEmpty())
		base = set.href(ElevationSet::Canopy);
	auto const path = productPath(map_path, base, QStringLiteral("vegetation_height"));
	auto const message = writeProduct(map, vegetationHeightProduct(*heights, options), path, attach);
	if (!message.isEmpty())
	{
		setError(error, message);
		return {};
	}
	return path;
}


Features findFeatures(Map& map, const LandformParams& params)
{
	Features features;
	auto const crs = mapCrs(map);
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, crs);
	if (!model)
		return features;
	auto const landforms = findLandforms(*model, params);
	// the design notes, "Cliffs from the slope gradient".
	std::vector<CliffArea> cliffs;
	if (params.cliffs)
	{
		CliffParams cliff_params;
		cliff_params.threshold = params.cliff_threshold;
		cliff_params.min_area = params.cliff_min_area;
		cliff_params.min_length = params.cliff_min_length;
		cliff_params.line_width = params.cliff_line_width;
		cliffs = findCliffs(*model, cliff_params);
	}
	// the design notes, "Trees": from the surface less the terrain.
	std::vector<Tree> trees;
	if (params.trees)
	{
		auto const* surface = map.elevationSet().model(ElevationSet::Surface, crs);
		auto const heights = surface ? ElevationModel::difference(*surface, *model) : nullptr;
		if (heights)
		{
			TreeParams tree_params;
			tree_params.min_height = params.tree_min_height;
			trees = findTrees(*heights, tree_params);
		}
		else
		{
			features.trees_without_surface = true;
		}
	}
	features.knolls = int(std::count_if(landforms.begin(), landforms.end(), [](const Landform& l) { return l.kind == Landform::Knoll; }));
	features.depressions = int(landforms.size()) - features.knolls;
	features.cliffs = int(cliffs.size());
	features.trees = int(trees.size());
	if (!landforms.empty())
		features.objects = makeLandformObjects(map, landforms, landformSymbols(map));
	if (!cliffs.empty())
	{
		auto const has_lines = std::any_of(cliffs.begin(), cliffs.end(), [](const CliffArea& cliff) { return cliff.isLine(); });
		auto const has_areas = std::any_of(cliffs.begin(), cliffs.end(), [](const CliffArea& cliff) { return !cliff.isLine(); });
		auto cliff_objects = makeCliffObjects(map, cliffs, has_areas ? cliffSymbol(map) : nullptr, has_lines ? cliffLineSymbol(map) : nullptr);
		std::move(cliff_objects.begin(), cliff_objects.end(), std::back_inserter(features.objects));
	}
	if (!trees.empty())
	{
		auto tree_objects = makeTreeObjects(map, trees, treeSymbol(map));
		std::move(tree_objects.begin(), tree_objects.end(), std::back_inserter(features.objects));
	}
	return features;
}

}  // namespace ElevationCommands

}  // namespace OpenOrienteering
