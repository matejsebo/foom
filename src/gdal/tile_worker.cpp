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


#include "tile_worker.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtGlobal>
#include <QFile>
#include <QLatin1String>
#include <QMetaType>

#include <gdal.h>
#include <gdal_utils.h>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>

#include "mapper_config.h"

namespace OpenOrienteering {

namespace {

QString lastGdalError()
{
	auto const* message = CPLGetLastErrorMsg();
	return (message && *message) ? QString::fromUtf8(message) : QStringLiteral("GDAL error %1").arg(CPLGetLastErrorNo());
}

/**
 * Reads the bands of a dataset into a 32-bit ARGB image.
 */
bool readImage(GDALDatasetH dataset, int width, int height, QImage& image, QString& error)
{
	auto const count = GDALGetRasterCount(dataset);
	if (count < 1)
	{
		error = QStringLiteral("No raster bands");
		return false;
	}

	int red = 0, green = 0, blue = 0, alpha = 0, grey = 0;
	for (int i = 1; i <= count; ++i)
	{
		switch (GDALGetRasterColorInterpretation(GDALGetRasterBand(dataset, i)))
		{
		case GCI_RedBand:   if (!red)   red = i;   break;
		case GCI_GreenBand: if (!green) green = i; break;
		case GCI_BlueBand:  if (!blue)  blue = i;  break;
		case GCI_AlphaBand: if (!alpha) alpha = i; break;
		case GCI_GrayIndex: if (!grey)  grey = i;  break;
		default: break;
		}
	}
	if (!(red && green && blue))
	{
		if (count >= 3)
		{
			red = 1; green = 2; blue = 3;
			if (count >= 4 && !alpha)
				alpha = 4;
		}
		else
		{
			red = green = blue = grey ? grey : 1;
			if (count == 2 && !alpha)
				alpha = 2;
		}
	}

	image = QImage(width, height, QImage::Format_ARGB32);
	if (image.isNull())
	{
		error = QStringLiteral("Out of memory");
		return false;
	}
	image.fill(0xffffffff);

	// Little-endian ARGB32: bytes B, G, R, A
	int bands[4] = { blue, green, red, alpha };
	auto const band_count = alpha ? 4 : 3;
	auto const result = GDALDatasetRasterIO(dataset, GF_Read, 0, 0, width, height,
	                                        image.bits(), width, height, GDT_Byte,
	                                        band_count, bands,
	                                        4, image.bytesPerLine(), 1);
	if (result != CE_None)
	{
		error = lastGdalError();
		return false;
	}
	if (!alpha)
	{
		for (int y = 0; y < height; ++y)
		{
			auto* line = image.scanLine(y);
			for (int x = 0; x < width; ++x)
				line[4 * x + 3] = 255;
		}
	}
	image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	return true;
}

QByteArray number(double value)
{
	return QByteArray::number(value, 'f', 6);
}

}  // namespace


TileWorker::TileWorker()
{
	qRegisterMetaType<TileKey>();
}

TileWorker::~TileWorker()
{
	close();
}


bool TileWorker::open(const QByteArray& gdal_source, const QString& map_crs_spec, const QPointF& reference,
                      const QString& user, const QString& password, QString* error, double* r0_out)
{
	close();

	static thread_local bool handler_installed = false;
	if (!handler_installed)
	{
		CPLPushErrorHandler(CPLQuietErrorHandler);
		handler_installed = true;
	}
	CPLSetThreadLocalConfigOption("GDAL_HTTP_TIMEOUT", "10");
	CPLSetThreadLocalConfigOption("GDAL_HTTP_USERAGENT", "FOOM/" APP_VERSION);
	CPLSetThreadLocalConfigOption("GDAL_HTTP_USERPWD", user.isEmpty() ? nullptr : (user + QLatin1Char(':') + password).toUtf8().constData());

	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};

	CPLErrorReset();
	dataset = GDALOpenEx(gdal_source.constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!dataset)
		return fail(lastGdalError());

