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


#include "raster_products.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringList>
#include <QStringLiteral>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "elevation/elevation_georef.h"
#include "elevation/elevation_model.h"
#include "gdal/gdal_template.h"
#include "templates/template.h"


namespace OpenOrienteering {

namespace {

constexpr double pi = 3.14159265358979323846;

/// Grey level of a shade in 0..1. Void cells are black too.
quint8 grey(double shade)
{
	return quint8(qBound(0, qRound(255.0 * shade), 255));
}

/// A raster product from a per-cell shade function of the gradient (dz/dx, dz/dy).
template <typename Shade>
RasterProduct makeProduct(const ElevationModel& input, double cell_size, ElevationModel::Interpolation interpolation, GradientMethod gradient, Shade shade)
{
	RasterProduct product;
	std::unique_ptr<ElevationModel> resampled;
	auto const* grid = &input;
	if (cell_size > 0.0 && qAbs(cell_size - input.cellSize()) > 1e-9 * input.cellSize())
	{
		resampled = input.resampled(cell_size, interpolation);
		grid = resampled.get();
	}
	product.crs_spec = grid->crsSpec();
	if (gradient == GradientMethod::Corners)
	{
		// the design notes, "The gradient on the corners of 2 by 2 cells":
		// one pixel of the grid's cell size centred on each interior corner.
		if (grid->width() < 2 || grid->height() < 2)
		{
			product.error = ElevationModel::tr("The grid is too small");
			return product;
		}
		auto const half = grid->cellSize() / 2;
		auto const extent = grid->extent();
		product.extent = extent.adjusted(half, half, -half, -half);
		product.image = QImage(grid->width() - 1, grid->height() - 1, QImage::Format_Grayscale8);
		if (product.image.isNull())
		{
			product.error = ElevationModel::tr("The raster is too large");
			return product;
		}
		for (int r = 0; r + 1 < grid->height(); ++r)
		{
			auto* line = product.image.scanLine(r);
			for (int c = 0; c + 1 < grid->width(); ++c)
			{
				auto const g = grid->cornerGradientAt(c, r);
				line[c] = std::isnan(g.x()) || std::isnan(g.y()) ? 0 : grey(shade(g.x(), g.y()));
			}
		}
		return product;
	}
	product.extent = grid->extent();
	product.image = QImage(grid->width(), grid->height(), QImage::Format_Grayscale8);
	if (product.image.isNull())
	{
		product.error = ElevationModel::tr("The raster is too large");
		return product;
	}
	for (int r = 0; r < grid->height(); ++r)
	{
		auto* line = product.image.scanLine(r);
		for (int c = 0; c < grid->width(); ++c)
		{
			if (std::isnan(grid->value(c, r)))
			{
				line[c] = 0;
				continue;
			}
			auto const g = grid->gradientAt(c, r);
			if (std::isnan(g.x()) || std::isnan(g.y()))
			{
				line[c] = 0;
				continue;
			}
			line[c] = grey(shade(g.x(), g.y()));
		}
	}
	return product;
}

double obliqueShade(double slope, double aspect, double azimuth, double zenith)
{
	return std::cos(zenith) * std::cos(slope) + std::sin(zenith) * std::sin(slope) * std::cos(azimuth - aspect);
}


/**
 * A GDAL template which is georeferenced from the start, so that no dialog
 * is needed when a product is attached. It saves and reloads as a GdalTemplate.
 */
class ProductTemplate : public GdalTemplate
{
public:
	ProductTemplate(const QString& path, Map* map, bool georeferenced)
	: GdalTemplate(path, map)
	{
		is_georeferenced = georeferenced;
	}

