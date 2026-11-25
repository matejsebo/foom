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
#include <QImage>
#include <QVector>
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

/// The largest grid, in cells (20 by 20 km at 1 m): the grids are held whole,
/// the points are gridded a row of tiles at a time.
constexpr qint64 max_cells = 400000000;

/// The largest grid written to disk (100 by 100 km at 1 m), and the default memory ceiling.
/// See the design notes, "Grids on disk, a tile cache, and a progress bar".
constexpr qint64 max_disk_cells = 10000000000LL;
constexpr qint64 default_memory_limit = qint64(2) << 30;

/// Estimated bytes of a cell of a row of tiles: per model in gridding, in all for the base map.
constexpr qint64 band_bytes_per_cell = 24;
constexpr qint64 base_map_bytes_per_cell = 44;

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
	// foom: a cloud in feet is read as the same projection in metres, its heights in metres
	auto file_crs = crsOf(*header);
	if (file_crs.isEmpty())
		file_crs = ElevationModel::AssumedCrs::forFileWithout();  // foom: in the system of a map which counts in feet
	auto const units = ElevationModel::linearUnits(file_crs);
	auto const convert = !units.isMetric();
	if (crs)
		*crs = units.metric_crs.isEmpty() ? file_crs : units.metric_crs;
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
		if (convert)
		{
			p.x *= units.horizontal;
			p.y *= units.horizontal;
			p.z *= units.vertical;
		}
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

/**
 * Fills void cells from the cells with points.
 *
 * A void cell inside the data is filled when a cell with points lies within
 * reach cells of it in each direction (a square). A void cell is inside
 * the data when cells with points lie on both sides of it, within reach, along
 * its row, its column or one of its diagonals: a hole, not the surroundings.
 * Past the outer edge of the data only edge_reach cells are filled. The value is the inverse
 * distance weighting (power 2) of the cells with points in the square about
 * the cell which reaches a little beyond the nearest of them, so that a small
 * hole is filled from its rim and a wide one costs no more than it must.
 */
void fillVoids(ElevationModel& grid, int reach, int edge_reach)
{
	edge_reach = std::min(edge_reach, reach);
	if (reach <= 0)
		return;
	auto const width = grid.width();
	auto const height = grid.height();
	auto const source = grid.values();
	auto const at = [width](int col, int row) { return std::size_t(row) * std::size_t(width) + std::size_t(col); };
	
	// The number of cells with points above and left of each corner.
	auto const stride = std::size_t(width) + 1;
	std::vector<int> counts(stride * (std::size_t(height) + 1), 0);
	for (int row = 0; row < height; ++row)
	{
		auto in_row = 0;
		for (int col = 0; col < width; ++col)
		{
			in_row += std::isnan(source[at(col, row)]) ? 0 : 1;
			counts[std::size_t(row + 1) * stride + std::size_t(col + 1)] = counts[std::size_t(row) * stride + std::size_t(col + 1)] + in_row;
		}
	}
	auto const within = [&counts, stride, width, height](int col, int row, int s) {
		auto const c0 = std::size_t(std::max(0, col - s)), c1 = std::size_t(std::min(width, col + s + 1));
		auto const r0 = std::size_t(std::max(0, row - s)), r1 = std::size_t(std::min(height, row + s + 1));
		return counts[r1 * stride + c1] - counts[r0 * stride + c1] - counts[r1 * stride + c0] + counts[r0 * stride + c0];
	};
	
	// Which void cells have points on both sides along a line through them.
	std::vector<char> enclosed(source.size(), 0);
	if (edge_reach < reach)
	{
		// Along every line of the four directions: the steps since the last cell with points, forwards and backwards.
		std::vector<int> since;
		auto const scan = [&](int col, int row, int dc, int dr) {
			since.clear();
			auto last = -1;
			for (int c = col, r = row, i = 0; c >= 0 && c < width && r >= 0 && r < height; c += dc, r += dr, ++i)
			{
				if (!std::isnan(source[at(c, r)]))
					last = i;
				since.push_back(last < 0 ? reach + 1 : i - last);
			}
			auto const n = int(since.size());
			auto next = -1;
			for (int i = n - 1; i >= 0; --i)
			{
				auto const c = col + i * dc, r = row + i * dr;
				if (!std::isnan(source[at(c, r)]))
					next = i;
				else if (next >= 0 && next - i <= reach && since[std::size_t(i)] <= reach)
					enclosed[at(c, r)] = 1;
			}
		};
		for (int row = 0; row < height; ++row)
		{
			scan(0, row, 1, 0);       // rows
			scan(0, row, 1, 1);       // diagonals down from the west edge
			scan(0, row, 1, -1);      // diagonals up from the west edge
		}
		for (int col = 0; col < width; ++col)
		{
			scan(col, 0, 0, 1);       // columns
			if (col > 0)
			{
				scan(col, 0, 1, 1);           // diagonals down from the north edge
				scan(col, height - 1, 1, -1); // diagonals up from the south edge
			}
		}
	}
	
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			if (!std::isnan(source[at(col, row)]))
				continue;
			if (within(col, row, enclosed[at(col, row)] || edge_reach >= reach ? reach : edge_reach) == 0)
				continue;
			// The distance to the nearest cell with points, in cells each way.
			auto low = 1, high = reach;
			while (low < high)
			{
				auto const middle = (low + high) / 2;
				if (within(col, row, middle) > 0)
					high = middle;
				else
					low = middle + 1;
			}
			auto const s = std::min(reach, low + 2 + low / 4);
			auto sum = 0.0;
			auto weights = 0.0;
			for (int r = std::max(0, row - s); r <= std::min(height - 1, row + s); ++r)
			{
				for (int c = std::max(0, col - s); c <= std::min(width - 1, col + s); ++c)
				{
					auto const value = source[at(c, r)];
					if (std::isnan(value))
						continue;
					auto const weight = 1.0 / double((c - col) * (c - col) + (r - row) * (r - row));
					sum += weight * double(value);
					weights += weight;
				}
			}
			if (weights > 0)
				grid.setValue(col, row, float(sum / weights));
		}
	}
}

}  // namespace



