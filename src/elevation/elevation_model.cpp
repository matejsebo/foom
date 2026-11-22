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


#include "elevation_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include <QtGlobal>
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QTextStream>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_srs_api.h>


namespace OpenOrienteering {

namespace {

QString last_error;  // NOLINT: set by open() and save(), read by lastError()

constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
constexpr float nodata_out = -9999.0f;

/// An .ocdDem grid file: 12 bytes of signature, ten int32 in centimetres, one int32 per cell.
const char ocd_dem_signature[12] = { '\x08', 'O', 'C', 'A', 'D', '_', 'D', 'E', 'M', '\x0c', '\0', '\0' };

void ensureGdal()
{
	static bool done = false;
	if (!done)
	{
		GDALAllRegister();
		done = true;
	}
}

QByteArray wktFromSpec(const QString& spec)
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

bool isOcdDem(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	auto const head = file.read(12);
	return head.size() == 12 && std::memcmp(head.constData(), ocd_dem_signature, 12) == 0;
}

QString formatNumber(double value)
{
	auto text = QString::number(value, 'f', 3);
	while (text.endsWith(QLatin1Char('0')))
		text.chop(1);
	if (text.endsWith(QLatin1Char('.')))
		text.chop(1);
	return text;
}

}  // namespace



ElevationModel::ElevationModel(int width, int height, const QPointF& top_left, double cell_size, std::vector<float> values, QString crs_spec)
: grid_width(width)
, grid_height(height)
, top_left(top_left)
, cell(cell_size)
, data(std::move(values))
, crs(std::move(crs_spec))
{
	Q_ASSERT(width > 0 && height > 0 && cell_size > 0.0);
	data.resize(std::size_t(width) * std::size_t(height), nan);
}


// static
QString ElevationModel::lastError()
{
	return last_error;
}


// static
std::unique_ptr<ElevationModel> ElevationModel::open(const QString& path, const OpenOptions& options)
{
	last_error.clear();
	if (!QFileInfo::exists(path))
	{
		last_error = tr("File not found: %1").arg(path);
		return nullptr;
	}

	auto grid = isOcdDem(path) ? openOcdDem(path) : openGdal(path, options.target_crs);
	if (!grid)
		return nullptr;

	if (options.feet_to_metres || options.millimetres_to_metres)
	{
		auto const factor = float((options.feet_to_metres ? 0.3048 : 1.0) * (options.millimetres_to_metres ? 0.001 : 1.0));
		for (auto& v : grid->data)
			v *= factor;
	}
	if (options.shift_below_sea_level)
	{
		auto const minimum = grid->info().minimum;
		if (minimum < 0.0)
		{
			auto const shift = float(-minimum);
			for (auto& v : grid->data)
				v += shift;
		}
	}
	return grid;
}


// static
std::unique_ptr<ElevationModel> ElevationModel::openGdal(const QString& path, const QString& target_crs)
{
	ensureGdal();
	auto dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!dataset)
	{
		last_error = tr("GDAL cannot open %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
		return nullptr;
	}

	auto source = dataset;
	GDALDatasetH warped = nullptr;
	QString crs_spec;
	if (auto const* projection = GDALGetProjectionRef(dataset))
		crs_spec = QString::fromUtf8(projection);
	if (!target_crs.isEmpty() && !crs_spec.isEmpty() && !sameCrs(crs_spec, target_crs))
	{
		auto const target_wkt = wktFromSpec(target_crs);
		if (!target_wkt.isEmpty())
		{
			warped = GDALAutoCreateWarpedVRT(dataset, nullptr, target_wkt.constData(), GRA_Bilinear, 0.0, nullptr);
			if (warped)
			{
				source = warped;
				crs_spec = target_crs;
			}
		}
	}

	std::unique_ptr<ElevationModel> grid;
	double gt[6] = { 0, 1, 0, 0, 0, -1 };
	if (GDALGetRasterCount(source) < 1)
		last_error = tr("%1 has no raster band").arg(path);
	else if (GDALGetGeoTransform(source, gt) != CE_None)
		last_error = tr("%1 has no georeferencing").arg(path);
	else if (gt[2] != 0.0 || gt[4] != 0.0)
		last_error = tr("%1 is rotated; only north-up grids are supported").arg(path);
	else if (gt[1] <= 0.0 || qAbs(qAbs(gt[5]) - gt[1]) > 1e-6 * gt[1])
		last_error = tr("%1 has non-square cells").arg(path);
	else
	{
		auto const width = GDALGetRasterXSize(source);
		auto const height = GDALGetRasterYSize(source);
		auto band = GDALGetRasterBand(source, 1);
		std::vector<float> values(std::size_t(width) * std::size_t(height));
		if (GDALRasterIO(band, GF_Read, 0, 0, width, height, values.data(), width, height, GDT_Float32, 0, 0) != CE_None)
		{
			last_error = tr("Cannot read %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
		}
		else
		{
			int has_nodata = 0;
			auto const nodata = GDALGetRasterNoDataValue(band, &has_nodata);
			if (has_nodata)
			{
				auto const nodata_f = float(nodata);
				for (auto& v : values)
					if (v == nodata_f || std::isnan(v))
						v = nan;
			}
			// A south-up file (positive gt[5]) is flipped to the northern row first.
			auto top = gt[3];
			if (gt[5] > 0.0)
			{
				top = gt[3] + height * gt[5];
				for (int r = 0; r < height / 2; ++r)
					std::swap_ranges(values.begin() + std::ptrdiff_t(r) * width, values.begin() + std::ptrdiff_t(r + 1) * width,
					                 values.begin() + std::ptrdiff_t(height - 1 - r) * width);
			}
			grid = std::make_unique<ElevationModel>(width, height, QPointF(gt[0], top), gt[1], std::move(values), crs_spec);
		}
	}

	if (warped)
		GDALClose(warped);
	GDALClose(dataset);
	return grid;
}


// static
std::unique_ptr<ElevationModel> ElevationModel::openOcdDem(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		last_error = tr("Cannot read %1").arg(path);
		return nullptr;
	}
	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.skipRawData(12);
	qint32 header[10];
	for (auto& h : header)
		stream >> h;
	auto const width = header[0], height = header[1];
	auto const cell_cm = header[8];
	if (stream.status() != QDataStream::Ok || width <= 0 || height <= 0 || cell_cm <= 0 || header[9] != cell_cm)
	{
		last_error = tr("%1 is not a valid OCD grid").arg(path);
		return nullptr;
	}
	auto const cell_size = cell_cm / 100.0;
	auto const left = header[2] / 100.0 - cell_size / 2;
	auto const top = header[5] / 100.0 + cell_size / 2;
	std::vector<float> values(std::size_t(width) * std::size_t(height));
	for (auto& v : values)
	{
		qint32 cm;
		stream >> cm;
		v = (cm == std::numeric_limits<qint32>::min() || cm == -999900) ? nan : float(cm / 100.0);
	}
	if (stream.status() != QDataStream::Ok)
	{
		last_error = tr("%1 is truncated").arg(path);
		return nullptr;
	}
	return std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell_size, std::move(values));
}