	/// Places a non-georeferenced product by its extent.
	void placeByExtent(const QRectF& extent)
	{
		auto const georef = elevationGeoreferencing(*map);  // in metres also for a map which counts in feet
		auto const top_left = georef.toMapCoordF(QPointF(extent.left(), extent.bottom()));
		auto const bottom_right = georef.toMapCoordF(QPointF(extent.right(), extent.top()));
		auto const center = 0.5 * (top_left + bottom_right);
		transform.template_scale_x = (bottom_right.x() - top_left.x()) / image.width();
		transform.template_scale_y = (bottom_right.y() - top_left.y()) / image.height();
		transform.template_shear = 0.0;
		transform.template_rotation = 0.0;
		transform.template_x = qRound(center.x() * 1000);
		transform.template_y = qRound(center.y() * 1000);
		updateTransformationMatrices();
	}

protected:
	ProductTemplate(const ProductTemplate& proto) = default;

	ProductTemplate* duplicate() const override
	{
		return new ProductTemplate(*this);
	}
};

}  // namespace



RasterProduct hillShade(const ElevationModel& grid, const HillShadeParams& params)
{
	auto const zenith = qDegreesToRadians(90.0 - params.elevation_deg);
	auto const azimuth = qDegreesToRadians(params.azimuth_deg);
	auto const exaggeration = qMax(params.exaggeration, 1e-9);
	auto const method = params.method;
	auto const multi = params.multi_directional;
	auto const interpolation = params.resampling == HillShadeParams::Bicubic ? ElevationModel::Interpolation::Bicubic : ElevationModel::Interpolation::Bilinear;
	return makeProduct(grid, params.cell_size, interpolation, params.gradient, [=](double dzdx, double dzdy) {
		auto const slope = std::atan(exaggeration * std::hypot(dzdx, dzdy));
		auto const slope_shading = 1.0 - slope / (pi / 2);
		if (method == HillShadeParams::SlopeShading)
			return slope_shading;
		// Aspect: the azimuth of the downhill direction, clockwise from north.
		auto const aspect = std::atan2(-dzdx, -dzdy);
		double oblique;
		if (multi)
		{
			double sum = 0.0, weights = 0.0;
			for (auto const az_deg : { 225.0, 270.0, 315.0, 360.0 })
			{
				auto const az = qDegreesToRadians(az_deg);
				auto const s = std::sin(aspect - az);
				auto const w = s * s;
				sum += w * obliqueShade(slope, aspect, az, zenith);
				weights += w;
			}
			oblique = weights > 1e-12 ? sum / weights : obliqueShade(slope, aspect, azimuth, zenith);
		}
		else
		{
			oblique = obliqueShade(slope, aspect, azimuth, zenith);
		}
		oblique = qBound(0.0, oblique, 1.0);
		return method == HillShadeParams::Combined ? oblique * slope_shading : oblique;
	});
}


RasterProduct slopeRaster(const ElevationModel& grid, const SlopeParams& params)
{
	auto const threshold = qDegreesToRadians(qMax(params.threshold_deg, 1e-9));
	auto const binary = params.binary;
	auto const darkest = qBound(0.0, params.darkest, 1.0);
	return makeProduct(grid, params.cell_size, ElevationModel::Interpolation::Bilinear, params.gradient, [=](double dzdx, double dzdy) {
		auto const slope = std::atan(std::hypot(dzdx, dzdy));
		if (binary)
			return slope < threshold ? 1.0 : 0.0;
		return darkest + (1.0 - darkest) * qBound(0.0, 1.0 - slope / threshold, 1.0);
	});
}


bool writeGeoTiff(const RasterProduct& product, const QString& path, QString* error)
{
	GDALAllRegister();
	auto driver = GDALGetDriverByName("GTiff");
	if (!driver || product.image.isNull())
	{
		if (error)
			*error = ElevationModel::tr("GDAL has no GeoTIFF driver");
		return false;
	}
	char** options = nullptr;
	options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
	options = CSLSetNameValue(options, "TILED", "YES");
	auto const width = product.image.width(), height = product.image.height();
	auto const format = product.image.format();
	auto const single = format == QImage::Format_Grayscale8 || format == QImage::Format_Indexed8;
	auto const image = single ? product.image : product.image.convertToFormat(QImage::Format_RGB32);
	auto dataset = GDALCreate(driver, path.toUtf8().constData(), width, height, single ? 1 : 3, GDT_Byte, options);
	CSLDestroy(options);
	if (!dataset)
	{
		if (error)
			*error = ElevationModel::tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
		return false;
	}
	auto const cell = product.extent.width() / width;
	double gt[6] = { product.extent.left(), cell, 0.0, product.extent.bottom(), 0.0, -product.extent.height() / height };
	GDALSetGeoTransform(dataset, gt);
	if (!product.crs_spec.isEmpty())
	{
		auto srs = OSRNewSpatialReference(nullptr);
		if (OSRSetFromUserInput(srs, product.crs_spec.toUtf8().constData()) == OGRERR_NONE)
		{
			char* wkt = nullptr;
			if (OSRExportToWkt(srs, &wkt) == OGRERR_NONE && wkt)
				GDALSetProjection(dataset, wkt);
			CPLFree(wkt);
		}
		OSRDestroySpatialReference(srs);
	}
	bool ok = true;
	if (single)
	{
		auto band = GDALGetRasterBand(dataset, 1);
		if (format == QImage::Format_Indexed8)
		{
			GDALSetRasterColorInterpretation(band, GCI_PaletteIndex);
			auto table = GDALCreateColorTable(GPI_RGB);
			auto const colours = image.colorTable();
			for (int i = 0; i < colours.size(); ++i)
			{
				GDALColorEntry entry = { short(qRed(colours[i])), short(qGreen(colours[i])), short(qBlue(colours[i])), 255 };
				GDALSetColorEntry(table, i, &entry);
			}
			GDALSetRasterColorTable(band, table);
			GDALDestroyColorTable(table);
		}
		else
		{
			GDALSetRasterColorInterpretation(band, GCI_GrayIndex);
		}
		for (int r = 0; r < height && ok; ++r)
		{
			std::vector<quint8> row(image.constScanLine(r), image.constScanLine(r) + width);
			ok = GDALRasterIO(band, GF_Write, 0, r, width, 1, row.data(), width, 1, GDT_Byte, 0, 0) == CE_None;
		}
	}
	else
	{
		GDALColorInterp const interpretation[] = { GCI_RedBand, GCI_GreenBand, GCI_BlueBand };
		for (int b = 0; b < 3 && ok; ++b)
		{
			auto band = GDALGetRasterBand(dataset, b + 1);
			GDALSetRasterColorInterpretation(band, interpretation[b]);
			std::vector<quint8> row(static_cast<std::size_t>(width));
			for (int r = 0; r < height && ok; ++r)
			{
				auto const* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(r));
				for (int c = 0; c < width; ++c)
					row[std::size_t(c)] = quint8(b == 0 ? qRed(pixels[c]) : b == 1 ? qGreen(pixels[c]) : qBlue(pixels[c]));
				ok = GDALRasterIO(band, GF_Write, 0, r, width, 1, row.data(), width, 1, GDT_Byte, 0, 0) == CE_None;
			}
		}
	}
	GDALClose(dataset);
	if (!ok && error)
		*error = ElevationModel::tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
	return ok;
}