// ### PointChoice ###

// static
PointChoice PointChoice::terrain()
{
	PointChoice choice;
	choice.classes[2] = true;
	choice.classes[9] = true;
	choice.returns = ReturnChoice::Last;
	return choice;
}

// static
PointChoice PointChoice::surface()
{
	// Every class but the two noise classes.
	PointChoice choice;
	choice.classes.fill(true);
	choice.classes[7] = false;
	choice.classes[18] = false;
	choice.returns = ReturnChoice::First;
	return choice;
}

// static
int PointChoice::groupOf(int classification)
{
	switch (classification)
	{
	case 0: case 1: return 0;
	case 2: return 1;
	case 3: return 2;
	case 4: return 3;
	case 5: return 4;
	case 6: return 5;
	case 7: return 6;
	case 9: return 7;
	case 12: return 8;
	default: return 9;
	}
}

// static
const char* PointChoice::groupId(int group)
{
	static const char* const ids[group_count] = { "unclassified", "ground", "low-vegetation", "medium-vegetation", "high-vegetation",
	                                              "building", "low-point", "water", "overlap", "other" };
	return group >= 0 && group < group_count ? ids[group] : "";
}

// static
QString PointChoice::groupName(int group)
{
	switch (group)
	{
	case 0: return PointCloud::tr("Unclassified");
	case 1: return PointCloud::tr("Ground");
	case 2: return PointCloud::tr("Low vegetation");
	case 3: return PointCloud::tr("Medium vegetation");
	case 4: return PointCloud::tr("High vegetation");
	case 5: return PointCloud::tr("Building");
	case 6: return PointCloud::tr("Low point (noise)");
	case 7: return PointCloud::tr("Water");
	case 8: return PointCloud::tr("Overlap points");
	default: return PointCloud::tr("Other");
	}
}

bool PointChoice::group(int group) const
{
	for (int c = 0; c < 256; ++c)
	{
		if (classes[std::size_t(c)] && groupOf(c) == group)
			return true;
	}
	return false;
}