// static
QString ElevationModel::geoTiffMetadata(const QString& path)
{
	ensureGdal();
	auto dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!dataset)
		return {};
	QString result;
	if (GDALGetRasterCount(dataset) > 0)
	{
		auto band = GDALGetRasterBand(dataset, 1);
		result = QString::fromUtf8(GDALGetDataTypeName(GDALGetRasterDataType(band)));
		if (auto const* compression = GDALGetMetadataItem(dataset, "COMPRESSION", "IMAGE_STRUCTURE"))
			result += QLatin1String(" COMPRESSION=") + QString::fromUtf8(compression);
		int block_x = 0, block_y = 0;
		GDALGetBlockSize(band, &block_x, &block_y);
		// Strips span the raster width; tiles are 256 or so wide whatever the raster.
		result += (block_x != GDALGetRasterXSize(dataset)) ? QLatin1String(" TILED") : QLatin1String(" STRIPPED");
	}
	GDALClose(dataset);
	return result;
}


// static
bool ElevationModel::sameCrs(const QString& a, const QString& b)
{
	if (a == b)
		return true;
	if (a.isEmpty() || b.isEmpty())
		return false;
	ensureGdal();
	auto srs_a = OSRNewSpatialReference(nullptr);
	auto srs_b = OSRNewSpatialReference(nullptr);
	bool same = false;
	if (OSRSetFromUserInput(srs_a, a.toUtf8().constData()) == OGRERR_NONE
	    && OSRSetFromUserInput(srs_b, b.toUtf8().constData()) == OGRERR_NONE)
	{
		const char* options[] = { "IGNORE_DATA_AXIS_TO_SRS_AXIS_MAPPING=YES", nullptr };
		same = OSRIsSameEx(srs_a, srs_b, options) != 0;
	}
	OSRDestroySpatialReference(srs_a);
	OSRDestroySpatialReference(srs_b);
	return same;
}



