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

#include "grid_tiles.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QByteArray>
#include <QFile>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "elevation/elevation_model.h"
#include "elevation/raster_products.h"

namespace OpenOrienteering {

namespace {

constexpr float nodata_out = -9999.0f;  // as ElevationModel::save()

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

QString gdalMessage()
{
	return QString::fromUtf8(CPLGetLastErrorMsg());
}

QByteArray wktOf(const QString& spec)
{
	if (spec.isEmpty())
		return {};
	auto srs = OSRNewSpatialReference(nullptr);
	QByteArray result;
	if (OSRSetFromUserInput(srs, spec.toUtf8().constData()) == OGRERR_NONE)
	{
		char* wkt = nullptr;
		if (OSRExportToWkt(srs, &wkt) == OGRERR_NONE && wkt)
			result = QByteArray(wkt);
		CPLFree(wkt);
	}
	OSRDestroySpatialReference(srs);
	return result;
}

/// GeoTIFF creation options: tiled, DEFLATE, BigTIFF when needed.
char** tiffOptions(bool predictor)
{
	char** options = nullptr;
	options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
	options = CSLSetNameValue(options, "TILED", "YES");
	options = CSLSetNameValue(options, "BLOCKXSIZE", "256");
	options = CSLSetNameValue(options, "BLOCKYSIZE", "256");
	options = CSLSetNameValue(options, "BIGTIFF", "IF_SAFER");
	if (predictor)
		options = CSLSetNameValue(options, "PREDICTOR", "3");
	return options;
}

// the design notes, "Grids on disk, a tile cache, and a progress bar": the ceiling of GDAL's
// block cache while a writer is open.
qint64 write_cache_limit = qint64(256) * 1024 * 1024;
int open_writers = 0;
GIntBig cache_max_before = 0;

/// Called when a writer has opened its file: lowers GDAL's ceiling for the first one.
void holdWriteCache()
{
	if (open_writers++ == 0)
	{
		cache_max_before = GDALGetCacheMax64();
		if (cache_max_before > write_cache_limit)
			GDALSetCacheMax64(GIntBig(write_cache_limit));
	}
}

/// Called when a writer has closed its file: gives GDAL its ceiling back after the last one.
void releaseWriteCache()
{
	if (open_writers > 0 && --open_writers == 0 && cache_max_before > 0)
		GDALSetCacheMax64(cache_max_before);
}

}  // namespace



// ### GridFileWriter ###

GridFileWriter::GridFileWriter() = default;

GridFileWriter::~GridFileWriter()
{
	if (dataset)
	{
		GDALClose(GDALDatasetH(dataset));
		releaseWriteCache();
	}
}

qint64 GridFileWriter::writeCacheLimit()
{
	return write_cache_limit;
}

void GridFileWriter::setWriteCacheLimit(qint64 bytes)
{
	write_cache_limit = std::max<qint64>(bytes, 1024 * 1024);
	if (open_writers > 0)
		GDALSetCacheMax64(GIntBig(std::min<qint64>(write_cache_limit, cache_max_before)));
}

qint64 GridFileWriter::cacheCeiling()
{
	return qint64(GDALGetCacheMax64());
}

qint64 GridFileWriter::cacheInUse()
{
	return qint64(GDALGetCacheUsed64());
}

bool GridFileWriter::open(const QString& path, int width, int height, const QPointF& top_left, double cell, const QString& crs_spec, QString* error)
{
	GDALAllRegister();
	auto driver = GDALGetDriverByName("GTiff");
	if (!driver)
	{
		setError(error, tr("GDAL has no GeoTIFF driver"));
		return false;
	}
	auto options = tiffOptions(true);
	auto handle = GDALCreate(driver, path.toUtf8().constData(), width, height, 1, GDT_Float32, options);
	CSLDestroy(options);
	if (!handle)
	{
		setError(error, tr("Cannot write %1: %2").arg(path, gdalMessage()));
		return false;
	}
	double gt[6] = { top_left.x(), cell, 0.0, top_left.y(), 0.0, -cell };
	GDALSetGeoTransform(handle, gt);
	auto const wkt = wktOf(crs_spec);
	if (!wkt.isEmpty())
		GDALSetProjection(handle, wkt.constData());
	GDALSetRasterNoDataValue(GDALGetRasterBand(handle, 1), nodata_out);
	dataset = handle;
	file = path;
	grid_width = width;
	holdWriteCache();
	return true;
}

bool GridFileWriter::writeRows(int first_row, int rows, const float* values, QString* error)
{
	if (!dataset)
		return false;
	std::vector<float> out(values, values + std::size_t(grid_width) * std::size_t(rows));
	for (auto& v : out)
	{
		if (std::isnan(v))
			v = nodata_out;
	}
	auto band = GDALGetRasterBand(GDALDatasetH(dataset), 1);
	if (GDALRasterIO(band, GF_Write, 0, first_row, grid_width, rows, out.data(), grid_width, rows, GDT_Float32, 0, 0) != CE_None)
	{
		setError(error, tr("Cannot write %1: %2").arg(file, gdalMessage()));
		return false;
	}
	return true;
}

bool GridFileWriter::setCrs(const QString& crs_spec, QString* error)
{
	if (!dataset)
		return false;
	auto const wkt = wktOf(crs_spec);
	if (wkt.isEmpty())
		return true;
	if (GDALSetProjection(GDALDatasetH(dataset), wkt.constData()) != CE_None)
	{
		setError(error, tr("Cannot write %1: %2").arg(file, gdalMessage()));
		return false;
	}
	return true;
}

bool GridFileWriter::close(QString* error)
{
	if (!dataset)
		return false;
	CPLErrorReset();
	GDALClose(GDALDatasetH(dataset));
	dataset = nullptr;
	releaseWriteCache();
	if (CPLGetLastErrorType() >= CE_Failure)
	{
		setError(error, tr("Cannot write %1: %2").arg(file, gdalMessage()));
		return false;
	}
	return true;
}

void GridFileWriter::discard()
{
	if (dataset)
	{
		GDALClose(GDALDatasetH(dataset));
		dataset = nullptr;
		releaseWriteCache();
	}
	if (!file.isEmpty())
		QFile::remove(file);
}



// ### RasterFileWriter ###

RasterFileWriter::RasterFileWriter() = default;

RasterFileWriter::~RasterFileWriter()
{
	if (dataset)
	{
		GDALClose(GDALDatasetH(dataset));
		releaseWriteCache();
	}
}

bool RasterFileWriter::open(const QString& path, int width, int height, const QRectF& extent, const QString& crs_spec,
                            QImage::Format format, const QVector<QRgb>& palette, QString* error)
{
	// As writeGeoTiff() in raster_products.cpp.
	GDALAllRegister();
	auto driver = GDALGetDriverByName("GTiff");
	if (!driver)
	{
		setError(error, tr("GDAL has no GeoTIFF driver"));
		return false;
	}
	single = format == QImage::Format_Grayscale8 || format == QImage::Format_Indexed8;
	auto options = tiffOptions(false);
	auto handle = GDALCreate(driver, path.toUtf8().constData(), width, height, single ? 1 : 3, GDT_Byte, options);
	CSLDestroy(options);
	if (!handle)
	{
		setError(error, tr("Cannot write %1: %2").arg(path, gdalMessage()));
		return false;
	}
	double gt[6] = { extent.left(), extent.width() / width, 0.0, extent.bottom(), 0.0, -extent.height() / height };
	GDALSetGeoTransform(handle, gt);
	auto const wkt = wktOf(crs_spec);
	if (!wkt.isEmpty())
		GDALSetProjection(handle, wkt.constData());
	if (single)
	{
		auto band = GDALGetRasterBand(handle, 1);
		if (format == QImage::Format_Indexed8)
		{
			GDALSetRasterColorInterpretation(band, GCI_PaletteIndex);
			auto table = GDALCreateColorTable(GPI_RGB);
			for (int i = 0; i < palette.size(); ++i)
			{
				GDALColorEntry entry = { short(qRed(palette[i])), short(qGreen(palette[i])), short(qBlue(palette[i])), 255 };
				GDALSetColorEntry(table, i, &entry);
			}
			GDALSetRasterColorTable(band, table);
			GDALDestroyColorTable(table);
		}
		else
		{
			GDALSetRasterColorInterpretation(band, GCI_GrayIndex);
		}
	}
	else
	{
		GDALColorInterp const interpretation[] = { GCI_RedBand, GCI_GreenBand, GCI_BlueBand };
		for (int b = 0; b < 3; ++b)
			GDALSetRasterColorInterpretation(GDALGetRasterBand(handle, b + 1), interpretation[b]);
	}
	dataset = handle;
	file = path;
	raster_width = width;
	holdWriteCache();
	return true;
}

bool RasterFileWriter::writeRows(int first_row, const QImage& rows, QString* error)
{
	if (!dataset)
		return false;
	auto const height = rows.height();
	auto const width = raster_width;
	bool ok = true;
	if (single)
	{
		std::vector<quint8> values(std::size_t(width) * std::size_t(height));
		for (int r = 0; r < height; ++r)
			std::copy(rows.constScanLine(r), rows.constScanLine(r) + width, values.begin() + std::ptrdiff_t(r) * width);
		auto band = GDALGetRasterBand(GDALDatasetH(dataset), 1);
		ok = GDALRasterIO(band, GF_Write, 0, first_row, width, height, values.data(), width, height, GDT_Byte, 0, 0) == CE_None;
	}
	else
	{
		auto const image = rows.format() == QImage::Format_RGB32 ? rows : rows.convertToFormat(QImage::Format_RGB32);
		std::vector<quint8> values(std::size_t(width) * std::size_t(height));
		for (int b = 0; b < 3 && ok; ++b)
		{
			for (int r = 0; r < height; ++r)
			{
				auto const* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(r));
				for (int c = 0; c < width; ++c)
					values[std::size_t(r) * std::size_t(width) + std::size_t(c)] = quint8(b == 0 ? qRed(pixels[c]) : b == 1 ? qGreen(pixels[c]) : qBlue(pixels[c]));
			}
			auto band = GDALGetRasterBand(GDALDatasetH(dataset), b + 1);
			ok = GDALRasterIO(band, GF_Write, 0, first_row, width, height, values.data(), width, height, GDT_Byte, 0, 0) == CE_None;
		}
	}
	if (!ok)
		setError(error, tr("Cannot write %1: %2").arg(file, gdalMessage()));
	return ok;
}