void PointChoice::setGroup(int group, bool chosen)
{
	for (int c = 0; c < 256; ++c)
	{
		if (groupOf(c) == group)
			classes[std::size_t(c)] = chosen;
	}
}

bool operator==(const PointChoice& lhs, const PointChoice& rhs)
{
	return lhs.classes == rhs.classes && lhs.returns == rhs.returns;
}



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
		{
			info.crs_spec = crs;
			if (!crs.isEmpty())
			{
				auto named = fileCrs(path);
				if (named.isEmpty())
					named = ElevationModel::AssumedCrs::forFileWithout();
				auto const units = ElevationModel::linearUnits(named);
				info.file_horizontal_unit = units.horizontal;
				info.file_vertical_unit = units.vertical;
			}
		}
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
QString PointCloud::fileCrs(const QString& path)
{
#ifdef FOOM_WITH_LASZIP
	laszip_POINTER reader = nullptr;
	if (laszip_create(&reader) != 0)
		return {};
	QString crs;
	laszip_BOOL compressed = 0;
	auto const name = QDir::toNativeSeparators(path).toLocal8Bit();
	if (laszip_open_reader(reader, name.constData(), &compressed) == 0)
	{
		laszip_header_struct* header = nullptr;
		if (laszip_get_header_pointer(reader, &header) == 0 && header)
			crs = crsOf(*header);
		laszip_close_reader(reader);
	}
	laszip_destroy(reader);
	return crs;
#else
	Q_UNUSED(path)
	return {};
#endif
}

// static
qint64 PointCloud::maxCells()
{
	return max_cells;
}


// static
qint64 PointCloud::maxDiskCells()
{
	return max_disk_cells;
}


// static
qint64 PointCloud::defaultMemoryLimit()
{
	return default_memory_limit;
}


namespace {

/// The extent of the points of one file, read rather than taken from the header.
struct FileBounds
{
	double min_x = std::numeric_limits<double>::max();
	double min_y = std::numeric_limits<double>::max();
	double max_x = std::numeric_limits<double>::lowest();
	double max_y = std::numeric_limits<double>::lowest();
	qint64 count = 0;

	bool reaches(double left, double bottom, double right, double top) const
	{
		return count > 0 && max_y >= bottom && min_y <= top && max_x >= left && min_x <= right;
	}
};

bool readFileBounds(const QStringList& paths, std::vector<FileBounds>& bounds, std::vector<QString>* crs_of_file, QString* error)
{
	bounds.assign(std::size_t(paths.size()), FileBounds());
	for (std::size_t i = 0; i < bounds.size(); ++i)
	{
		auto& b = bounds[i];
		auto const extend = [&b](const CloudPoint& p) {
			++b.count;
			b.min_x = std::min(b.min_x, p.x);
			b.min_y = std::min(b.min_y, p.y);
			b.max_x = std::max(b.max_x, p.x);
			b.max_y = std::max(b.max_y, p.y);
		};
#ifdef FOOM_WITH_LASZIP
		QString crs;
		if (!readFile(paths[int(i)], true, extend, &crs, error))
			return false;
		if (crs_of_file)
			(*crs_of_file)[i] = crs;
#else
		Q_UNUSED(extend)
		Q_UNUSED(crs_of_file)
		Q_UNUSED(error)
#endif
	}
	return true;
}

QString cancelledMessage()
{
	return PointCloud::tr("Cancelled.");
}

}  // namespace


