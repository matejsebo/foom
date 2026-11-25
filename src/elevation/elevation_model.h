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


#ifndef OPENORIENTEERING_ELEVATION_MODEL_H
#define OPENORIENTEERING_ELEVATION_MODEL_H

#include <memory>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace OpenOrienteering {


/**
 * One elevation grid: square cells of equal size, the northern row first,
 * values in metres as float, void cells NaN.
 *
 * Coordinates are the projected coordinates of the grid's system, x east and
 * y north; the extent is the outer edge of the cells. The grid is read and
 * written through GDAL, and in the .ocdDem layout by its own code.
 * See the design notes, "Core classes".
 */
class ElevationModel
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::ElevationModel)

public:
	/// How values between cell centres are found.
	enum class Interpolation
	{
		Bilinear,
		Bicubic
	};
	
	struct OpenOptions
	{
		bool feet_to_metres = false;          ///< Multiply the heights by 0.3048.
		bool millimetres_to_metres = false;   ///< Divide the heights by 1000.
		bool shift_below_sea_level = false;   ///< Raise the grid so that its minimum is 0 when it is negative.
		QString target_crs;                   ///< When set and different from the file's system, the grid is warped into it on load.
	};

	struct Info
	{
		double minimum = 0.0;
		double maximum = 0.0;
		qint64 void_count = 0;
	};

	/**
	 * Constructs a grid of width by height cells with the given top-left
	 * corner (left edge, top edge) and cell size.
	 *
	 * Values are row-major, northern row first; an empty vector gives a void grid.
	 */
	ElevationModel(int width, int height, const QPointF& top_left, double cell_size, std::vector<float> values = {}, QString crs_spec = {});

	/**
	 * Opens any raster GDAL reads, or an .ocdDem grid.
	 *
	 * Returns nullptr on failure; lastError() tells why.
	 */
	static std::unique_ptr<ElevationModel> open(const QString& path, const OpenOptions& options);
	static std::unique_ptr<ElevationModel> open(const QString& path) { return open(path, OpenOptions()); }

	/// The message of the last failed open() or save().
	static QString lastError();

	/// A summary of a GeoTIFF's data type, compression and tiling, for diagnostics and tests.
	static QString geoTiffMetadata(const QString& path);

	/// Returns true when the two coordinate reference system specifications name the same system.
	static bool sameCrs(const QString& a, const QString& b);

	/**
	 * foom: The units of a coordinate reference system, for data which are not in metres
	 * (the State Plane systems of the United States in feet).
	 * See the design notes, "State Plane and feet".
	 */
	struct LinearUnits
	{
		double horizontal = 1.0;      ///< Metres in a unit of the plane coordinates.
		double vertical = 1.0;        ///< Metres in a unit of the heights: the vertical system's when the system names one, else the horizontal unit.
		bool has_vertical = false;    ///< The system names a vertical system.
		QString metric_crs;           ///< The same projection with the metre as the unit of its plane coordinates, as WKT; empty when the unit is the metre or the system is not projected.
		bool isMetric() const { return horizontal == 1.0 && vertical == 1.0; }
	};
	static LinearUnits linearUnits(const QString& crs_spec);

	/// foom: Metres in a unit of heights which GDAL names (a band's unit type: "ft", "US survey foot", ...); 1 for metres and for names not known.
	static double metresPerHeightUnit(const QString& unit_name);


	int width() const { return grid_width; }
	int height() const { return grid_height; }

	/// The outer edge of the cells; QRectF::top() is the southern edge.
	QRectF extent() const;

	double cellSize() const { return cell; }

	/// The coordinate reference system, as PROJ string or WKT; empty when unknown.
	const QString& crsSpec() const { return crs; }
	void setCrsSpec(const QString& spec) { crs = spec; }

	float value(int col, int row) const { return data[index(col, row)]; }
	void setValue(int col, int row, float value) { data[index(col, row)] = value; }
	const std::vector<float>& values() const { return data; }
	std::vector<float>& values() { return data; }

	/// The projected coordinates of the centre of the cell.
	QPointF cellCenter(int col, int row) const;

	/// Finds the cell holding the point; returns false outside the extent.
	bool cellAt(const QPointF& projected, int& col, int& row) const;

	/// Bilinear interpolation between cell centres; NaN outside the extent or over a void cell.
	double at(const QPointF& projected) const;

	/**
	 * Keys' cubic convolution (a = -0.5) over the 4 by 4 cell centres around the point;
	 * the bilinear value when a neighbour with a weight is void; NaN outside the extent.
	 * See the design notes, "Bicubic resampling for hill shading".
	 */
	double atBicubic(const QPointF& projected) const;

	/// Horn's 3 by 3 gradient (dz/dx, dz/dy) of the cell holding the point; NaN components when undefined.
	QPointF gradient(const QPointF& projected) const;

	/// Horn's 3 by 3 gradient of a cell, the neighbours clamped to the grid.
	QPointF gradientAt(int col, int row) const;
	
	/**
	 * The gradient (dz/dx, dz/dy) at the corner shared by the cells (col, row) and (col + 1, row + 1):
	 * the mean of the two differences along each axis; NaN components when one of the cells is void.
	 * See the design notes, "The gradient on the corners of 2 by 2 cells".
	 */
	QPointF cornerGradientAt(int col, int row) const;

	Info info() const;

	/**
	 * Saves the grid in the format named by the extension:
	 * .asc (ESRI ASCII grid), .xyz, .ocdDem, otherwise GeoTIFF (float32, DEFLATE, tiled).
	 */
	bool save(const QString& path) const;

	/**
	 * Saves the grid as ESRI ASCII grids of points by points cells,
	 * `<base>_<row>_<column>.asc` from the
	 * north-west, neighbouring tiles sharing their edge row or column.
	 *
	 * Returns the files written, or an empty list on failure (lastError()).
	 */
	QStringList saveAsciiTiles(const QString& path, int points = 1001) const;


	/// One pass of the 3 by 3 binomial kernel per unit of radius; border cells stay unchanged.
	std::unique_ptr<ElevationModel> smoothed(int radius = 1) const;

	/// The cells which intersect the box.
	std::unique_ptr<ElevationModel> cropped(const QRectF& box) const;

	/// The same extent at another cell size, values by bilinear or bicubic interpolation.
	std::unique_ptr<ElevationModel> resampled(double cell_size, Interpolation interpolation = Interpolation::Bilinear) const;

	/// A mosaic of grids of equal cell size on the first grid's lattice; later grids win in overlaps.
	static std::unique_ptr<ElevationModel> merge(const std::vector<const ElevationModel*>& grids);

	/// Upper minus lower over the overlap, on the upper grid's lattice.
	static std::unique_ptr<ElevationModel> difference(const ElevationModel& upper, const ElevationModel& lower);


private:
	std::size_t index(int col, int row) const { return std::size_t(row) * std::size_t(grid_width) + std::size_t(col); }

	static std::unique_ptr<ElevationModel> openGdal(const QString& path, const QString& target_crs);
	bool heights_converted = false;  ///< foom: openGdal() turned the heights into metres by the band's unit
	static std::unique_ptr<ElevationModel> openOcdDem(const QString& path);
	bool saveGeoTiff(const QString& path) const;
	bool saveAsciiGrid(const QString& path) const;
	bool saveXyz(const QString& path) const;
	bool saveOcdDem(const QString& path) const;

	int grid_width;
	int grid_height;
	QPointF top_left;
	double cell;
	std::vector<float> data;
	QString crs;
};


}  // namespace OpenOrienteering

#endif