QRectF ElevationModel::extent() const
{
	return QRectF(top_left.x(), top_left.y() - grid_height * cell, grid_width * cell, grid_height * cell);
}


QPointF ElevationModel::cellCenter(int col, int row) const
{
	return QPointF(top_left.x() + (col + 0.5) * cell, top_left.y() - (row + 0.5) * cell);
}


bool ElevationModel::cellAt(const QPointF& projected, int& col, int& row) const
{
	auto const fx = (projected.x() - top_left.x()) / cell;
	auto const fy = (top_left.y() - projected.y()) / cell;
	if (fx < 0.0 || fy < 0.0 || fx > grid_width || fy > grid_height)
		return false;
	col = qBound(0, int(std::floor(fx)), grid_width - 1);
	row = qBound(0, int(std::floor(fy)), grid_height - 1);
	return true;
}


double ElevationModel::at(const QPointF& projected) const
{
	auto const fx = (projected.x() - top_left.x()) / cell;
	auto const fy = (top_left.y() - projected.y()) / cell;
	if (fx < 0.0 || fy < 0.0 || fx > grid_width || fy > grid_height)
		return std::numeric_limits<double>::quiet_NaN();

	// Interpolate between cell centres; constant beyond the outermost centres.
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
	const float corners[4] = { value(c0, r0), value(c1, r0), value(c0, r1), value(c1, r1) };
	for (int i = 0; i < 4; ++i)
	{
		if (weights[i] == 0.0)
			continue;
		if (std::isnan(corners[i]))
			return std::numeric_limits<double>::quiet_NaN();
		sum += weights[i] * corners[i];
	}
	return sum;
}


namespace {

/// Keys' cubic convolution kernel with a = -0.5 of the distance in cells.
double keysWeight(double d)
{
	d = std::abs(d);
	if (d <= 1.0)
		return (1.5 * d - 2.5) * d * d + 1.0;
	if (d < 2.0)
		return ((-0.5 * d + 2.5) * d - 4.0) * d + 2.0;
	return 0.0;
}

}  // namespace


double ElevationModel::atBicubic(const QPointF& projected) const
{
	auto const fx = (projected.x() - top_left.x()) / cell;
	auto const fy = (top_left.y() - projected.y()) / cell;
	if (fx < 0.0 || fy < 0.0 || fx > grid_width || fy > grid_height)
		return std::numeric_limits<double>::quiet_NaN();

	// As at(): constant beyond the outermost centres; neighbours beyond the edge repeat it.
	auto const cx = qBound(0.0, fx - 0.5, double(grid_width - 1));
	auto const cy = qBound(0.0, fy - 0.5, double(grid_height - 1));
	auto const c0 = qMin(int(std::floor(cx)), grid_width - 1);
	auto const r0 = qMin(int(std::floor(cy)), grid_height - 1);
	auto const tx = cx - c0;
	auto const ty = cy - r0;
	double wx[4], wy[4];
	for (int i = 0; i < 4; ++i)
	{
		wx[i] = keysWeight(tx - (i - 1));
		wy[i] = keysWeight(ty - (i - 1));
	}

	double sum = 0.0;
	for (int j = 0; j < 4; ++j)
	{
		if (wy[j] == 0.0)
			continue;
		auto const row = qBound(0, r0 + j - 1, grid_height - 1);
		for (int i = 0; i < 4; ++i)
		{
			if (wx[i] == 0.0)
				continue;
			auto const v = value(qBound(0, c0 + i - 1, grid_width - 1), row);
			if (std::isnan(v))
				return at(projected);
			sum += wx[i] * wy[j] * v;
		}
	}
	return sum;
}


