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

#include "point_cloud.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QPointF>

#include "elevation/elevation_model.h"

#ifdef FOOM_WITH_LASZIP
#include <laszip/laszip_api.h>
#endif


namespace OpenOrienteering {

GriddingResult::GriddingResult() = default;
GriddingResult::GriddingResult(GriddingResult&&) = default;
GriddingResult& GriddingResult::operator=(GriddingResult&&) = default;
GriddingResult::~GriddingResult() = default;


namespace {

/// The largest grid, in cells (10 by 10 km at 1 m).
constexpr qint64 max_cells = 100000000;

/// Floor and ceiling which forgive the rounding of coordinates read with a scale.
double floorTo(double value, double cell)
{
	return std::floor(value / cell + 1e-9) * cell;
}

double ceilTo(double value, double cell)
{
	return std::ceil(value / cell - 1e-9) * cell;
}

#ifdef FOOM_WITH_LASZIP

QString laszipError(laszip_POINTER reader)
{
	laszip_CHAR* message = nullptr;
	if (reader && laszip_get_error(reader, &message) == 0 && message)
		return QString::fromLocal8Bit(message);
	return {};
}

/// The system of the file's WKT record, or of its GeoTIFF key record, or empty.
QString crsOf(const laszip_header_struct& header)
{
	QString from_keys;
	for (laszip_U32 i = 0; i < header.number_of_variable_length_records; ++i)
	{
		auto const& vlr = header.vlrs[i];
		if (std::strncmp(vlr.user_id, "LASF_Projection", 16) != 0 || !vlr.data)
			continue;
		if (vlr.record_id == 2112 && vlr.record_length_after_header > 0)
		{
			auto const text = QString::fromUtf8(reinterpret_cast<const char*>(vlr.data), int(qstrnlen(reinterpret_cast<const char*>(vlr.data), vlr.record_length_after_header))).trimmed();
			if (!text.isEmpty())
				return text;
		}
		else if (vlr.record_id == 34735 && vlr.record_length_after_header >= 8)
		{
			// The GeoKeyDirectory: a header of four shorts, then four shorts per key.
			auto const count = int(vlr.record_length_after_header / 2);
			auto const short_at = [&vlr](int k) {
				return laszip_U16(vlr.data[2 * k] | (vlr.data[2 * k + 1] << 8));
			};
			auto const keys = std::min(int(short_at(3)), count / 4 - 1);
			for (int k = 1; k <= keys; ++k)
			{
				// ProjectedCSTypeGeoKey, the value in place.
				if (short_at(4 * k) == 3072 && short_at(4 * k + 1) == 0 && short_at(4 * k + 3) > 0 && short_at(4 * k + 3) < 32767)
					from_keys = QStringLiteral("EPSG:%1").arg(short_at(4 * k + 3));
			}
		}
	}
	return from_keys;
}

/// The points of one file, through a callback; selective decompression for the extent pass.
bool readFile(const QString& path, bool xy_only, const std::function<void (const CloudPoint&)>& receiver, QString* crs, QString* error)
{
	auto const set_error = [error, &path](const QString& message) {
		if (error)
			*error = PointCloud::tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), message);
	};
	if (!QFileInfo::exists(path))
	{
		set_error(PointCloud::tr("The file does not exist."));
		return false;
	}
	laszip_POINTER reader = nullptr;
	if (laszip_create(&reader) != 0)
	{
		set_error(PointCloud::tr("LASzip cannot start."));
		return false;
	}
	if (xy_only)
		laszip_decompress_selective(reader, laszip_DECOMPRESS_SELECTIVE_CHANNEL_RETURNS_XY);
	laszip_BOOL compressed = 0;
	auto const name = QDir::toNativeSeparators(path).toLocal8Bit();
	if (laszip_open_reader(reader, name.constData(), &compressed) != 0)
	{
		set_error(laszipError(reader));
		laszip_destroy(reader);
		return false;
	}
	laszip_header_struct* header = nullptr;
	laszip_point_struct* point = nullptr;
	laszip_get_header_pointer(reader, &header);
	laszip_get_point_pointer(reader, &point);
	// laszip_get_point_count() counts the points read so far.
	auto const count = header->number_of_point_records ? laszip_I64(header->number_of_point_records) : laszip_I64(header->extended_number_of_point_records);
	if (crs)
		*crs = crsOf(*header);
	auto const extended = header->point_data_format >= 6;
	auto ok = true;
	CloudPoint p;
	for (laszip_I64 i = 0; i < count; ++i)
	{
		if (laszip_read_point(reader) != 0)
		{
			set_error(laszipError(reader));
			ok = false;
			break;
		}
		p.x = point->X * header->x_scale_factor + header->x_offset;
		p.y = point->Y * header->y_scale_factor + header->y_offset;
		p.z = point->Z * header->z_scale_factor + header->z_offset;
		p.intensity = point->intensity;
		if (extended)
		{
			p.return_number = point->extended_return_number;
			p.number_of_returns = point->extended_number_of_returns;
			p.classification = point->extended_classification;
		}
		else
		{
			p.return_number = point->return_number;
			p.number_of_returns = point->number_of_returns;
			p.classification = point->classification;
		}
		receiver(p);
	}
	laszip_close_reader(reader);
	laszip_destroy(reader);
	return ok;
}