// static
GriddingResult PointCloud::grid(const QStringList& paths, const GriddingParams& params)
{
	// the design notes, "Reading point clouds and gridding them", "Tiles of 1 km"
	// and "Grids on disk, a tile cache, and a progress bar".
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
	auto const num_files = std::size_t(paths.size());
	std::vector<QString> crs_of_file(num_files);
	std::vector<FileBounds> file_bounds;

	// The edges.
	QRectF bounds = params.box;
	if (bounds.isEmpty())
	{
		if (!readFileBounds(paths, file_bounds, &crs_of_file, &result.error))
			return result;
		FileBounds all;
		for (auto const& b : file_bounds)
		{
			if (b.count == 0)
				continue;
			all.count += b.count;
			all.min_x = std::min(all.min_x, b.min_x);
			all.min_y = std::min(all.min_y, b.min_y);
			all.max_x = std::max(all.max_x, b.max_x);
			all.max_y = std::max(all.max_y, b.max_y);
		}
		if (all.count == 0)
		{
			result.error = tr("The files hold no points.");
			return result;
		}
		bounds = QRectF(all.min_x, all.min_y, all.max_x - all.min_x, all.max_y - all.min_y);
	}
	auto const left = floorTo(bounds.left(), cell);
	auto const bottom = floorTo(bounds.top(), cell);
	auto const width = std::max(1, int(std::lround((ceilTo(bounds.right(), cell) - left) / cell)));
	auto const height = std::max(1, int(std::lround((ceilTo(bounds.bottom(), cell) - bottom) / cell)));
	auto const top = bottom + height * cell;
	auto const right = left + width * cell;
	auto const cells = qint64(width) * qint64(height);

	// In memory or on disk.
	auto const limit = params.memory_limit > 0 ? params.memory_limit : default_memory_limit;
	auto const models = qint64(params.dtm) + qint64(params.dsm);
	auto const on_disk = params.to_disk || cells * 4 * models > limit || cells > max_cells;
	if (on_disk)
	{
		if ((params.dtm && params.dtm_path.isEmpty()) || (params.dsm && params.dsm_path.isEmpty()))
		{
			result.error = tr("The grid of %1 by %2 cells does not fit in memory; choose a larger cell or a box.").arg(width).arg(height);
			return result;
		}
		if (cells > max_disk_cells)
		{
			result.error = tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(width).arg(height);
			return result;
		}
	}

	// Rows of tiles, from the north. The buffer covers the reach of the void
	// filling, so that every cell is filled from the same cells as in one piece.
	auto tile_rows = std::max(1, int(std::lround(params.tile_size / cell)));
	auto const buffer_rows = std::max(std::max(0, params.fill_cells), int(std::ceil(params.tile_buffer / cell - 1e-9)));
	auto const rows_in_limit = limit / (band_bytes_per_cell * qint64(width) * models);
	if (rows_in_limit < 1 + 2 * qint64(buffer_rows))
	{
		result.error = tr("The memory ceiling is too low for a row of tiles %1 cells wide.").arg(width);
		return result;
	}
	tile_rows = int(std::min(qint64(tile_rows), rows_in_limit - 2 * qint64(buffer_rows)));
	auto const num_bands = (height + tile_rows - 1) / tile_rows;
	if (num_bands > 1 && file_bounds.empty() && !readFileBounds(paths, file_bounds, &crs_of_file, &result.error))
		return result;
	result.tile_rows = num_bands;

	auto const nan = std::numeric_limits<float>::quiet_NaN();
	std::vector<float> dtm_values, dsm_values;
	GridFileWriter dtm_writer, dsm_writer;
	auto const discard = [&]() {
		dtm_writer.discard();
		dsm_writer.discard();
	};
	if (on_disk)
	{
		if ((params.dtm && !dtm_writer.open(params.dtm_path, width, height, QPointF(left, top), cell, {}, &result.error))
		    || (params.dsm && !dsm_writer.open(params.dsm_path, width, height, QPointF(left, top), cell, {}, &result.error)))
		{
			discard();
			return result;
		}
	}
	else
	{
		auto const n = std::size_t(cells);
		if (params.dtm)
			dtm_values.assign(n, nan);
		if (params.dsm)
			dsm_values.assign(n, nan);
	}
	auto const step = tr("Gridding");
	auto const cancel = [&]() {
		discard();
		result.cancelled = true;
		result.error = cancelledMessage();
		return std::move(result);
	};
	if (params.progress && !params.progress(step, 0, num_bands))
		return cancel();

	// Sums relative to the first height keep the precision of float.
	auto reference = std::numeric_limits<double>::quiet_NaN();
	auto points_seen = qint64(0);
	auto stopped = false;
	auto const beat = std::max<qint64>(1, params.progress_points);
	for (int band = 0; band < num_bands; ++band)
	{
		auto const r0 = band * tile_rows;
		auto const r1 = std::min(height, r0 + tile_rows);
		auto const b0 = std::max(0, r0 - buffer_rows);
		auto const b1 = std::min(height, r1 + buffer_rows);
		auto const band_height = b1 - b0;
		auto const band_top = top - b0 * cell;
		auto const band_bottom = top - b1 * cell;
		auto const m = std::size_t(width) * std::size_t(band_height);
		std::vector<float> dtm_sum, dsm_sum;
		std::vector<quint32> dtm_count, dsm_count;
		if (params.dtm)
		{
			dtm_sum.assign(m, 0.0f);
			dtm_count.assign(m, 0);
		}
		if (params.dsm)
		{
			dsm_sum.assign(m, 0.0f);
			dsm_count.assign(m, 0);
		}
		auto const add = [&](const CloudPoint& p) {
			// A sign of life between the rows of tiles, and an ear for Cancel.
			if (stopped || (params.progress && ++points_seen % beat == 0 && !params.progress(step, band, num_bands)))
			{
				stopped = true;
				return;
			}
			if (p.x < left || p.x > right || p.y < bottom || p.y > top)
				return;
			// Cells include their south and west edges.
			auto const row = height - 1 - std::min(height - 1, int((p.y - bottom) / cell));
			if (row < b0 || row >= b1)
				return;
			auto const col = std::min(width - 1, int((p.x - left) / cell));
			auto const k = std::size_t(row - b0) * std::size_t(width) + std::size_t(col);
			if (std::isnan(reference))
				reference = p.z;
			if (params.dtm && params.dtm_points.accepts(p))
			{
				dtm_sum[k] += float(p.z - reference);
				++dtm_count[k];
			}
			// The mean.
			if (params.dsm && params.dsm_points.accepts(p))
			{
				dsm_sum[k] += float(p.z - reference);
				++dsm_count[k];
			}
		};
		for (std::size_t i = 0; i < num_files; ++i)
		{
			if (!file_bounds.empty() && !file_bounds[i].reaches(left, band_bottom, right, band_top))
				continue;
			++result.file_reads;
			if (!read(paths[int(i)], add, &crs_of_file[i], &result.error))
			{
				discard();
				return result;
			}
			if (stopped)
				return cancel();
		}

		// The band's grids, filled, and its own rows copied out or written.
		auto const finish = [&](std::vector<float>& sum, std::vector<quint32>& count, std::vector<float>& out, GridFileWriter& writer) {
			std::vector<float> values(m, nan);
			for (std::size_t k = 0; k < m; ++k)
			{
				if (count[k] > 0)
					values[k] = float(reference + double(sum[k]) / count[k]);
			}
			sum = {};
			count = {};
			ElevationModel piece(width, band_height, QPointF(left, band_top), cell, std::move(values));
			fillVoids(piece, params.fill_cells, params.edge_cells);
			auto const& filled = piece.values();
			auto const from = filled.begin() + std::ptrdiff_t(std::size_t(r0 - b0) * std::size_t(width));
			if (on_disk)
				return writer.writeRows(r0, r1 - r0, &*from, &result.error);
			std::copy(from, filled.begin() + std::ptrdiff_t(std::size_t(r1 - b0) * std::size_t(width)),
			          out.begin() + std::ptrdiff_t(std::size_t(r0) * std::size_t(width)));
			return true;
		};
		if ((params.dtm && !finish(dtm_sum, dtm_count, dtm_values, dtm_writer))
		    || (params.dsm && !finish(dsm_sum, dsm_count, dsm_values, dsm_writer)))
		{
			discard();
			return result;
		}
		if (params.progress && !params.progress(step, band + 1, num_bands))
			return cancel();
	}

	QString crs_spec;
	for (auto const& crs : crs_of_file)
	{
		if (!crs.isEmpty())
		{
			crs_spec = crs;
			break;
		}
	}
	if (on_disk)
	{
		auto const finish = [&](GridFileWriter& writer, const QString& path, QString& out) {
			if (!writer.setCrs(crs_spec, &result.error) || !writer.close(&result.error))
				return false;
			out = path;
			return true;
		};
		if ((params.dtm && !finish(dtm_writer, params.dtm_path, result.dtm_path))
		    || (params.dsm && !finish(dsm_writer, params.dsm_path, result.dsm_path)))
		{
			discard();
			result.dtm_path.clear();
			result.dsm_path.clear();
		}
		return result;
	}
	if (params.dtm)
		result.dtm = std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell, std::move(dtm_values), crs_spec);
	if (params.dsm)
		result.dsm = std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell, std::move(dsm_values), crs_spec);
	return result;
}