QPointF ElevationModel::cornerGradientAt(int col, int row) const
{
	auto const a = double(value(col, row));          // north-west
	auto const b = double(value(col + 1, row));      // north-east
	auto const c = double(value(col, row + 1));      // south-west
	auto const d = double(value(col + 1, row + 1));  // south-east
	if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
		return QPointF(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
	return QPointF(((b + d) - (a + c)) / (2.0 * cell), ((a + b) - (c + d)) / (2.0 * cell));
}


QPointF ElevationModel::gradient(const QPointF& projected) const
{
	int col, row;
	if (!cellAt(projected, col, row))
		return QPointF(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
	return gradientAt(col, row);
}


QPointF ElevationModel::gradientAt(int col, int row) const
{
	auto const c0 = qMax(col - 1, 0), c2 = qMin(col + 1, grid_width - 1);
	auto const r0 = qMax(row - 1, 0), r2 = qMin(row + 1, grid_height - 1);
	auto const a = value(c0, r0), b = value(col, r0), c = value(c2, r0);
	auto const d = value(c0, row),                    f = value(c2, row);
	auto const g = value(c0, r2), h = value(col, r2), i = value(c2, r2);
	auto const dzdx = ((c + 2.0 * f + i) - (a + 2.0 * d + g)) / (8.0 * cell);
	auto const dzdy = ((a + 2.0 * b + c) - (g + 2.0 * h + i)) / (8.0 * cell);
	return QPointF(dzdx, dzdy);  // NaN propagates from void neighbours
}


ElevationModel::Info ElevationModel::info() const
{
	Info info;
	info.minimum = std::numeric_limits<double>::infinity();
	info.maximum = -std::numeric_limits<double>::infinity();
	for (auto const v : data)
	{
		if (std::isnan(v))
			++info.void_count;
		else
		{
			info.minimum = qMin(info.minimum, double(v));
			info.maximum = qMax(info.maximum, double(v));
		}
	}
	if (info.void_count == qint64(data.size()))
		info.minimum = info.maximum = std::numeric_limits<double>::quiet_NaN();
	return info;
}



QStringList ElevationModel::saveAsciiTiles(const QString& path, int points) const
{
	// the design notes, "Tiled export": tiles of points by points cells
	// from the north-west, neighbours sharing their edge row or column.
	last_error.clear();
	QStringList written;
	if (points < 2)
	{
		last_error = tr("A tile needs at least two points a side.");
		return written;
	}
	QFileInfo const info(path);
	auto const base = info.dir().absoluteFilePath(info.completeBaseName());
	auto const step = points - 1;
	for (int r0 = 0, tile_row = 0; ; r0 += step, ++tile_row)
	{
		auto const r1 = std::min(grid_height, r0 + points);
		for (int c0 = 0, tile_col = 0; ; c0 += step, ++tile_col)
		{
			auto const c1 = std::min(grid_width, c0 + points);
			std::vector<float> values;
			values.reserve(std::size_t(c1 - c0) * std::size_t(r1 - r0));
			for (int r = r0; r < r1; ++r)
				values.insert(values.end(), data.begin() + std::ptrdiff_t(index(c0, r)), data.begin() + std::ptrdiff_t(index(c0, r) + std::size_t(c1 - c0)));
			ElevationModel tile(c1 - c0, r1 - r0, QPointF(top_left.x() + c0 * cell, top_left.y() - r0 * cell), cell, std::move(values), crs);
			auto const tile_path = QStringLiteral("%1_%2_%3.asc").arg(base).arg(tile_row).arg(tile_col);
			if (!tile.saveAsciiGrid(tile_path))
				return {};
			written.push_back(tile_path);
			if (c1 >= grid_width)
				break;
		}
		if (r1 >= grid_height)
			break;
	}
	return written;
}


bool ElevationModel::save(const QString& path) const
{
	last_error.clear();
	auto const suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QLatin1String("asc"))
		return saveAsciiGrid(path);
	if (suffix == QLatin1String("xyz"))
		return saveXyz(path);
	if (suffix == QLatin1String("ocddem"))
		return saveOcdDem(path);
	return saveGeoTiff(path);
}


bool ElevationModel::saveGeoTiff(const QString& path) const
{
	ensureGdal();
	auto driver = GDALGetDriverByName("GTiff");
	if (!driver)
	{
		last_error = tr("GDAL has no GeoTIFF driver");
		return false;
	}
	char** options = nullptr;
	options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
	options = CSLSetNameValue(options, "TILED", "YES");
	options = CSLSetNameValue(options, "PREDICTOR", "3");
	auto dataset = GDALCreate(driver, path.toUtf8().constData(), grid_width, grid_height, 1, GDT_Float32, options);
	CSLDestroy(options);
	if (!dataset)
	{
		last_error = tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
		return false;
	}
	double gt[6] = { top_left.x(), cell, 0.0, top_left.y(), 0.0, -cell };
	GDALSetGeoTransform(dataset, gt);
	auto const wkt = wktFromSpec(crs);
	if (!wkt.isEmpty())
		GDALSetProjection(dataset, wkt.constData());
	auto band = GDALGetRasterBand(dataset, 1);
	GDALSetRasterNoDataValue(band, nodata_out);
	std::vector<float> out(data);
	for (auto& v : out)
		if (std::isnan(v))
			v = nodata_out;
	auto const ok = GDALRasterIO(band, GF_Write, 0, 0, grid_width, grid_height, out.data(), grid_width, grid_height, GDT_Float32, 0, 0) == CE_None;
	GDALClose(dataset);
	if (!ok)
		last_error = tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
	return ok;
}


bool ElevationModel::saveAsciiGrid(const QString& path) const
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		last_error = tr("Cannot write %1").arg(path);
		return false;
	}
	QTextStream out(&file);
	out << "ncols " << grid_width << "\n"
	    << "nrows " << grid_height << "\n"
	    << "xllcorner " << formatNumber(top_left.x()) << "\n"
	    << "yllcorner " << formatNumber(top_left.y() - grid_height * cell) << "\n"
	    << "cellsize " << formatNumber(cell) << "\n"
	    << "NODATA_value -9999\n";
	for (int r = 0; r < grid_height; ++r)
	{
		for (int c = 0; c < grid_width; ++c)
		{
			auto const v = value(c, r);
			if (c > 0)
				out << ' ';
			out << (std::isnan(v) ? QString::fromLatin1("-9999") : QString::number(v, 'f', 3));
		}
		out << '\n';
	}
	return true;
}