#endif

/// Fills void cells by inverse distance weighting (power 2) from the cells with points within reach.
void fillVoids(ElevationModel& grid, int reach)
{
	if (reach <= 0)
		return;
	struct Offset { int dc, dr; double weight; };
	std::vector<Offset> offsets;
	for (int dr = -reach; dr <= reach; ++dr)
	{
		for (int dc = -reach; dc <= reach; ++dc)
		{
			auto const d2 = dc * dc + dr * dr;
			if (d2 > 0 && d2 <= reach * reach)
				offsets.push_back({ dc, dr, 1.0 / d2 });
		}
	}
	auto const width = grid.width();
	auto const height = grid.height();
	auto const source = grid.values();
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			if (!std::isnan(source[std::size_t(row) * std::size_t(width) + std::size_t(col)]))
				continue;
			auto sum = 0.0;
			auto weights = 0.0;
			for (auto const& offset : offsets)
			{
				auto const c = col + offset.dc;
				auto const r = row + offset.dr;
				if (c < 0 || r < 0 || c >= width || r >= height)
					continue;
				auto const value = source[std::size_t(r) * std::size_t(width) + std::size_t(c)];
				if (std::isnan(value))
					continue;
				sum += offset.weight * double(value);
				weights += offset.weight;
			}
			if (weights > 0)
				grid.setValue(col, row, float(sum / weights));
		}
	}
}

}  // namespace



// static
bool PointCloud::supported()
{
#ifdef FOOM_WITH_LASZIP
	return true;
#else
	return false;
#endif
}


// static
bool PointCloud::read(const QString& path, const std::function<void (const CloudPoint&)>& receiver, QString* crs, QString* error)
{
#ifdef FOOM_WITH_LASZIP
	return readFile(path, false, receiver, crs, error);
#else
	Q_UNUSED(path)
	Q_UNUSED(receiver)
	Q_UNUSED(crs)
	if (error)
		*error = tr("This build of FOOM does not read point clouds.");
	return false;
#endif
}


