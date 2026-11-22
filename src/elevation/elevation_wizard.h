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

#ifndef OPENORIENTEERING_ELEVATION_WIZARD_H
#define OPENORIENTEERING_ELEVATION_WIZARD_H

#include <vector>

#include <QString>
#include <QStringList>

#include "elevation/contours.h"
#include "elevation/elevation_model.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "elevation/raster_products.h"

class QJsonObject;

namespace OpenOrienteering {

class Map;


/**
 * What the elevation wizard makes: its pages' settings, in the option
 * structs of the single commands.
 *
 * See the design notes, "Wizard".
 */
struct ElevationWizardJob
{
	enum Source
	{
		Grid,
		PointCloud
	};

	Source source = Grid;
	QStringList files;                          ///< Grids (merged when several) or point clouds.
	ElevationModel::OpenOptions open_options;   ///< Grids: the units; target_crs is the map's.
	GriddingParams gridding;                    ///< Point clouds: the cell size and the models.

	bool contours = true;
	std::vector<ContourParams> contour_sets = { ContourParams() };
	bool hill_shading = true;
	HillShadeParams hill_shade;
	bool slope = true;
	SlopeParams slope_params;
	bool hypsometric = false;
	HypsometricParams hypsometric_params;

	/// Knolls, depressions and trees; the cliffs, on the slope page, are landforms.cliffs.
	bool features = false;
	LandformParams landforms;

	bool vegetation_height = false;             ///< Point clouds, from the map's grids.
	VegetationHeightOptions vegetation_height_options;
	PointCloudProducts cloud;                   ///< Point clouds: the base map, feature, intensity and classification maps.

	bool attach = true;                         ///< Attach the rasters as templates.

	/// The wizard's defaults for the source.
	static ElevationWizardJob defaults(Source source);

	/// The landform parameters which the run uses: only what the pages chose.
	LandformParams effectiveLandforms() const;

	/// True when the run makes objects.
	bool makesObjects() const;
};


/**
 * The outcome of a run.
 */
struct ElevationWizardResult
{
	bool ok = false;
	QString error;
	QStringList grids;      ///< The grid files which became the map's grids.
	QStringList files;      ///< The rasters written.
	int contours = 0;       ///< Contour objects added.
	int features = 0;       ///< Knolls, depressions, cliffs and trees added.
	int templates = 0;      ///< Templates attached.
};


/**
 * Runs the wizard's job on the map: the grids, the rasters of the cloud,
 * the rasters of the terrain grid, then the objects in one undo step.
 *
 * map_path is the map's file, or empty while it is unsaved. progress reports the
 * rows of tiles of a point cloud and may cancel the run.
 */
ElevationWizardResult runElevationWizard(Map& map, const QString& map_path, const ElevationWizardJob& job, const TileProgress& progress = {});

/**
 * Reads the parameters of the runner step `elevation-wizard` into job,
 * starting from the defaults of its source. The files are taken as written.
 *
 * Returns false with a message for an unknown or ill-typed member or value.
 */
bool elevationWizardJobFromJson(const QJsonObject& object, ElevationWizardJob* job, QString* error);

}  // namespace OpenOrienteering

#endif