bool ElevationModel::saveXyz(const QString& path) const
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		last_error = tr("Cannot write %1").arg(path);
		return false;
	}
	QTextStream out(&file);
	for (int r = 0; r < grid_height; ++r)
		for (int c = 0; c < grid_width; ++c)
		{
			auto const v = value(c, r);
			if (std::isnan(v))
				continue;
			auto const center = cellCenter(c, r);
			out << QString::number(center.x(), 'f', 3) << ' ' << QString::number(center.y(), 'f', 3) << ' ' << QString::number(v, 'f', 3) << '\n';
		}
	return true;
}


bool ElevationModel::saveOcdDem(const QString& path) const
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		last_error = tr("Cannot write %1").arg(path);
		return false;
	}
	auto const stats = info();
	auto const cm = [](double v) { return qint32(qRound64(v * 100.0)); };
	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.writeRawData(ocd_dem_signature, 12);
	stream << qint32(grid_width) << qint32(grid_height)
	       << cm(top_left.x() + cell / 2) << cm(top_left.x() + (grid_width - 0.5) * cell)
	       << cm(top_left.y() - (grid_height - 0.5) * cell) << cm(top_left.y() - cell / 2)
	       << (std::isnan(stats.minimum) ? qint32(0) : cm(stats.minimum)) << (std::isnan(stats.maximum) ? qint32(0) : cm(stats.maximum))
	       << cm(cell) << cm(cell);
	for (auto const v : data)
		stream << (std::isnan(v) ? qint32(-999900) : cm(v));
	return stream.status() == QDataStream::Ok;
}



std::unique_ptr<ElevationModel> ElevationModel::smoothed(int radius) const
{
	auto result = std::make_unique<ElevationModel>(*this);
	if (grid_width < 3 || grid_height < 3)
		return result;
	std::vector<float> source;
	for (int pass = 0; pass < radius; ++pass)
	{
		source = result->data;
		for (int r = 1; r + 1 < grid_height; ++r)
			for (int c = 1; c + 1 < grid_width; ++c)
			{
				double sum = 0.0;
				bool valid = true;
				for (int dr = -1; dr <= 1 && valid; ++dr)
					for (int dc = -1; dc <= 1; ++dc)
					{
						auto const v = source[index(c + dc, r + dr)];
						if (std::isnan(v))
						{
							valid = false;
							break;
						}
						sum += (dr == 0 ? 2.0 : 1.0) * (dc == 0 ? 2.0 : 1.0) * v;
					}
				if (valid)
					result->data[index(c, r)] = float(sum / 16.0);
			}
	}
	return result;
}