bool RasterFileWriter::close(QString* error)
{
	if (!dataset)
		return false;
	CPLErrorReset();
	GDALClose(GDALDatasetH(dataset));
	dataset = nullptr;
	releaseWriteCache();
	if (CPLGetLastErrorType() >= CE_Failure)
	{
		setError(error, tr("Cannot write %1: %2").arg(file, gdalMessage()));
		return false;
	}
	return true;
}

void RasterFileWriter::discard()
{
	if (dataset)
	{
		GDALClose(GDALDatasetH(dataset));
		dataset = nullptr;
		releaseWriteCache();
	}
	if (!file.isEmpty())
		QFile::remove(file);
}



QImage readRasterFile(const QString& path, QRectF* extent, QString* error)
{
	GDALAllRegister();
	auto handle = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!handle)
	{
		setError(error, GridTileCache::tr("GDAL cannot open %1: %2").arg(path, gdalMessage()));
		return {};
	}
	auto const width = GDALGetRasterXSize(handle);
	auto const height = GDALGetRasterYSize(handle);
	auto const bands = GDALGetRasterCount(handle);
	QImage image;
	std::vector<quint8> values(std::size_t(width) * std::size_t(height));
	auto readBand = [&](int b) {
		return GDALRasterIO(GDALGetRasterBand(handle, b), GF_Read, 0, 0, width, height, values.data(), width, height, GDT_Byte, 0, 0) == CE_None;
	};
	if (bands == 1 && readBand(1))
	{
		auto const table = GDALGetRasterColorTable(GDALGetRasterBand(handle, 1));
		image = QImage(width, height, table ? QImage::Format_Indexed8 : QImage::Format_Grayscale8);
		if (table)
		{
			QVector<QRgb> colours;
			for (int i = 0; i < GDALGetColorEntryCount(table); ++i)
			{
				auto const* entry = GDALGetColorEntry(table, i);
				colours.push_back(qRgb(entry->c1, entry->c2, entry->c3));
			}
			image.setColorTable(colours);
		}
		for (int r = 0; r < height; ++r)
			std::copy(values.begin() + std::ptrdiff_t(r) * width, values.begin() + std::ptrdiff_t(r + 1) * width, image.scanLine(r));
	}
	else if (bands >= 3)
	{
		image = QImage(width, height, QImage::Format_RGB32);
		image.fill(qRgb(0, 0, 0));
		for (int b = 0; b < 3 && !image.isNull(); ++b)
		{
			if (!readBand(b + 1))
			{
				image = QImage();
				break;
			}
			for (int r = 0; r < height; ++r)
			{
				auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(r));
				for (int c = 0; c < width; ++c)
				{
					auto const v = values[std::size_t(r) * std::size_t(width) + std::size_t(c)];
					auto const p = pixels[c];
					pixels[c] = b == 0 ? qRgb(v, qGreen(p), qBlue(p)) : b == 1 ? qRgb(qRed(p), v, qBlue(p)) : qRgb(qRed(p), qGreen(p), v);
				}
			}
		}
	}
	if (image.isNull())
		setError(error, GridTileCache::tr("Cannot read %1: %2").arg(path, gdalMessage()));
	double gt[6] = { 0, 1, 0, 0, 0, -1 };
	if (extent && GDALGetGeoTransform(handle, gt) == CE_None)
		*extent = QRectF(gt[0], gt[3] + height * gt[5], width * gt[1], -height * gt[5]);
	GDALClose(handle);
	return image;
}



