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

#include "elevation/grid_tiles.h"
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
	/// foom: Metres in a unit of the files' plane coordinates and of their heights, when they are not in metres.
	/// The points, the extent, the heights and crs_spec are in metres then: a cloud in feet is read as the same
	/// projection in metres (the design notes, "State Plane and feet").
	double file_horizontal_unit = 1.0;
	double file_vertical_unit = 1.0;
	QString error;
};


/** Which returns of a pulse go into a model. */
enum class ReturnChoice { First, Last, All };

/**
 * The points which go into a model: their classes and returns.
 *
 * See the design notes, "The points of the models".
 */
struct PointChoice
{
	std::array<bool, 256> classes = {};
	ReturnChoice returns = ReturnChoice::All;
	
	bool accepts(const CloudPoint& p) const
	{
		return classes[p.classification]
		       && (returns == ReturnChoice::All
		           || (returns == ReturnChoice::First ? p.return_number <= 1 : p.return_number >= p.number_of_returns));
	}
	
	/// Ground and water, the last returns.
	static PointChoice terrain();
	/// Every class but the noise (7 and 18), the first returns.
	static PointChoice surface();
	
	/// Ten rows: unclassified (0, 1), ground, low, medium and high vegetation, building, low point, water, overlap, other.
	static constexpr int group_count = 10;
	static int groupOf(int classification);
	/// The name in job files: "unclassified", "ground", "low-vegetation", ...
	static const char* groupId(int group);
	static QString groupName(int group);
	/// Whether a class of the group is chosen.
	bool group(int group) const;
	void setGroup(int group, bool chosen);
};

bool operator==(const PointChoice& lhs, const PointChoice& rhs);
inline bool operator!=(const PointChoice& lhs, const PointChoice& rhs) { return !(lhs == rhs); }


/**
 * The settings of gridding.
 */
struct GriddingParams
{
	double cell = 1.0;
	QRectF box;             ///< left, bottom, width, height; empty for all points
	bool dtm = true;
	bool dsm = true;
	PointChoice dtm_points = PointChoice::terrain();
	PointChoice dsm_points = PointChoice::surface();
	int fill_cells = 50;    ///< reach of the void filling inside the data, in cells each way
	int edge_cells = 10;    ///< reach past the outer edge of the data
	double tile_size = 1000.0;  ///< rows of tiles of this many metres are gridded one at a time
	double tile_buffer = 20.0;  ///< metres read beyond a row of tiles; at least the reach of the void filling
	/// Bytes the gridding may hold; 0 for PointCloud::defaultMemoryLimit().
	/// See the design notes, "Grids on disk, a tile cache, and a progress bar".
	qint64 memory_limit = 0;
	QString dtm_path;           ///< Where the grids are written when they do not fit (or with to_disk).
	QString dsm_path;
	bool to_disk = false;       ///< Write the grids to the paths whatever their size.
	TileProgress progress;      ///< Called per row of tiles; also by the products of an import.
	/// While a row of tiles is read, progress is called again after every so many points, with the numbers
	/// it had last, so that a window stays alive and Cancel is heard (seen 2026-11-23: "not responding").
	qint64 progress_points = 262144;
};


struct GriddingResult
{
	std::unique_ptr<ElevationModel> dtm;
	std::unique_ptr<ElevationModel> dsm;
	QString error;
	int tile_rows = 0;       ///< rows of tiles gridded
	int file_reads = 0;      ///< files read for the rows, the extent pass not counted
	QString dtm_path;        ///< The files written, in the place of the models, when the grids are on disk.
	QString dsm_path;
	bool cancelled = false;
	
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
	double tile_size = 1000.0;       ///< Rows of this many metres are counted one at a time.
	qint64 memory_limit = 0;         ///< Bytes for the counts of a row; 0 for PointCloud::defaultMemoryLimit().
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
	bool feature_legacy = false;         ///< Legacy mode: the objects on the terrain.
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

	/// foom: The system of a file as its records name it, before the conversion to metres; empty without one.
	static QString fileCrs(const QString& path);
	
	/**
	 * Writes the points in point format 1 with a scale of 0.01, compressed when the extension is .laz.
	 *
	 * A non-empty wkt is written as a WKT record, a positive epsg as a GeoTIFF key record.
	 */
	static bool write(const QString& path, const std::vector<CloudPoint>& points, const QString& wkt, int epsg, QString* error);
	
	static PointCloudInfo analyse(const QStringList& paths);
	
	/// The largest grid grid() makes in memory, in cells.
	static qint64 maxCells();
	
	/// The largest grid grid() writes to disk, in cells.
	static qint64 maxDiskCells();
	
	/// The memory ceiling of a memory_limit of 0, in bytes: 2 GiB.
	static qint64 defaultMemoryLimit();
	
	static GriddingResult grid(const QStringList& paths, const GriddingParams& params);
	
	/**
	 * The vegetation base map of the points over the terrain model, on the model's extent.
	 * 
	 * An Indexed8 image of VegetationClass values with the palette.
	 */
	static RasterProduct vegetationBaseMap(const QStringList& paths, const ElevationModel& dtm, const VegetationParams& params, const TileProgress& progress = {});
	
	/// The same, the terrain model read through a tile cache.
	static RasterProduct vegetationBaseMap(const QStringList& paths, const GridTileCache& dtm, const VegetationParams& params, const TileProgress& progress = {});
	
	/// The same, written to a GeoTIFF row of tiles by row of tiles.
	static bool vegetationBaseMapToFile(const QStringList& paths, const GridTileCache& dtm, const VegetationParams& params,
	                                    const QString& path, const TileProgress& progress, QString* error);
};


}  // namespace OpenOrienteering

#endif