// static
bool PointCloud::write(const QString& path, const std::vector<CloudPoint>& points, const QString& wkt, int epsg, QString* error)
{
#ifdef FOOM_WITH_LASZIP
	laszip_POINTER writer = nullptr;
	if (laszip_create(&writer) != 0)
	{
		if (error)
			*error = tr("LASzip cannot start.");
		return false;
	}
	auto const fail = [writer, error, &path]() {
		if (error)
			*error = tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), laszipError(writer));
		laszip_destroy(writer);
		return false;
	};
	laszip_header_struct* header = nullptr;
	laszip_get_header_pointer(writer, &header);
	header->version_major = 1;
	header->version_minor = 2;
	header->point_data_format = 1;
	header->point_data_record_length = 28;
	header->x_scale_factor = header->y_scale_factor = header->z_scale_factor = 0.01;
	auto min_x = std::numeric_limits<double>::max(), min_y = min_x, min_z = min_x;
	auto max_x = std::numeric_limits<double>::lowest(), max_y = max_x, max_z = max_x;
	laszip_U32 by_return[5] = {};
	for (auto const& p : points)
	{
		min_x = std::min(min_x, p.x);
		min_y = std::min(min_y, p.y);
		min_z = std::min(min_z, p.z);
		max_x = std::max(max_x, p.x);
		max_y = std::max(max_y, p.y);
		max_z = std::max(max_z, p.z);
		if (p.return_number >= 1 && p.return_number <= 5)
			++by_return[p.return_number - 1];
	}
	if (points.empty())
		min_x = min_y = min_z = max_x = max_y = max_z = 0.0;
	header->x_offset = std::floor(min_x);
	header->y_offset = std::floor(min_y);
	header->z_offset = std::floor(min_z);
	header->min_x = min_x;
	header->min_y = min_y;
	header->min_z = min_z;
	header->max_x = max_x;
	header->max_y = max_y;
	header->max_z = max_z;
	header->number_of_point_records = laszip_U32(points.size());
	std::copy(std::begin(by_return), std::end(by_return), header->number_of_points_by_return);
	if (!wkt.isEmpty())
	{
		auto const text = wkt.toUtf8();
		if (laszip_add_vlr(writer, "LASF_Projection", 2112, laszip_U16(text.size() + 1), "OGC WKT", reinterpret_cast<const laszip_U8*>(text.constData())) != 0)
			return fail();
	}
	if (epsg > 0)
	{
		laszip_geokey_struct keys[2] = {};
		keys[0].key_id = 1024;   // GTModelTypeGeoKey: projected
		keys[0].value_offset = 1;
		keys[1].key_id = 3072;   // ProjectedCSTypeGeoKey
		keys[1].count = 1;
		keys[1].value_offset = laszip_U16(epsg);
		keys[0].count = 1;
		if (laszip_set_geokeys(writer, 2, keys) != 0)
			return fail();
	}
	auto const compress = QFileInfo(path).suffix().compare(QLatin1String("laz"), Qt::CaseInsensitive) == 0;
	auto const name = QDir::toNativeSeparators(path).toLocal8Bit();
	if (laszip_open_writer(writer, name.constData(), compress) != 0)
		return fail();
	laszip_point_struct* point = nullptr;
	laszip_get_point_pointer(writer, &point);
	for (auto const& p : points)
	{
		point->X = laszip_I32(std::lround((p.x - header->x_offset) / header->x_scale_factor));
		point->Y = laszip_I32(std::lround((p.y - header->y_offset) / header->y_scale_factor));
		point->Z = laszip_I32(std::lround((p.z - header->z_offset) / header->z_scale_factor));
		point->intensity = p.intensity;
		point->return_number = p.return_number & 7;
		point->number_of_returns = p.number_of_returns & 7;
		point->classification = p.classification & 31;
		if (laszip_write_point(writer) != 0)
			return fail();
	}
	if (laszip_close_writer(writer) != 0)
		return fail();
	laszip_destroy(writer);
	return true;
#else
	Q_UNUSED(path)
	Q_UNUSED(points)
	Q_UNUSED(wkt)
	Q_UNUSED(epsg)
	if (error)
		*error = tr("This build of FOOM does not read point clouds.");
	return false;
#endif
}