// ### GridTileCache ###

// static
qint64 GridTileCache::defaultMemoryLimit()
{
	return qint64(256) << 20;
}

// static
std::unique_ptr<GridTileCache> GridTileCache::open(const QString& path, qint64 memory_limit, QString* error)
{
	GDALAllRegister();
	auto handle = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!handle)
	{
		setError(error, tr("GDAL cannot open %1: %2").arg(path, gdalMessage()));
		return nullptr;
	}
	double gt[6] = { 0, 1, 0, 0, 0, -1 };
	QString message;
	if (GDALGetRasterCount(handle) < 1)
		message = tr("%1 has no raster band").arg(path);
	else if (GDALGetGeoTransform(handle, gt) != CE_None)
		message = tr("%1 has no georeferencing").arg(path);
	else if (gt[2] != 0.0 || gt[4] != 0.0 || gt[5] >= 0.0)
		message = tr("%1 is not a north-up grid").arg(path);
	else if (gt[1] <= 0.0 || qAbs(qAbs(gt[5]) - gt[1]) > 1e-6 * gt[1])
		message = tr("%1 has non-square cells").arg(path);
	if (!message.isEmpty())
	{
		GDALClose(handle);
		setError(error, message);
		return nullptr;
	}
	std::unique_ptr<GridTileCache> cache(new GridTileCache());
	cache->dataset = handle;
	cache->file = path;
	cache->grid_width = GDALGetRasterXSize(handle);
	cache->grid_height = GDALGetRasterYSize(handle);
	cache->top_left = QPointF(gt[0], gt[3]);
	cache->cell = gt[1];
	if (auto const* projection = GDALGetProjectionRef(handle))
		cache->crs = QString::fromUtf8(projection);
	int has = 0;
	cache->nodata = GDALGetRasterNoDataValue(GDALGetRasterBand(handle, 1), &has);
	cache->has_nodata = has != 0;
	cache->limit = memory_limit > 0 ? memory_limit : defaultMemoryLimit();
	cache->blocks_per_row = (cache->grid_width + block_size - 1) / block_size;
	return cache;
}