Template* attachProductTemplate(Map& map, const QString& path, QString* error)
{
	auto grid = ElevationModel::open(path);
	if (!grid)
	{
		if (error)
			*error = ElevationModel::lastError();
		return nullptr;
	}
	auto const georeferenced = map.getGeoreferencing().getState() == Georeferencing::Geospatial
	                           && !grid->crsSpec().isEmpty();
	auto temp = std::unique_ptr<ProductTemplate>(new ProductTemplate(path, &map, georeferenced));
	if (!temp->loadTemplateFile())
	{
		if (error)
			*error = temp->errorString();
		return nullptr;
	}
	if (!georeferenced)
		temp->placeByExtent(grid->extent());
	temp->setTemplateState(Template::Loaded);  // a new template starts in the Configuring state
	auto* result = temp.get();
	// A product written a second time takes the place of its template (seen twice in the list on 2026-11-23).
	auto const file = QFileInfo(path);
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		if (QFileInfo(map.getTemplate(i)->getTemplatePath()) == file)
		{
			map.setTemplateAreaDirty(i);
			map.deleteTemplate(i);
			map.addTemplate(i, std::move(temp));
			map.setTemplateAreaDirty(i);
			return result;
		}
	}
	map.addTemplate(-1, std::move(temp));
	return result;
}