std::unique_ptr<ElevationModel> ElevationModel::cropped(const QRectF& box) const
{
	auto const c0 = qBound(0, int(std::floor((box.left() - top_left.x()) / cell + 1e-9)), grid_width - 1);
	auto const c1 = qBound(c0 + 1, int(std::ceil((box.right() - top_left.x()) / cell - 1e-9)), grid_width);
	auto const r0 = qBound(0, int(std::floor((top_left.y() - box.bottom()) / cell + 1e-9)), grid_height - 1);
	auto const r1 = qBound(r0 + 1, int(std::ceil((top_left.y() - box.top()) / cell - 1e-9)), grid_height);
	auto const width = c1 - c0, height = r1 - r0;
	std::vector<float> values(std::size_t(width) * std::size_t(height));
	for (int r = 0; r < height; ++r)
		std::copy_n(data.begin() + std::ptrdiff_t(index(c0, r0 + r)), width, values.begin() + std::ptrdiff_t(r) * width);
	return std::make_unique<ElevationModel>(width, height, QPointF(top_left.x() + c0 * cell, top_left.y() - r0 * cell), cell, std::move(values), crs);
}


std::unique_ptr<ElevationModel> ElevationModel::resampled(double cell_size, Interpolation interpolation) const
{
	if (cell_size <= 0.0 || qAbs(cell_size - cell) < 1e-9 * cell)
		return std::make_unique<ElevationModel>(*this);
	auto const width = qMax(1, int(std::ceil(grid_width * cell / cell_size - 1e-9)));
	auto const height = qMax(1, int(std::ceil(grid_height * cell / cell_size - 1e-9)));
	auto result = std::make_unique<ElevationModel>(width, height, top_left, cell_size, std::vector<float>{}, crs);
	for (int r = 0; r < height; ++r)
		for (int c = 0; c < width; ++c)
		{
			auto const center = result->cellCenter(c, r);
			result->setValue(c, r, float(interpolation == Interpolation::Bicubic ? atBicubic(center) : at(center)));
		}
	return result;
}


// static
std::unique_ptr<ElevationModel> ElevationModel::merge(const std::vector<const ElevationModel*>& grids)
{
	last_error.clear();
	if (grids.empty())
	{
		last_error = tr("No grid to merge");
		return nullptr;
	}
	auto const& first = *grids.front();
	auto const cell = first.cellSize();
	auto bounds = first.extent();
	for (auto const* grid : grids)
	{
		if (qAbs(grid->cellSize() - cell) > 1e-9 * cell)
		{
			last_error = tr("The grids have different cell sizes");
			return nullptr;
		}
		bounds |= grid->extent();
	}
	// Snap the union to the first grid's lattice.
	auto const left = first.top_left.x() - std::ceil((first.top_left.x() - bounds.left()) / cell - 1e-9) * cell;
	auto const top = first.top_left.y() + std::ceil((bounds.bottom() - first.top_left.y()) / cell - 1e-9) * cell;
	auto const width = int(std::ceil((bounds.right() - left) / cell - 1e-9));
	auto const height = int(std::ceil((top - bounds.top()) / cell - 1e-9));
	auto result = std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell, std::vector<float>{}, first.crs);
	for (int r = 0; r < height; ++r)
		for (int c = 0; c < width; ++c)
		{
			auto const center = result->cellCenter(c, r);
			for (auto it = grids.rbegin(); it != grids.rend(); ++it)
			{
				int col, row;
				if ((*it)->cellAt(center, col, row) && !std::isnan((*it)->value(col, row)))
				{
					result->setValue(c, r, (*it)->value(col, row));
					break;
				}
			}
		}
	return result;
}


// static
std::unique_ptr<ElevationModel> ElevationModel::difference(const ElevationModel& upper, const ElevationModel& lower)
{
	last_error.clear();
	auto const overlap = upper.extent() & lower.extent();
	if (overlap.isEmpty())
	{
		last_error = tr("The grids do not overlap");
		return nullptr;
	}
	auto result = upper.cropped(overlap);
	for (int r = 0; r < result->height(); ++r)
		for (int c = 0; c < result->width(); ++c)
		{
			auto const v = result->value(c, r);
			auto const l = lower.at(result->cellCenter(c, r));
			result->setValue(c, r, (std::isnan(v) || std::isnan(l)) ? nan : float(v - l));
		}
	return result;
}


}  // namespace OpenOrienteering
