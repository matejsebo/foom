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
 * Point clouds through LASzip.
 *
 * See the design notes, "Reading point clouds and gridding them".
 */
class PointCloud
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::PointCloud)
	
public:
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
};


}  // namespace OpenOrienteering

#endif