QString productPath(const QString& map_path, const QString& grid_path, const QString& suffix)
{
	auto const base = map_path.isEmpty() ? grid_path : map_path;
	QFileInfo const info(base);
	auto const name = info.completeBaseName().isEmpty() ? QStringLiteral("elevation") : info.completeBaseName();
	return info.absoluteDir().absoluteFilePath(name + QLatin1Char('_') + suffix + QLatin1String(".tif"));
}


std::vector<HeightClass> defaultHeightClasses()
{
	// The defaults.
	return {
		{ 0.0, 0.1, qRgb(255, 255, 55), qRgb(255, 255, 55) },
		{ 0.1, 2.0, qRgb(255, 215, 0), qRgb(255, 45, 55) },
		{ 2.0, 12.0, qRgb(0, 180, 0), qRgb(255, 255, 255) },
		{ 12.0, 30.0, qRgb(255, 255, 255), qRgb(255, 255, 255) },
	};
}

RasterProduct vegetationHeightRaster(const ElevationModel& heights, const std::vector<HeightClass>& classes)
{
	RasterProduct product;
	if (classes.empty())
	{
		product.error = ElevationModel::tr("No height class");
		return product;
	}
	auto const mix = [](QRgb a, QRgb b, double t) {
		auto const channel = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
		return qRgb(channel(qRed(a), qRed(b)), channel(qGreen(a), qGreen(b)), channel(qBlue(a), qBlue(b)));
	};
	auto const colour = [&classes, &mix](double h) {
		if (h < classes.front().from)
			return classes.front().from_color;
		for (auto const& c : classes)
		{
			if (h >= c.from && h < c.to)
				return mix(c.from_color, c.to_color, c.to > c.from ? (h - c.from) / (c.to - c.from) : 0.0);
		}
		if (h >= classes.back().to)
			return classes.back().to_color;
		return qRgb(255, 255, 255);   // in a gap between classes
	};
	product.image = QImage(heights.width(), heights.height(), QImage::Format_RGB32);
	for (int row = 0; row < heights.height(); ++row)
	{
		auto* line = reinterpret_cast<QRgb*>(product.image.scanLine(row));
		for (int col = 0; col < heights.width(); ++col)
		{
			auto const h = heights.value(col, row);
			line[col] = std::isnan(h) ? qRgb(0, 0, 0) : colour(double(h));
		}
	}
	product.extent = heights.extent();
	product.crs_spec = heights.crsSpec();
	return product;
}


RasterProduct vegetationHeightGreyscale(const ElevationModel& heights, VegetationHeightOptions::GreyMapping mapping, double max_height)
{
	// the design notes, "Vegetation height in grey and its classes in files".
	RasterProduct product;
	if (!(max_height > 0))
	{
		product.error = ElevationModel::tr("The maximum height must be positive");
		return product;
	}
	product.image = QImage(heights.width(), heights.height(), QImage::Format_Grayscale8);
	for (int row = 0; row < heights.height(); ++row)
	{
		auto* line = product.image.scanLine(row);
		for (int col = 0; col < heights.width(); ++col)
		{
			auto const h = heights.value(col, row);
			if (std::isnan(h))
			{
				line[col] = 0;
				continue;
			}
			auto const t = qBound(0.0, double(h) / max_height, 1.0);
			double value;
			switch (mapping)
			{
			case VegetationHeightOptions::QuadraticNegative:
				value = (1.0 - t) * (1.0 - t);
				break;
			case VegetationHeightOptions::QuadraticPositive:
				value = 1.0 - t * t;
				break;
			case VegetationHeightOptions::Linear:
			default:
				value = 1.0 - t;
			}
			line[col] = grey(value);
		}
	}
	product.extent = heights.extent();
	product.crs_spec = heights.crsSpec();
	return product;
}