// static
PointCloudInfo PointCloud::analyse(const QStringList& paths)
{
	PointCloudInfo info;
	auto min_x = std::numeric_limits<double>::max(), min_y = min_x;
	auto max_x = std::numeric_limits<double>::lowest(), max_y = max_x;
	info.z_min = std::numeric_limits<double>::max();
	info.z_max = std::numeric_limits<double>::lowest();
	auto const add = [&](const CloudPoint& p) {
		++info.count;
		min_x = std::min(min_x, p.x);
		min_y = std::min(min_y, p.y);
		max_x = std::max(max_x, p.x);
		max_y = std::max(max_y, p.y);
		info.z_min = std::min(info.z_min, p.z);
		info.z_max = std::max(info.z_max, p.z);
		++info.per_class[p.classification];
	};
	for (auto const& path : paths)
	{
		QString crs;
		if (!read(path, add, &crs, &info.error))
			return info;
		if (info.crs_spec.isEmpty())
			info.crs_spec = crs;
	}
	if (info.count == 0)
	{
		info.z_min = info.z_max = 0.0;
		return info;
	}
	info.extent = QRectF(min_x, min_y, max_x - min_x, max_y - min_y);
	auto const area = info.extent.width() * info.extent.height();
	info.density = area > 0 ? info.count / area : 0.0;
	return info;
}


// static
GriddingResult PointCloud::grid(const QStringList& paths, const GriddingParams& params)
{
	// the design notes, "Reading point clouds and gridding them".
	GriddingResult result;
	if (!supported())
	{
		result.error = tr("This build of FOOM does not read point clouds.");
		return result;
	}
	if (!(params.cell > 0) || paths.isEmpty() || (!params.dtm && !params.dsm))
	{
		result.error = tr("Nothing to grid.");
		return result;
	}
	auto const cell = params.cell;
	
	// The edges.
	QRectF bounds = params.box;
	if (bounds.isEmpty())
	{
		auto min_x = std::numeric_limits<double>::max(), min_y = min_x;
		auto max_x = std::numeric_limits<double>::lowest(), max_y = max_x;
		qint64 count = 0;
		auto const extend = [&](const CloudPoint& p) {
			++count;
			min_x = std::min(min_x, p.x);
			min_y = std::min(min_y, p.y);
			max_x = std::max(max_x, p.x);
			max_y = std::max(max_y, p.y);
		};
#ifdef FOOM_WITH_LASZIP
		for (auto const& path : paths)
		{
			if (!readFile(path, true, extend, nullptr, &result.error))
				return result;
		}
#endif
		if (count == 0)
		{
			result.error = tr("The files hold no points.");
			return result;
		}
		bounds = QRectF(min_x, min_y, max_x - min_x, max_y - min_y);
	}
	auto const left = floorTo(bounds.left(), cell);
	auto const bottom = floorTo(bounds.top(), cell);
	auto const width = std::max(1, int(std::lround((ceilTo(bounds.right(), cell) - left) / cell)));
	auto const height = std::max(1, int(std::lround((ceilTo(bounds.bottom(), cell) - bottom) / cell)));
	auto const top = bottom + height * cell;
	auto const cells = qint64(width) * qint64(height);
	if (cells > max_cells)
	{
		result.error = tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(width).arg(height);
		return result;
	}
	
	// The points: sums relative to the first height keep the precision of float.
	auto const n = std::size_t(cells);
	auto const nan = std::numeric_limits<float>::quiet_NaN();
	std::vector<float> dtm_sum, dsm_sum;
	std::vector<quint32> dtm_count, dsm_count;
	if (params.dtm)
	{
		dtm_sum.assign(n, 0.0f);
		dtm_count.assign(n, 0);
	}
	if (params.dsm)
	{
		dsm_sum.assign(n, 0.0f);
		dsm_count.assign(n, 0);
	}
	auto reference = std::numeric_limits<double>::quiet_NaN();
	QString crs_spec;
	auto const right = left + width * cell;
	auto const add = [&](const CloudPoint& p) {
		if (p.x < left || p.x > right || p.y < bottom || p.y > top)
			return;
		auto const col = std::min(width - 1, int((p.x - left) / cell));
		// Cells include their south and west edges.
		auto const row = height - 1 - std::min(height - 1, int((p.y - bottom) / cell));
		auto const k = std::size_t(row) * std::size_t(width) + std::size_t(col);
		auto const c = p.classification;
		if (std::isnan(reference))
			reference = p.z;
		if (params.dtm && (c == 2 || c == 9) && p.return_number >= p.number_of_returns)
		{
			dtm_sum[k] += float(p.z - reference);
			++dtm_count[k];
		}
		// The mean.
		if (params.dsm && (c == 2 || c == 3 || c == 4 || c == 5 || c == 6 || c == 9) && p.return_number <= 1)
		{
			dsm_sum[k] += float(p.z - reference);
			++dsm_count[k];
		}
	};
	for (auto const& path : paths)
	{
		QString crs;
		if (!read(path, add, &crs, &result.error))
			return result;
		if (crs_spec.isEmpty())
			crs_spec = crs;
	}
	
	// The grids.
	auto const makeGrid = [&](std::vector<float>& sum, std::vector<quint32>& count) {
		std::vector<float> values(n, nan);
		for (std::size_t k = 0; k < n; ++k)
		{
			if (count[k] > 0)
				values[k] = float(reference + double(sum[k]) / count[k]);
		}
		sum.clear();
		sum.shrink_to_fit();
		count.clear();
		count.shrink_to_fit();
		auto grid = std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell, std::move(values), crs_spec);
		fillVoids(*grid, params.fill_cells);
		return grid;
	};
	if (params.dtm)
		result.dtm = makeGrid(dtm_sum, dtm_count);
	if (params.dsm)
		result.dsm = makeGrid(dsm_sum, dsm_count);
	return result;
}