namespace {

/// The terrain model's height under a point, falling back to the cell's value at the grid's edge.
template <class Grid>
double baseHeight(const Grid& dtm, const CloudPoint& p)
{
	auto base = dtm.at(QPointF(p.x, p.y));
	if (std::isnan(base))
	{
		int c, r;
		if (!dtm.cellAt(QPointF(p.x, p.y), c, r) || std::isnan(dtm.value(c, r)))
			return std::numeric_limits<double>::quiet_NaN();
		base = dtm.value(c, r);
	}
	return base;
}

QVector<QRgb> baseMapPalette(const VegetationParams& params)
{
	return {
	    params.no_data_red ? qRgb(255, 0, 0) : qRgb(255, 255, 255),
	    qRgb(255, 201, 15),
	    qRgb(255, 255, 255),
	    qRgb(204, 255, 179),
	    qRgb(153, 255, 102),
	    qRgb(51, 255, 0),
	    qRgb(128, 0, 255),
	    qRgb(255, 224, 119),
	    qRgb(191, 128, 255),
	};
}

/// The layout of the base map over the terrain model.
struct BaseMapLayout
{
	int width = 0;
	int height = 0;
	double left = 0.0;
	double top = 0.0;
	double cell = 1.0;
	QRectF extent;

	BaseMapLayout(const QRectF& dtm_extent, double cell_size)
	: cell(cell_size)
	{
		width = std::max(1, int(std::lround(dtm_extent.width() / cell)));
		height = std::max(1, int(std::lround(dtm_extent.height() / cell)));
		left = dtm_extent.left();
		top = dtm_extent.top() + dtm_extent.height();
		extent = QRectF(left, dtm_extent.top(), width * cell, height * cell);
	}
};

/**
 * The base map, row of tiles by row of tiles: the rows of each go to the sink.
 * See the design notes, "Vegetation height and vegetation base map"
 * and "Grids on disk, a tile cache, and a progress bar".
 */
template <class Grid>
bool baseMapRows(const QStringList& paths, const Grid& dtm, const VegetationParams& params, const BaseMapLayout& layout,
                 const TileProgress& progress, const std::function<bool (int, const QImage&)>& sink, QString* error, bool* cancelled)
{
	auto const width = layout.width;
	auto const height = layout.height;
	auto const cell = layout.cell;
	auto const left = layout.left;
	auto const top = layout.top;
	auto const palette = baseMapPalette(params);

	// Rows of tiles with a buffer of half the larger kernel: every window sees the same cells as in one piece.
	auto const half = std::max(std::max(params.undergrowth_kernel, params.vegetation_kernel), 1) / 2;
	auto rows_per_band = std::max(1, int(std::lround(params.tile_size / cell)));
	auto const limit = params.memory_limit > 0 ? params.memory_limit : default_memory_limit;
	auto const rows_in_limit = limit / (base_map_bytes_per_cell * qint64(width));
	if (rows_in_limit < 1 + 2 * qint64(half))
	{
		*error = PointCloud::tr("The memory ceiling is too low for a row of tiles %1 cells wide.").arg(width);
		return false;
	}
	rows_per_band = int(std::min(qint64(rows_per_band), rows_in_limit - 2 * qint64(half)));
	auto const bands = (height + rows_per_band - 1) / rows_per_band;
	std::vector<FileBounds> file_bounds;
	if (bands > 1 && !readFileBounds(paths, file_bounds, nullptr, error))
		return false;
	auto const step = PointCloud::tr("Vegetation base map");
	if (progress && !progress(step, 0, bands))
	{
		*cancelled = true;
		return false;
	}

	// The normalised difference of vegetation and ground; returns 2 when undefined.
	auto const ndvd = [](qint64 v, qint64 g) {
		return v + g > 0 ? double(v - g) / double(v + g) : 2.0;
	};
	auto const& u = params.undergrowth_thresholds;
	auto const& t = params.vegetation_thresholds;
	auto points_seen = qint64(0);
	auto stopped = false;
	for (int band = 0; band < bands; ++band)
	{
		auto const r0 = band * rows_per_band;
		auto const r1 = std::min(height, r0 + rows_per_band);
		auto const b0 = std::max(0, r0 - half);
		auto const b1 = std::min(height, r1 + half);
		auto const band_height = b1 - b0;
		auto const band_top = top - b0 * cell;
		auto const band_bottom = top - b1 * cell;

		// Counts per cell of the band and its buffers.
		auto const n = std::size_t(width) * std::size_t(band_height);
		std::vector<quint32> returns(n, 0), raised(n, 0), ground(n, 0), vegetation(n, 0), undergrowth(n, 0);
		auto const add = [&](const CloudPoint& p) {
			// A sign of life between the rows of tiles, as in gridding.
			if (stopped || (progress && ++points_seen % 262144 == 0 && !progress(step, band, bands)))
			{
				stopped = true;
				return;
			}
			auto const col = int(std::floor((p.x - left) / cell));
			auto const row = height - 1 - int(std::floor((p.y - (top - height * cell)) / cell));
			if (col < 0 || row < b0 || col >= width || row >= b1)
				return;
			auto const k = std::size_t(row - b0) * std::size_t(width) + std::size_t(col);
			++returns[k];
			auto const base = baseHeight(dtm, p);
			if (std::isnan(base))
				return;
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
		for (int i = 0; i < paths.size(); ++i)
		{
			if (!file_bounds.empty() && !file_bounds[std::size_t(i)].reaches(left, band_bottom, left + width * cell, band_top))
				continue;
			if (!PointCloud::read(paths[i], add, nullptr, error))
				return false;
			if (stopped)
			{
				*cancelled = true;
				return false;
			}
		}

		// Sums over windows through summed-area tables.
		auto const table = [width, band_height](const std::vector<quint32>& counts) {
			std::vector<qint64> sums(std::size_t(width + 1) * std::size_t(band_height + 1), 0);
			for (int row = 0; row < band_height; ++row)
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
		ground = {};
		vegetation = {};
		undergrowth = {};
		// The window in the band's rows; the band holds every row of the grid a window of its own rows reaches.
		auto const window = [width, band_height](const std::vector<qint64>& sums, int col, int row, int size) {
			auto const h = size / 2;
			auto const c0 = std::max(0, col - h), c1 = std::min(width, col + h + 1);
			auto const w0 = std::max(0, row - h), w1 = std::min(band_height, row + h + 1);
			auto const at = [&sums, width](int c, int r) { return sums[std::size_t(r) * std::size_t(width + 1) + std::size_t(c)]; };
			return at(c1, w1) - at(c0, w1) - at(c1, w0) + at(c0, w0);
		};

		QImage rows(width, r1 - r0, QImage::Format_Indexed8);
		if (rows.isNull())
		{
			*error = ElevationModel::tr("The raster is too large");
			return false;
		}
		rows.setColorTable(palette);
		for (int row = r0; row < r1; ++row)
		{
			auto* line = rows.scanLine(row - r0);
			auto const local = row - b0;
			for (int col = 0; col < width; ++col)
			{
				auto const k = std::size_t(local) * std::size_t(width) + std::size_t(col);
				auto value = PointCloud::Runnable;
				if (returns[k] == 0)
				{
					value = PointCloud::NoData;
				}
				else if (raised[k] == 0)
				{
					value = PointCloud::Open;
				}
				else
				{
					auto const iu = ndvd(window(undergrowth_sums, col, local, params.undergrowth_kernel), window(ground_sums, col, local, params.undergrowth_kernel));
					auto const iv = ndvd(window(vegetation_sums, col, local, params.vegetation_kernel), window(ground_sums, col, local, params.vegetation_kernel));
					auto const has_iu = iu <= 1.0;
					auto const has_iv = iv <= 1.0;
					if (has_iu && iu >= u[0] && (!has_iv || iu >= iv))
						value = iu >= u[1] ? PointCloud::UndergrowthDense : PointCloud::UndergrowthSome;
					else if (has_iv)
						value = iv >= t[2] ? PointCloud::Fight : iv >= t[1] ? PointCloud::Walk : iv >= t[0] ? PointCloud::Slow : PointCloud::Runnable;
				}
				line[col] = quint8(value);
			}
		}
		if (!sink(r0, rows))
			return false;
		if (progress && !progress(step, band + 1, bands))
		{
			*cancelled = true;
			return false;
		}
	}
	return true;
}

template <class Grid>
RasterProduct baseMapImage(const QStringList& paths, const Grid& dtm, const VegetationParams& params, const TileProgress& progress)
{
	RasterProduct product;
	if (!PointCloud::supported())
	{
		product.error = PointCloud::tr("This build of FOOM does not read point clouds.");
		return product;
	}
	if (!(params.cell > 0))
	{
		product.error = PointCloud::tr("Nothing to grid.");
		return product;
	}
	BaseMapLayout const layout(dtm.extent(), params.cell);
	if (qint64(layout.width) * qint64(layout.height) > max_cells)
	{
		product.error = PointCloud::tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(layout.width).arg(layout.height);
		return product;
	}
	QImage image(layout.width, layout.height, QImage::Format_Indexed8);
	if (image.isNull())
	{
		product.error = ElevationModel::tr("The raster is too large");
		return product;
	}
	image.setColorTable(baseMapPalette(params));
	auto const sink = [&image](int first_row, const QImage& rows) {
		for (int r = 0; r < rows.height(); ++r)
			std::copy(rows.constScanLine(r), rows.constScanLine(r) + rows.width(), image.scanLine(first_row + r));
		return true;
	};
	bool cancelled = false;
	if (!baseMapRows(paths, dtm, params, layout, progress, sink, &product.error, &cancelled))
	{
		if (cancelled)
			product.error = cancelledMessage();
		return product;
	}
	product.image = std::move(image);
	product.extent = layout.extent;
	product.crs_spec = dtm.crsSpec();
	return product;
}

}  // namespace


// static
RasterProduct PointCloud::vegetationBaseMap(const QStringList& paths, const ElevationModel& dtm, const VegetationParams& params, const TileProgress& progress)
{
	return baseMapImage(paths, dtm, params, progress);
}


// static
RasterProduct PointCloud::vegetationBaseMap(const QStringList& paths, const GridTileCache& dtm, const VegetationParams& params, const TileProgress& progress)
{
	return baseMapImage(paths, dtm, params, progress);
}


// static
bool PointCloud::vegetationBaseMapToFile(const QStringList& paths, const GridTileCache& dtm, const VegetationParams& params,
                                         const QString& path, const TileProgress& progress, QString* error)
{
	QString message;
	auto const fail = [&](const QString& text) {
		if (error)
			*error = text;
		return false;
	};
	if (!supported())
		return fail(tr("This build of FOOM does not read point clouds."));
	if (!(params.cell > 0))
		return fail(tr("Nothing to grid."));
	BaseMapLayout const layout(dtm.extent(), params.cell);
	if (qint64(layout.width) * qint64(layout.height) > max_disk_cells)
		return fail(tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(layout.width).arg(layout.height));
	RasterFileWriter writer;
	if (!writer.open(path, layout.width, layout.height, layout.extent, dtm.crsSpec(), QImage::Format_Indexed8, baseMapPalette(params), &message))
		return fail(message);
	auto const sink = [&writer, &message](int first_row, const QImage& rows) {
		return writer.writeRows(first_row, rows, &message);
	};
	bool cancelled = false;
	if (!baseMapRows(paths, dtm, params, layout, progress, sink, &message, &cancelled))
	{
		writer.discard();
		return fail(cancelled ? cancelledMessage() : message);
	}
	if (!writer.close(&message))
	{
		writer.discard();
		return fail(message);
	}
	return true;
}

}  // namespace OpenOrienteering
