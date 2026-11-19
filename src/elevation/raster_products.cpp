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
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QStringLiteral>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
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
RasterProduct makeProduct(const ElevationModel& input, double cell_size, Shade shade)
{
	RasterProduct product;
	std::unique_ptr<ElevationModel> resampled;
	auto const* grid = &input;
	if (cell_size > 0.0 && qAbs(cell_size - input.cellSize()) > 1e-9 * input.cellSize())
	{
		resampled = input.resampled(cell_size);
		grid = resampled.get();
	}
	product.extent = grid->extent();
	product.crs_spec = grid->crsSpec();
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
		auto const& georef = map->getGeoreferencing();
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
	return makeProduct(grid, params.cell_size, [=](double dzdx, double dzdy) {
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
	return makeProduct(grid, params.cell_size, [=](double dzdx, double dzdy) {
		auto const slope = std::atan(std::hypot(dzdx, dzdy));
		if (binary)
			return slope < threshold ? 1.0 : 0.0;
		return qBound(0.0, 1.0 - slope / threshold, 1.0);
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
	auto dataset = GDALCreate(driver, path.toUtf8().constData(), width, height, 1, GDT_Byte, options);
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
	auto band = GDALGetRasterBand(dataset, 1);
	GDALSetRasterColorInterpretation(band, GCI_GrayIndex);
	bool ok = true;
	for (int r = 0; r < height && ok; ++r)
	{
		std::vector<quint8> row(product.image.constScanLine(r), product.image.constScanLine(r) + width);
		ok = GDALRasterIO(band, GF_Write, 0, r, width, 1, row.data(), width, 1, GDT_Byte, 0, 0) == CE_None;
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


}  // namespace OpenOrienteering
