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

#ifndef OPENORIENTEERING_POINT_CLOUD_H
#define OPENORIENTEERING_POINT_CLOUD_H

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "elevation/raster_products.h"

namespace OpenOrienteering {

class ElevationModel;


/**
 * One point of a LAS or LAZ file, in the file's coordinates.
 */
struct CloudPoint
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	quint16 intensity = 0;
	quint8 return_number = 1;
	quint8 number_of_returns = 1;
	quint8 classification = 0;
};


/**
 * The summary of one or several clouds.
 */
struct PointCloudInfo
{
	qint64 count = 0;
	QRectF extent;          ///< left, bottom, width, height of the points
	double z_min = 0.0;
	double z_max = 0.0;
	double density = 0.0;   ///< points per square metre of the extent
	std::array<qint64, 256> per_class = {};
	QString crs_spec;       ///< of the first file which has one
	QString error;
};


/**
 * The settings of gridding.
 */
struct GriddingParams
{
	double cell = 1.0;
	QRectF box;             ///< left, bottom, width, height; empty for all points
	bool dtm = true;
	bool dsm = true;
	int fill_cells = 10;    ///< reach of the void filling, in cells
};


struct GriddingResult
{
	std::unique_ptr<ElevationModel> dtm;
	std::unique_ptr<ElevationModel> dsm;
	QString error;
	
	GriddingResult();
	GriddingResult(GriddingResult&&);
	GriddingResult& operator=(GriddingResult&&);
	~GriddingResult();
};


/**
 * The settings of the vegetation base map.
 *
 * See the design notes, "Vegetation height and vegetation base map".
 */
struct VegetationParams
{
	double cell = 1.0;
	double undergrowth_from = 0.3;   ///< Height band above the terrain model, m.
	double undergrowth_to = 1.3;
	int undergrowth_kernel = 7;      ///< Cells per side.
	std::array<double, 2> undergrowth_thresholds = {{ -0.80, -0.60 }};
	double vegetation_from = 0.3;
	double vegetation_to = 3.0;
	int vegetation_kernel = 5;
	std::array<double, 3> vegetation_thresholds = {{ -0.60, -0.30, -0.10 }};
	double open_height = 0.1;        ///< A cell without a return this high above the terrain model is open.
	bool no_data_red = true;         ///< Otherwise white.
};


/**
 * The products made with an import of point clouds.
 */
struct PointCloudProducts
{
	bool vegetation_height = false;
	bool vegetation_map = false;
	VegetationParams vegetation;
	bool feature_map = false;            ///< See point_cloud_products.h.
	double feature_from = 0.0;           ///< The feature map's band, m above the terrain model.
	double feature_to = 2.0;
	bool intensity_map = false;
	bool classification_map = false;
};


/**
 * Point clouds through LASzip.
 *
 * See the design notes, "Reading point clouds and gridding them".
 */
class PointCloud
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::PointCloud)
	
public:
	/// The classes of the vegetation base map, as indices of the palette.
	enum VegetationClass
	{
		NoData = 0,
		Open = 1,
		Runnable = 2,
		Slow = 3,
		Walk = 4,
		Fight = 5,
		UndergrowthDense = 6,
		OpenRough = 7,          ///< In the palette; not assigned.
		UndergrowthSome = 8,
	};
	
	/// False when FOOM was built without LASzip.
	static bool supported();
	
	/**
	 * Reads every point of a .las or .laz file.
	 *
	 * crs receives the file's system, or an empty string.
	 */
	static bool read(const QString& path, const std::function<void (const CloudPoint&)>& receiver, QString* crs, QString* error);
	
	/**
	 * Writes the points in point format 1 with a scale of 0.01, compressed when the extension is .laz.
	 *
	 * A non-empty wkt is written as a WKT record, a positive epsg as a GeoTIFF key record.
	 */
	static bool write(const QString& path, const std::vector<CloudPoint>& points, const QString& wkt, int epsg, QString* error);
	
	static PointCloudInfo analyse(const QStringList& paths);
	
	static GriddingResult grid(const QStringList& paths, const GriddingParams& params);
	
	/**
	 * The vegetation base map of the points over the terrain model, on the model's extent.
	 * 
	 * An Indexed8 image of VegetationClass values with the palette.
	 */
	static RasterProduct vegetationBaseMap(const QStringList& paths, const ElevationModel& dtm, const VegetationParams& params);
};


}  // namespace OpenOrienteering

#endif