GridTileCache::~GridTileCache()
{
	if (dataset)
		GDALClose(GDALDatasetH(dataset));
}

QRectF GridTileCache::extent() const
{
	return { top_left.x(), top_left.y() - grid_height * cell, grid_width * cell, grid_height * cell };
}

const GridTileCache::Block& GridTileCache::block(int block_col, int block_row) const
{
	auto const key = qint64(block_row) * blocks_per_row + block_col;
	auto found = blocks.find(key);
	if (found != blocks.end())
	{
		uses.splice(uses.begin(), uses, found->second.use);
		return found->second;
	}
	auto const col0 = block_col * block_size;
	auto const row0 = block_row * block_size;
	auto const cols = std::min(block_size, grid_width - col0);
	auto const rows = std::min(block_size, grid_height - row0);
	auto const bytes = qint64(cols) * rows * qint64(sizeof(float));
	// Room for the new block, keeping at least none.
	while (!uses.empty() && bytes_held + bytes > limit)
	{
		auto const oldest = uses.back();
		uses.pop_back();
		auto const it = blocks.find(oldest);
		bytes_held -= qint64(it->second.values.size() * sizeof(float));
		blocks.erase(it);
	}
	Block fresh;
	fresh.cols = cols;
	fresh.values.resize(std::size_t(cols) * std::size_t(rows));
	auto band = GDALGetRasterBand(GDALDatasetH(dataset), 1);
	++blocks_read;
	if (GDALRasterIO(band, GF_Read, col0, row0, cols, rows, fresh.values.data(), cols, rows, GDT_Float32, 0, 0) != CE_None)
	{
		read_error = tr("Cannot read %1: %2").arg(file, gdalMessage());
		failed.cols = cols;
		failed.values.assign(fresh.values.size(), std::numeric_limits<float>::quiet_NaN());
		return failed;
	}
	if (has_nodata)
	{
		auto const nodata_f = float(nodata);
		for (auto& v : fresh.values)
		{
			if (v == nodata_f)
				v = std::numeric_limits<float>::quiet_NaN();
		}
	}
	uses.push_front(key);
	fresh.use = uses.begin();
	bytes_held += bytes;
	peak_bytes = std::max(peak_bytes, bytes_held);
	return blocks.emplace(key, std::move(fresh)).first->second;
}

