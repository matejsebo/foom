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


#ifndef OPENORIENTEERING_ELEVATION_RASTER_PRODUCTS_H
#define OPENORIENTEERING_ELEVATION_RASTER_PRODUCTS_H

#include <limits>
#include <vector>

#include <QImage>
#include <QRectF>
#include <QString>

namespace OpenOrienteering {

class ElevationModel;
class Map;
class Template;


/**
 * Where the gradient of a raster product is taken, see the design notes,
 * "The gradient on the corners of 2 by 2 cells".
 */
enum class GradientMethod
{
	Horn,       ///< Horn's 3 by 3 at the cell centres; the product has the grid's cells.
	Corners     ///< 2 by 2 at the interior corners; the product has one pixel per corner.
};


/**
 * Hill shading parameters, see the design notes, "Raster products".
 */
struct HillShadeParams
{
	enum Method
	{
		SlopeShading,   ///< 1 - slope / 90.
		Oblique,        ///< cos(zen) cos(slope) + sin(zen) sin(slope) cos(azimuth - aspect).
		Combined        ///< Oblique times slope shading.
	};

	Method method = Oblique;
	bool multi_directional = true;    ///< Four lights at 225, 270, 315 and 360 degrees weighted by sin^2(aspect - azimuth); the default since 2026-11-23.
	double azimuth_deg = 315.0;       ///< Light direction, clockwise from north.
	double elevation_deg = 45.0;      ///< Sun elevation.
	double exaggeration = 4.0;        ///< Multiplies the gradient.
	double cell_size = 0.0;           ///< Output cell size; 0 for the grid's.
	
	/// How the grid is resampled to another output cell size.
	enum Resampling
	{
		Bilinear,
		Bicubic         ///< See "Bicubic resampling for hill shading".
	};
	Resampling resampling = Bicubic;
	GradientMethod gradient = GradientMethod::Corners;  ///< The lattice of corners.
};


/**
 * Slope gradient parameters.
 */
struct SlopeParams
{
	bool binary = false;              ///< White below the threshold, black above; else grey from white to darkest at the threshold.
	double darkest = 0.2;             ///< The grey at and above the threshold of the continuous raster (0 black, 1 white); dark grey since 2026-11-23.
	double threshold_deg = 45.0;
	double cell_size = 0.0;
	GradientMethod gradient = GradientMethod::Corners;  ///< The lattice of corners.
};


/**
 * A raster with its position: Grayscale8 (255 white; void cells black), Indexed8 with its
 * colour table, or RGB32.
 */
struct RasterProduct
{
	QImage image;
	QRectF extent;
	QString crs_spec;
	QString error;
};


RasterProduct hillShade(const ElevationModel& grid, const HillShadeParams& params);

RasterProduct slopeRaster(const ElevationModel& grid, const SlopeParams& params);

/**
 * Hypsometric map parameters, see the design notes, "Hypsometric map".
 */
struct HypsometricParams
{
	/// The ramps, named from high to low.
	enum Ramp
	{
		Greyscale,
		RedGreenBlue,
		RedYellowGreen,
		RedYellowBlue,
		BrownYellowGreen,
		BrownYellow,
		RedYellow,
		GreyscaleInterval
	};
	
	Ramp ramp = BrownYellowGreen;
	double interval = 100.0;          ///< m, for GreyscaleInterval.
	bool white_at_zero = false;       ///< Cells of height 0 white.
	/// Legacy mode: other colours, and for GreyscaleInterval steps of
	/// one grey per interval from black upwards in place of black to white in each.
	bool legacy = false;
	/// The heights of the ramp's ends; NaN takes the grid's lowest and highest.
	double range_from = std::numeric_limits<double>::quiet_NaN();
	double range_to = std::numeric_limits<double>::quiet_NaN();
};

/// Colours the heights from the lowest to the highest cell through the ramp; RGB32, void cells black.
RasterProduct hypsometricRaster(const ElevationModel& grid, const HypsometricParams& params);


/**
 * A class of the vegetation height raster: heights from..to coloured from from_color to to_color.
 */
struct HeightClass
{
	double from;
	double to;
	QRgb from_color;
	QRgb to_color;
};

/// The default classes, see the design notes.
std::vector<HeightClass> defaultHeightClasses();

/**
 * Colours the heights of vegetation (surface model minus terrain model) through the classes.
 *
 * Heights below the first class take its first colour, above the last its last colour;
 * void cells are black. The image is RGB32.
 */
RasterProduct vegetationHeightRaster(const ElevationModel& heights, const std::vector<HeightClass>& classes);

/**
 * The vegetation height raster in grey or in colour classes.
 *
 * See the design notes, "Vegetation height in grey and its classes in files".
 */
struct VegetationHeightOptions
{
	enum GreyMapping
	{
		Linear,              ///< 255 (1 - t)
		QuadraticNegative,   ///< 255 (1 - t)^2
		QuadraticPositive,   ///< 255 (1 - t^2)
	};
	
	bool greyscale = false;
	GreyMapping mapping = Linear;
	double max_height = 30.0;          ///< t = height / max_height, clamped to 0..1.
	std::vector<HeightClass> classes = defaultHeightClasses();
};

/// The heights in grey, white at the ground; Grayscale8, void cells black.
RasterProduct vegetationHeightGreyscale(const ElevationModel& heights, VegetationHeightOptions::GreyMapping mapping, double max_height);

/// vegetationHeightRaster() or vegetationHeightGreyscale() as the options say.
RasterProduct vegetationHeightProduct(const ElevationModel& heights, const VegetationHeightOptions& options);

/**
 * Writes height classes as text: "from;to;from_colour;to_colour", then one class per line.
 */
bool saveHeightClasses(const QString& path, const std::vector<HeightClass>& classes, QString* error = nullptr);

/**
 * Reads height classes written by saveHeightClasses(); classes is left unchanged on failure.
 */
bool loadHeightClasses(const QString& path, std::vector<HeightClass>& classes, QString* error = nullptr);

/// Writes the product as a GeoTIFF (DEFLATE, tiled): one Byte band, with the palette for Indexed8; three for RGB32.
bool writeGeoTiff(const RasterProduct& product, const QString& path, QString* error = nullptr);

/**
 * Attaches a written product to the map as a background template.
 *
 * The template is georeferenced when the map is and the file carries a system;
 * otherwise it is placed by its extent. Returns nullptr on failure.
 */
Template* attachProductTemplate(Map& map, const QString& path, QString* error = nullptr);

/**
 * The path of a product file: beside the map, or beside the grid when the map
 * is unsaved, named after the map (or grid) plus the suffix, with .tif.
 */
QString productPath(const QString& map_path, const QString& grid_path, const QString& suffix);


}  // namespace OpenOrienteering

#endif