RasterProduct vegetationHeightProduct(const ElevationModel& heights, const VegetationHeightOptions& options)
{
	if (options.greyscale)
		return vegetationHeightGreyscale(heights, options.mapping, options.max_height);
	return vegetationHeightRaster(heights, options.classes);
}


namespace {

const char* const height_class_header = "from;to;from_colour;to_colour";

}  // namespace


bool saveHeightClasses(const QString& path, const std::vector<HeightClass>& classes, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	QByteArray text = height_class_header;
	text += '\n';
	auto const colour = [](QRgb rgb) {
		return QColor(rgb).name().toLatin1();   // #rrggbb
	};
	for (auto const& c : classes)
	{
		text += QByteArray::number(c.from, 'g', 15) + ';' + QByteArray::number(c.to, 'g', 15) + ';'
		        + colour(c.from_color) + ';' + colour(c.to_color) + '\n';
	}
	if (file.write(text) != text.size())
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}


bool loadHeightClasses(const QString& path, std::vector<HeightClass>& classes, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return fail(file.errorString());
	auto const lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
	if (lines.isEmpty() || lines.front().trimmed() != QLatin1String(height_class_header))
		return fail(ElevationModel::tr("Line 1 must read \"%1\".").arg(QLatin1String(height_class_header)));
	std::vector<HeightClass> result;
	QRegularExpression const colour_pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
	for (int i = 1; i < lines.size(); ++i)
	{
		auto const line = lines[i].trimmed();
		if (line.isEmpty())
			continue;
		auto const fields = line.split(QLatin1Char(';'));
		auto const bad = [&fail, i]() {
			return fail(ElevationModel::tr("Line %1 is not a class of the form from;to;#rrggbb;#rrggbb.").arg(i + 1));
		};
		if (fields.size() != 4)
			return bad();
		bool ok_from = false, ok_to = false;
		HeightClass c;
		c.from = fields[0].trimmed().toDouble(&ok_from);
		c.to = fields[1].trimmed().toDouble(&ok_to);
		auto const from_colour = fields[2].trimmed();
		auto const to_colour = fields[3].trimmed();
		if (!ok_from || !ok_to || !colour_pattern.match(from_colour).hasMatch() || !colour_pattern.match(to_colour).hasMatch())
			return bad();
		c.from_color = QColor(from_colour).rgb();
		c.to_color = QColor(to_colour).rgb();
		if (!(c.from < c.to))
			return fail(ElevationModel::tr("Line %1: the class must begin below its end.").arg(i + 1));
		if (!result.empty() && c.from < result.back().to)
			return fail(ElevationModel::tr("Line %1: the class begins below the end of the one before.").arg(i + 1));
		result.push_back(c);
	}
	if (result.empty())
		return fail(ElevationModel::tr("The file holds no class."));
	classes = std::move(result);
	return true;
}