float GridTileCache::value(int col, int row) const
{
	if (col < 0 || row < 0 || col >= grid_width || row >= grid_height)
		return std::numeric_limits<float>::quiet_NaN();
	auto const& b = block(col / block_size, row / block_size);
	return b.values[std::size_t(row % block_size) * std::size_t(b.cols) + std::size_t(col % block_size)];
}

bool GridTileCache::cellAt(const QPointF& projected, int& col, int& row) const
{
	// As ElevationModel::cellAt().
	auto const fx = (projected.x() - top_left.x()) / cell;
	auto const fy = (top_left.y() - projected.y()) / cell;
	if (fx < 0.0 || fy < 0.0 || fx > grid_width || fy > grid_height)
		return false;
	col = qBound(0, int(std::floor(fx)), grid_width - 1);
	row = qBound(0, int(std::floor(fy)), grid_height - 1);
	return true;
}

double GridTileCache::at(const QPointF& projected) const
{
	// As ElevationModel::at(), the same arithmetic.
	auto const fx = (projected.x() - top_left.x()) / cell;
	auto const fy = (top_left.y() - projected.y()) / cell;
	if (fx < 0.0 || fy < 0.0 || fx > grid_width || fy > grid_height)
		return std::numeric_limits<double>::quiet_NaN();
	auto const cx = qBound(0.0, fx - 0.5, double(grid_width - 1));
	auto const cy = qBound(0.0, fy - 0.5, double(grid_height - 1));
	auto const c0 = qMin(int(std::floor(cx)), grid_width - 1);
	auto const r0 = qMin(int(std::floor(cy)), grid_height - 1);
	auto const c1 = qMin(c0 + 1, grid_width - 1);
	auto const r1 = qMin(r0 + 1, grid_height - 1);
	auto const tx = cx - c0;
	auto const ty = cy - r0;
	double sum = 0.0;
	const double weights[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
	const int cols[4] = { c0, c1, c0, c1 };
	const int rows[4] = { r0, r0, r1, r1 };
	for (int i = 0; i < 4; ++i)
	{
		if (weights[i] == 0.0)
			continue;
		auto const corner = value(cols[i], rows[i]);
		if (std::isnan(corner))
			return std::numeric_limits<double>::quiet_NaN();
		sum += weights[i] * corner;
	}
	return sum;
}

std::unique_ptr<ElevationModel> GridTileCache::window(int col, int row, int cols, int rows) const
{
	cols = std::max(1, cols);
	rows = std::max(1, rows);
	std::vector<float> values(std::size_t(cols) * std::size_t(rows), std::numeric_limits<float>::quiet_NaN());
	auto const c_from = std::max(0, col), c_to = std::min(grid_width, col + cols);
	auto const r_from = std::max(0, row), r_to = std::min(grid_height, row + rows);
	// Block by block, so that a window of several blocks reads each once.
	for (int br = r_from / block_size; br * block_size < r_to; ++br)
	{
		for (int bc = c_from / block_size; bc * block_size < c_to; ++bc)
		{
			auto const& b = block(bc, br);
			auto const r0 = std::max(r_from, br * block_size), r1 = std::min(r_to, (br + 1) * block_size);
			auto const c0 = std::max(c_from, bc * block_size), c1 = std::min(c_to, (bc + 1) * block_size);
			for (int r = r0; r < r1; ++r)
			{
				auto const* source = &b.values[std::size_t(r - br * block_size) * std::size_t(b.cols) + std::size_t(c0 - bc * block_size)];
				std::copy(source, source + (c1 - c0), values.begin() + std::ptrdiff_t(r - row) * cols + (c0 - col));
			}
		}
	}
	return std::make_unique<ElevationModel>(cols, rows, QPointF(top_left.x() + col * cell, top_left.y() - row * cell), cell, std::move(values), crs);
}



bool vegetationHeightToFile(const GridTileCache& dsm, const GridTileCache& dtm, const VegetationHeightOptions& options,
                            const QString& path, double tile_size, const TileProgress& progress, QString* error)
{
	// the design notes, "Grids on disk, a tile cache, and a progress bar".
	auto const step = GridTileCache::tr("Vegetation height");
	if (dsm.width() != dtm.width() || dsm.height() != dtm.height() || dsm.cellSize() != dtm.cellSize() || dsm.topLeft() != dtm.topLeft())
	{
		setError(error, GridTileCache::tr("The surface and terrain models are not on one lattice."));
		return false;
	}
	auto const width = dsm.width(), height = dsm.height();
	auto const rows_per_band = std::max(1, int(std::lround(tile_size / dsm.cellSize())));
	auto const bands = (height + rows_per_band - 1) / rows_per_band;
	if (progress && !progress(step, 0, bands))
	{
		setError(error, GridTileCache::tr("Cancelled."));
		return false;
	}
	RasterFileWriter writer;
	for (int band = 0; band < bands; ++band)
	{
		auto const r0 = band * rows_per_band;
		auto const rows = std::min(height, r0 + rows_per_band) - r0;
		// As ElevationModel::difference() on the whole grids: the terrain model
		// interpolated at the cell centres of the whole lattice.
		auto const heights = dsm.window(0, r0, width, rows);
		auto const tl = dsm.topLeft();
		auto const cell = dsm.cellSize();
		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < width; ++c)
			{
				auto const v = heights->value(c, r);
				auto const l = dtm.at(QPointF(tl.x() + (c + 0.5) * cell, tl.y() - (r0 + r + 0.5) * cell));
				heights->setValue(c, r, (std::isnan(v) || std::isnan(l)) ? std::numeric_limits<float>::quiet_NaN() : float(v - l));
			}
		}
		auto const product = vegetationHeightProduct(*heights, options);
		if (!product.error.isEmpty())
		{
			writer.discard();
			setError(error, product.error);
			return false;
		}
		if (band == 0)
		{
			auto const extent = dsm.extent();
			if (!writer.open(path, width, height, extent, dsm.crsSpec(), product.image.format(), product.image.colorTable(), error))
				return false;
		}
		if (!writer.writeRows(r0, product.image, error))
		{
			writer.discard();
			return false;
		}
		if (progress && !progress(step, band + 1, bands))
		{
			writer.discard();
			setError(error, GridTileCache::tr("Cancelled."));
			return false;
		}
	}
	return writer.close(error);
}


}  // namespace OpenOrienteering