	const char* projection = GDALGetProjectionRef(dataset);
	if (!projection || !*projection)
	{
		// foom: a WMTS service of several layers, opened without a layer, lists them as
		// subdatasets and has no coordinate system itself; take the first layer.
		auto const* first = CSLFetchNameValue(GDALGetMetadata(dataset, "SUBDATASETS"), "SUBDATASET_1_NAME");
		if (first && *first)
		{
			QByteArray const name(first);
			GDALClose(dataset);
			dataset = GDALOpenEx(name.constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
			if (!dataset)
				return fail(lastGdalError());
			projection = GDALGetProjectionRef(dataset);
		}
	}
	if (!projection || !*projection)
	{
		close();
		return fail(QStringLiteral("The service does not state a coordinate system."));
	}
	source_wkt = projection;

	auto source_srs = OSRNewSpatialReference(source_wkt.constData());
	auto target_srs = OSRNewSpatialReference(nullptr);
	auto const cleanup = [&]() {
		OSRDestroySpatialReference(source_srs);
		OSRDestroySpatialReference(target_srs);
	};
	if (!source_srs || OSRSetFromUserInput(target_srs, map_crs_spec.toUtf8().constData()) != OGRERR_NONE)
	{
		cleanup();
		close();
		return fail(QStringLiteral("Cannot interpret the coordinate system \"%1\".").arg(map_crs_spec));
	}
	OSRSetAxisMappingStrategy(source_srs, OAMS_TRADITIONAL_GIS_ORDER);
	OSRSetAxisMappingStrategy(target_srs, OAMS_TRADITIONAL_GIS_ORDER);

	char* wkt = nullptr;
	OSRExportToWkt(target_srs, &wkt);
	target_wkt = wkt ? QByteArray(wkt) : QByteArray();
	CPLFree(wkt);
	if (target_wkt.isEmpty())
	{
		cleanup();
		close();
		return fail(QStringLiteral("Cannot describe the map's coordinate system to GDAL."));
	}

	same_crs = OSRIsSame(source_srs, target_srs);

	double gt[6] = { 0, 1, 0, 0, 0, -1 };
	if (GDALGetGeoTransform(dataset, gt) != CE_None)
	{
		cleanup();
		close();
		return fail(QStringLiteral("The service does not state its georeferencing."));
	}
	auto const pixel = std::hypot(gt[1], gt[4]);
	r0 = pixel;

	if (!same_crs)
	{
		// Measure the source pixel at the reference point, in the map's system.
		auto to_source = OCTNewCoordinateTransformation(target_srs, source_srs);
		auto to_target = OCTNewCoordinateTransformation(source_srs, target_srs);
		if (to_source && to_target)
		{
			double x[2] = { reference.x(), 0 };
			double y[2] = { reference.y(), 0 };
			if (OCTTransform(to_source, 1, x, y, nullptr))
			{
				x[1] = x[0] + pixel;
				y[1] = y[0];
				if (OCTTransform(to_target, 2, x, y, nullptr))
				{
					auto const measured = std::hypot(x[1] - x[0], y[1] - y[0]);
					if (measured > 0 && std::isfinite(measured))
						r0 = measured;
				}
			}
		}
		if (to_source)
			OCTDestroyCoordinateTransformation(to_source);
		if (to_target)
			OCTDestroyCoordinateTransformation(to_target);
	}
	cleanup();

	has_alpha = false;
	for (int i = 1, count = GDALGetRasterCount(dataset); i <= count; ++i)
		has_alpha |= GDALGetRasterColorInterpretation(GDALGetRasterBand(dataset, i)) == GCI_AlphaBand;

	if (r0_out)
		*r0_out = r0;
	return true;
}

void TileWorker::close()
{
	if (dataset)
	{
		GDALClose(dataset);
		dataset = nullptr;
	}
	r0 = 0;
}


void TileWorker::setPending(std::vector<TileKey> keys)
{
	std::lock_guard<std::mutex> lock(mutex);
	pending = std::move(keys);
}

void TileWorker::cancel()
{
	std::lock_guard<std::mutex> lock(mutex);
	pending.clear();
	cancelled = true;
}

void TileWorker::work()
{
	for (;;)
	{
		TileKey key;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (pending.empty())
				return;
			key = pending.front();
			pending.erase(pending.begin());
		}
		QImage image;
		QString error;
		if (fetch(key, image, error))
			emit tileReady(key, image);
		else
			emit tileFailed(key, error);
	}
}


bool TileWorker::fetch(const TileKey& key, QImage& image, QString& error)
{
	return renderWindow(TilePyramid::tileRect(r0, key), TilePyramid::tile_size, TilePyramid::tile_size, image, error);
}