RasterProduct hypsometricRaster(const ElevationModel& grid, const HypsometricParams& params)
{
	// the design notes, "Hypsometric map: the colours by convention".
	auto const black = qRgb(0, 0, 0);
	auto const white = qRgb(255, 255, 255);
	std::vector<QRgb> stops;   // from low to high, evenly spaced
	switch (params.ramp)
	{
	case HypsometricParams::RedGreenBlue:
		stops = { qRgb(0, 0, 255), qRgb(0, 255, 255), qRgb(0, 255, 0), qRgb(255, 255, 0), qRgb(255, 0, 0) };
		break;
	case HypsometricParams::RedYellowGreen:    // ColorBrewer's RdYlGn of five classes
		stops = { qRgb(26, 150, 65), qRgb(166, 217, 106), qRgb(255, 255, 191), qRgb(253, 174, 97), qRgb(215, 25, 28) };
		break;
	case HypsometricParams::RedYellowBlue:     // ColorBrewer's RdYlBu of five classes
		stops = { qRgb(44, 123, 182), qRgb(171, 217, 233), qRgb(255, 255, 191), qRgb(253, 174, 97), qRgb(215, 25, 28) };
		break;
	case HypsometricParams::BrownYellowGreen:  // the layer tints of atlases: green lowlands, brown heights
		stops = { qRgb(0, 117, 39), qRgb(86, 185, 30), qRgb(255, 226, 54), qRgb(255, 178, 0), qRgb(166, 122, 83) };
		break;
	case HypsometricParams::BrownYellow:
		stops = { qRgb(255, 255, 190), qRgb(211, 189, 137), qRgb(166, 122, 83) };
		break;
	case HypsometricParams::RedYellow:
		stops = { qRgb(255, 255, 191), qRgb(235, 140, 110), qRgb(215, 25, 28) };
		break;
	case HypsometricParams::Viridis:           // matplotlib's viridis (CC0) at every eighth
		stops = { qRgb(68, 1, 84), qRgb(71, 45, 123), qRgb(59, 82, 139), qRgb(44, 114, 142), qRgb(33, 145, 140),
		          qRgb(40, 174, 128), qRgb(94, 201, 98), qRgb(173, 220, 48), qRgb(253, 231, 37) };
		break;
	case HypsometricParams::Greyscale:
	case HypsometricParams::GreyscaleInterval:
		stops = { black, white };
		break;
	}
	auto const mix = [](QRgb a, QRgb b, double t) {
		auto const channel = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
		return qRgb(channel(qRed(a), qRed(b)), channel(qGreen(a), qGreen(b)), channel(qBlue(a), qBlue(b)));
	};
	auto const colour = [&stops, &mix](double t) {
		t = qBound(0.0, t, 1.0) * double(stops.size() - 1);
		auto const k = std::min(std::size_t(t), stops.size() - 2);
		return mix(stops[k], stops[k + 1], t - double(k));
	};
	auto const info = grid.info();
	auto const lowest = std::isnan(params.range_from) ? info.minimum : params.range_from;
	auto const highest = std::isnan(params.range_to) ? info.maximum : params.range_to;
	auto const range = highest - lowest;
	auto const interval = params.interval > 0.0 ? params.interval : 100.0;
	// The intervals which the heights touch, counted from multiples of the interval.
	auto const first_band = std::floor(lowest / interval);
	auto const bands = std::floor(highest / interval) - first_band + 1.0;
	
	RasterProduct product;
	product.image = QImage(grid.width(), grid.height(), QImage::Format_RGB32);
	for (int row = 0; row < grid.height(); ++row)
	{
		auto* line = reinterpret_cast<QRgb*>(product.image.scanLine(row));
		for (int col = 0; col < grid.width(); ++col)
		{
			auto const z = double(grid.value(col, row));
			if (std::isnan(z))
				line[col] = black;
			else if (params.white_at_zero && z == 0.0)
				line[col] = white;
			else if (params.ramp == HypsometricParams::GreyscaleInterval)
				line[col] = colour(bands > 0.0 ? (std::floor(z / interval) - first_band) / bands : 0.0);
			else
				line[col] = colour(range > 0.0 ? (z - lowest) / range : 0.0);
		}
	}
	product.extent = grid.extent();
	product.crs_spec = grid.crsSpec();
	return product;
}


}  // namespace OpenOrienteering