// static
RasterProduct PointCloud::vegetationBaseMap(const QStringList& paths, const ElevationModel& dtm, const VegetationParams& params)
{
	// the design notes, "Vegetation height and vegetation base map".
	RasterProduct product;
	if (!supported())
	{
		product.error = tr("This build of FOOM does not read point clouds.");
		return product;
	}
	auto const extent = dtm.extent();
	auto const cell = params.cell;
	if (!(cell > 0))
	{
		product.error = tr("Nothing to grid.");
		return product;
	}
	auto const width = std::max(1, int(std::lround(extent.width() / cell)));
	auto const height = std::max(1, int(std::lround(extent.height() / cell)));
	if (qint64(width) * qint64(height) > max_cells)
	{
		product.error = tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(width).arg(height);
		return product;
	}
	auto const left = extent.left();
	auto const top = extent.top() + extent.height();
	
	// Counts per cell.
	auto const n = std::size_t(width) * std::size_t(height);
	std::vector<quint32> returns(n, 0), raised(n, 0), ground(n, 0), vegetation(n, 0), undergrowth(n, 0);
	auto const add = [&](const CloudPoint& p) {
		auto const col = int(std::floor((p.x - left) / cell));
		auto const row = height - 1 - int(std::floor((p.y - (top - height * cell)) / cell));
		if (col < 0 || row < 0 || col >= width || row >= height)
			return;
		auto const k = std::size_t(row) * std::size_t(width) + std::size_t(col);
		++returns[k];
		auto base = dtm.at(QPointF(p.x, p.y));
		if (std::isnan(base))
		{
			int c, r;
			if (!dtm.cellAt(QPointF(p.x, p.y), c, r) || std::isnan(dtm.value(c, r)))
				return;
			base = dtm.value(c, r);
		}
		auto const h = p.z - base;
		if (p.classification == 2 || p.classification == 9)
			++ground[k];
		if (h >= params.open_height)
			++raised[k];
		if (h >= params.vegetation_from && h < params.vegetation_to)
			++vegetation[k];
		if (h >= params.undergrowth_from && h < params.undergrowth_to)
			++undergrowth[k];
	};
	for (auto const& path : paths)
	{
		if (!read(path, add, nullptr, &product.error))
			return product;
	}
	
	// Sums over windows through summed-area tables.
	auto const table = [width, height](const std::vector<quint32>& counts) {
		std::vector<qint64> sums(std::size_t(width + 1) * std::size_t(height + 1), 0);
		for (int row = 0; row < height; ++row)
		{
			qint64 line = 0;
			for (int col = 0; col < width; ++col)
			{
				line += counts[std::size_t(row) * std::size_t(width) + std::size_t(col)];
				sums[std::size_t(row + 1) * std::size_t(width + 1) + std::size_t(col + 1)] = sums[std::size_t(row) * std::size_t(width + 1) + std::size_t(col + 1)] + line;
			}
		}
		return sums;
	};
	auto const ground_sums = table(ground);
	auto const vegetation_sums = table(vegetation);
	auto const undergrowth_sums = table(undergrowth);
	auto const window = [width, height](const std::vector<qint64>& sums, int col, int row, int size) {
		auto const half = size / 2;
		auto const c0 = std::max(0, col - half), c1 = std::min(width, col + half + 1);
		auto const r0 = std::max(0, row - half), r1 = std::min(height, row + half + 1);
		auto const at = [&sums, width](int c, int r) { return sums[std::size_t(r) * std::size_t(width + 1) + std::size_t(c)]; };
		return at(c1, r1) - at(c0, r1) - at(c1, r0) + at(c0, r0);
	};
	// The normalised difference of vegetation and ground; returns 2 when undefined.
	auto const ndvd = [](qint64 v, qint64 g) {
		return v + g > 0 ? double(v - g) / double(v + g) : 2.0;
	};
	
	product.image = QImage(width, height, QImage::Format_Indexed8);
	product.image.setColorTable({
	    params.no_data_red ? qRgb(255, 0, 0) : qRgb(255, 255, 255),
	    qRgb(255, 201, 15),
	    qRgb(255, 255, 255),
	    qRgb(204, 255, 179),
	    qRgb(153, 255, 102),
	    qRgb(51, 255, 0),
	    qRgb(128, 0, 255),
	    qRgb(255, 224, 119),
	    qRgb(191, 128, 255),
	});
	auto const& u = params.undergrowth_thresholds;
	auto const& t = params.vegetation_thresholds;
	for (int row = 0; row < height; ++row)
	{
		auto* line = product.image.scanLine(row);
		for (int col = 0; col < width; ++col)
		{
			auto const k = std::size_t(row) * std::size_t(width) + std::size_t(col);
			auto value = Runnable;
			if (returns[k] == 0)
			{
				value = NoData;
			}
			else if (raised[k] == 0)
			{
				value = Open;
			}
			else
			{
				auto const iu = ndvd(window(undergrowth_sums, col, row, params.undergrowth_kernel), window(ground_sums, col, row, params.undergrowth_kernel));
				auto const iv = ndvd(window(vegetation_sums, col, row, params.vegetation_kernel), window(ground_sums, col, row, params.vegetation_kernel));
				auto const has_iu = iu <= 1.0;
				auto const has_iv = iv <= 1.0;
				if (has_iu && iu >= u[0] && (!has_iv || iu >= iv))
					value = iu >= u[1] ? UndergrowthDense : UndergrowthSome;
				else if (has_iv)
					value = iv >= t[2] ? Fight : iv >= t[1] ? Walk : iv >= t[0] ? Slow : Runnable;
			}
			line[col] = quint8(value);
		}
	}
	product.extent = QRectF(left, extent.top(), width * cell, height * cell);
	product.crs_spec = dtm.crsSpec();
	return product;
}

}  // namespace OpenOrienteering
