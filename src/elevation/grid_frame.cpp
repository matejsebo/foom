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


#include "grid_frame.h"

#include <cstddef>
#include <vector>

#include <QByteArray>
#include <QPointF>
#include <QRectF>

#include <cpl_error.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

namespace {

/// The ring of a rectangle given by a point-to-coordinate function of the fractions (u, v), u eastwards and v southwards.
template <typename ToCoord>
QPolygonF ringOf(int pieces, ToCoord to_coord)
{
	QPolygonF ring;
	ring.reserve(4 * pieces);
	for (int i = 0; i < pieces; ++i)
		ring.append(to_coord(double(i) / pieces, 0.0));
	for (int i = 0; i < pieces; ++i)
		ring.append(to_coord(1.0, double(i) / pieces));
	for (int i = 0; i < pieces; ++i)
		ring.append(to_coord(1.0 - double(i) / pieces, 1.0));
	for (int i = 0; i < pieces; ++i)
		ring.append(to_coord(0.0, 1.0 - double(i) / pieces));
	return ring;
}

QPolygonF ringOf(const QRectF& extent, int pieces)
{
	// QRectF::top() is the southern edge.
	return ringOf(pieces, [&extent](double u, double v) {
		return QPointF(extent.left() + u * extent.width(), extent.bottom() - v * extent.height());
	});
}

/// Projects the points in place; false on failure.
bool project(QPolygonF& ring, const QByteArray& source_spec, const QString& target_spec)
{
	auto source = OSRNewSpatialReference(nullptr);
	auto target = OSRNewSpatialReference(nullptr);
	OGRCoordinateTransformationH transform = nullptr;
	if (OSRSetFromUserInput(source, source_spec.constData()) == OGRERR_NONE
	    && OSRSetFromUserInput(target, target_spec.toUtf8().constData()) == OGRERR_NONE)
	{
		OSRSetAxisMappingStrategy(source, OAMS_TRADITIONAL_GIS_ORDER);
		OSRSetAxisMappingStrategy(target, OAMS_TRADITIONAL_GIS_ORDER);
		transform = OCTNewCoordinateTransformation(source, target);
	}
	auto ok = transform != nullptr;
	if (ok)
	{
		std::vector<double> x, y;
		x.reserve(std::size_t(ring.size()));
		y.reserve(std::size_t(ring.size()));
		for (auto const& p : ring)
		{
			x.push_back(p.x());
			y.push_back(p.y());
		}
		std::vector<int> success(x.size(), 0);
		ok = OCTTransformEx(transform, int(x.size()), x.data(), y.data(), nullptr, success.data()) != 0;
		for (std::size_t i = 0; ok && i < x.size(); ++i)
		{
			ok = success[i] != 0;
			ring[int(i)] = QPointF(x[i], y[i]);
		}
		OCTDestroyCoordinateTransformation(transform);
	}
	OSRDestroySpatialReference(source);
	OSRDestroySpatialReference(target);
	return ok;
}

}  // namespace



QPolygonF gridFootprint(const QString& path, const QString& target_crs, int pieces)
{
	if (pieces < 1)
		pieces = 1;
	GDALAllRegister();
	CPLPushErrorHandler(CPLQuietErrorHandler);
	auto dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	CPLPopErrorHandler();
	if (!dataset)
		return {};

	QPolygonF ring;
	double gt[6] = { 0, 1, 0, 0, 0, -1 };
	if (GDALGetGeoTransform(dataset, gt) == CE_None)
	{
		auto const width = double(GDALGetRasterXSize(dataset));
		auto const height = double(GDALGetRasterYSize(dataset));
		// A south-up file starts from its last row, so that the ring starts in the north-west.
		auto const south_up = gt[5] > 0.0;
		ring = ringOf(pieces, [&](double u, double v) {
			auto const px = u * width;
			auto const py = (south_up ? 1.0 - v : v) * height;
			return QPointF(gt[0] + px * gt[1] + py * gt[2], gt[3] + px * gt[4] + py * gt[5]);
		});
		QByteArray source_spec;
		if (auto const* projection = GDALGetProjectionRef(dataset))
			source_spec = projection;
		if (!target_crs.isEmpty() && !source_spec.isEmpty()
		    && !ElevationModel::sameCrs(QString::fromUtf8(source_spec), target_crs)
		    && !project(ring, source_spec, target_crs))
			ring.clear();
	}
	GDALClose(dataset);
	return ring;
}


std::vector<ElevationFrame> elevationFrames(Map& map, int pieces)
{
	std::vector<ElevationFrame> frames;
	auto const& georef = map.getGeoreferencing();
	auto const crs = georef.getProjectedCRSSpec();
	auto& set = map.elevationSet();
	for (int k = 0; k < ElevationSet::kind_count; ++k)
	{
		auto const kind = ElevationSet::Kind(k);
		auto const href = set.href(kind);
		if (href.isEmpty())
			continue;
		auto ring = gridFootprint(href, crs, pieces);
		if (ring.isEmpty())
		{
			auto const* model = set.model(kind, crs);
			if (!model)
				continue;
			ring = ringOf(model->extent(), pieces);
		}
		for (auto& point : ring)
			point = georef.toMapCoordF(point);
		frames.push_back({ kind, ring });
	}
	return frames;
}


}  // namespace OpenOrienteering