bool TileWorker::renderWindow(const QRectF& rect, int width, int height, QImage& image, QString& error)
{
	if (!dataset)
	{
		error = QStringLiteral("The service is not open.");
		return false;
	}
	if (width < 1 || height < 1 || !rect.isValid())
	{
		error = QStringLiteral("Nothing to render.");
		return false;
	}

	char** argv = nullptr;
	argv = CSLAddString(argv, "-of");
	argv = CSLAddString(argv, "MEM");
	argv = CSLAddString(argv, "-t_srs");
	argv = CSLAddString(argv, target_wkt.constData());
	argv = CSLAddString(argv, "-te");
	argv = CSLAddString(argv, number(rect.left()));
	argv = CSLAddString(argv, number(rect.top()));
	argv = CSLAddString(argv, number(rect.right()));
	argv = CSLAddString(argv, number(rect.bottom()));
	argv = CSLAddString(argv, "-ts");
	argv = CSLAddString(argv, QByteArray::number(width));
	argv = CSLAddString(argv, QByteArray::number(height));
	argv = CSLAddString(argv, "-r");
	argv = CSLAddString(argv, "bilinear");
	if (!has_alpha)
		argv = CSLAddString(argv, "-dstalpha");
	auto* options = GDALWarpAppOptionsNew(argv, nullptr);
	CSLDestroy(argv);
	if (!options)
	{
		error = QStringLiteral("Invalid warp options");
		return false;
	}

	CPLErrorReset();
	int usage_error = 0;
	GDALDatasetH sources[1] = { dataset };
	auto result = GDALWarp("", nullptr, 1, sources, options, &usage_error);
	GDALWarpAppOptionsFree(options);
	if (!result)
	{
		error = lastGdalError();
		return false;
	}

	auto const ok = readImage(result, width, height, image, error);
	GDALClose(result);
	return ok;
}


// static
int TileWorker::progressCallback(double complete, const char* /*message*/, void* data)
{
	auto* worker = static_cast<TileWorker*>(data);
	auto const percent = qBound(0, int(complete * 100), 100);
	if (percent != worker->last_percent)
	{
		worker->last_percent = percent;
		emit worker->downloadProgress(percent);
	}
	return worker->cancelled ? FALSE : TRUE;
}

void TileWorker::download(const QRectF& projected_rect, int width, int height, const QString& path)
{
	if (!dataset)
	{
		emit downloadFinished(false, QStringLiteral("The service is not open."));
		return;
	}
	cancelled = false;
	last_percent = -1;

	char** argv = nullptr;
	argv = CSLAddString(argv, "-of");
	argv = CSLAddString(argv, "GTiff");
	argv = CSLAddString(argv, "-co");
	argv = CSLAddString(argv, "TILED=YES");
	argv = CSLAddString(argv, "-co");
	argv = CSLAddString(argv, "COMPRESS=DEFLATE");
	argv = CSLAddString(argv, "-co");
	argv = CSLAddString(argv, "PREDICTOR=2");
	argv = CSLAddString(argv, "-co");
	argv = CSLAddString(argv, "BIGTIFF=IF_SAFER");
	argv = CSLAddString(argv, "-overwrite");
	argv = CSLAddString(argv, "-t_srs");
	argv = CSLAddString(argv, target_wkt.constData());
	argv = CSLAddString(argv, "-te");
	argv = CSLAddString(argv, number(projected_rect.left()));
	argv = CSLAddString(argv, number(projected_rect.top()));
	argv = CSLAddString(argv, number(projected_rect.right()));
	argv = CSLAddString(argv, number(projected_rect.bottom()));
	argv = CSLAddString(argv, "-ts");
	argv = CSLAddString(argv, QByteArray::number(width));
	argv = CSLAddString(argv, QByteArray::number(height));
	argv = CSLAddString(argv, "-r");
	argv = CSLAddString(argv, "bilinear");
	if (!has_alpha)
		argv = CSLAddString(argv, "-dstalpha");
	auto* options = GDALWarpAppOptionsNew(argv, nullptr);
	CSLDestroy(argv);
	if (!options)
	{
		emit downloadFinished(false, QStringLiteral("Invalid warp options"));
		return;
	}
	GDALWarpAppOptionsSetProgress(options, &TileWorker::progressCallback, this);

	CPLErrorReset();
	int usage_error = 0;
	GDALDatasetH sources[1] = { dataset };
	auto result = GDALWarp(path.toUtf8().constData(), nullptr, 1, sources, options, &usage_error);
	GDALWarpAppOptionsFree(options);
	if (result)
	{
		GDALClose(result);
		emit downloadFinished(true, path);
	}
	else
	{
		auto const message = cancelled ? QStringLiteral("Cancelled") : lastGdalError();
		QFile::remove(path);
		emit downloadFinished(false, message);
	}
}


}  // namespace OpenOrienteering
