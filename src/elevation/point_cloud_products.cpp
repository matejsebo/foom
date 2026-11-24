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

#include "point_cloud_products.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QTextStream>

#include "elevation/elevation_model.h"
#include "elevation/grid_tiles.h"

namespace OpenOrienteering {

namespace {

// The limit of point_cloud.cpp: 400 MB of float.
constexpr qint64 max_cells = 100000000;

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::PointCloud", text);
}


/**
 * Cells of a size over the terrain model's extent, rows from the north.
 */
struct CellLayout
{
	double left = 0.0;
	double bottom = 0.0;
	double cell = 1.0;
	int width = 0;
	int height = 0;
	QString crs_spec;
	QString error;

	template <class Grid>
	CellLayout(const Grid& dtm, double cell_size)
	: cell(cell_size)
	, crs_spec(dtm.crsSpec())
	{
		if (!PointCloud::supported())
		{
			error = tr("This build of FOOM does not read point clouds.");
			return;
		}
		if (!(cell > 0))
		{
			error = tr("Nothing to grid.");
			return;
		}
		auto const extent = dtm.extent();
		width = std::max(1, int(std::lround(extent.width() / cell)));
		height = std::max(1, int(std::lround(extent.height() / cell)));
		if (qint64(width) * qint64(height) > max_cells)
		{
			error = tr("The grid of %1 by %2 cells is too large; choose a larger cell or a box.").arg(width).arg(height);
			return;
		}
		left = extent.left();
		bottom = extent.top() + extent.height() - height * cell;
	}

	std::size_t size() const { return std::size_t(width) * std::size_t(height); }

	/// The cell of a point, as the vegetation base map's; -1 outside.
	qint64 index(double x, double y) const
	{
		auto const col = int(std::floor((x - left) / cell));
		auto const row = height - 1 - int(std::floor((y - bottom) / cell));
		if (col < 0 || row < 0 || col >= width || row >= height)
			return -1;
		return qint64(row) * width + col;
	}

	QPointF center(int col, int row) const
	{
		return { left + (col + 0.5) * cell, bottom + (height - row - 0.5) * cell };
	}

	QRectF extent() const
	{
		return { left, bottom, width * cell, height * cell };
	}
};


/// The terrain model's height under a point, falling back to the cell's value at the grid's edge.
template <class Grid>
double groundAt(const Grid& dtm, double x, double y)
{
	auto base = dtm.at(QPointF(x, y));
	if (std::isnan(base))
	{
		int c, r;
		if (dtm.cellAt(QPointF(x, y), c, r))
			base = dtm.value(c, r);
	}
	return base;
}


quint8 toGrey(double value)
{
	return quint8(qBound(0, int(std::lround(255.0 * value)), 255));
}


/**
 * The counts of the vegetation base map and its indices over the kernels.
 */
struct VegetationIndices
{
	std::vector<double> vegetation;    ///< > 1 where undefined
	std::vector<double> undergrowth;
	QString error;
};

VegetationIndices vegetationIndices(const QStringList& paths, const ElevationModel& dtm, const CellLayout& layout, const VegetationParams& params)
{
	// The counts and the windows of PointCloud::vegetationBaseMap().
	VegetationIndices result;
	auto const n = layout.size();
	std::vector<quint32> ground(n, 0), vegetation(n, 0), undergrowth(n, 0);
	auto const add = [&](const CloudPoint& p) {
		auto const k = layout.index(p.x, p.y);
		if (k < 0)
			return;
		auto const base = groundAt(dtm, p.x, p.y);
		if (std::isnan(base))
			return;
		auto const h = p.z - base;
		if (p.classification == 2 || p.classification == 9)
			++ground[std::size_t(k)];
		if (h >= params.vegetation_from && h < params.vegetation_to)
			++vegetation[std::size_t(k)];
		if (h >= params.undergrowth_from && h < params.undergrowth_to)
			++undergrowth[std::size_t(k)];
	};
	for (auto const& path : paths)
	{
		if (!PointCloud::read(path, add, nullptr, &result.error))
			return result;
	}
	auto const width = layout.width, height = layout.height;
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
	auto const window = [width, height](const std::vector<qint64>& sums, int col, int row, int size) {
		auto const half = size / 2;
		auto const c0 = std::max(0, col - half), c1 = std::min(width, col + half + 1);
		auto const r0 = std::max(0, row - half), r1 = std::min(height, row + half + 1);
		auto const at = [&sums, width](int c, int r) { return sums[std::size_t(r) * std::size_t(width + 1) + std::size_t(c)]; };
		return at(c1, r1) - at(c0, r1) - at(c1, r0) + at(c0, r0);
	};
	auto const ndvd = [](qint64 v, qint64 g) {
		return v + g > 0 ? double(v - g) / double(v + g) : 2.0;
	};
	auto const ground_sums = table(ground);
	auto const vegetation_sums = table(vegetation);
	auto const undergrowth_sums = table(undergrowth);
	result.vegetation.resize(n);
	result.undergrowth.resize(n);
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			auto const k = std::size_t(row) * std::size_t(width) + std::size_t(col);
			result.vegetation[k] = ndvd(window(vegetation_sums, col, row, params.vegetation_kernel), window(ground_sums, col, row, params.vegetation_kernel));
			result.undergrowth[k] = ndvd(window(undergrowth_sums, col, row, params.undergrowth_kernel), window(ground_sums, col, row, params.undergrowth_kernel));
		}
	}
	return result;
}


}  // namespace



namespace {

template <class Grid>
RasterProduct featureMapOf(const QStringList& paths, const Grid& dtm, const FeatureMapParams& params)
{
	// the design notes, "Feature map, intensity and classification maps".
	RasterProduct product;
	CellLayout const layout(dtm, params.cell);
	if (!layout.error.isEmpty())
	{
		product.error = layout.error;
		return product;
	}
	auto const n = layout.size();
	std::vector<float> heights(n, 0.0f);
	std::vector<bool> has_return(n, false);
	auto const add = [&](const CloudPoint& p) {
		auto const k = layout.index(p.x, p.y);
		if (k < 0)
			return;
		has_return[std::size_t(k)] = true;
		auto const base = groundAt(dtm, p.x, p.y);
		if (std::isnan(base))
			return;
		auto const h = p.z - base;
		if (h >= params.band_from && h <= params.band_to)
			heights[std::size_t(k)] = std::max(heights[std::size_t(k)], float(h));
	};
	for (auto const& path : paths)
	{
		if (!PointCloud::read(path, add, nullptr, &product.error))
			return product;
	}
	// Cells without returns are shaded as height 0 so that their neighbours keep their shade.
	auto const extent = layout.extent();
	if (params.legacy)
	{
		// Legacy mode: what stands in the band is put on the terrain,
		// and the terrain is shown wherever it is known, with returns or without.
		for (int row = 0; row < layout.height; ++row)
		{
			for (int col = 0; col < layout.width; ++col)
			{
				auto const k = std::size_t(row) * std::size_t(layout.width) + std::size_t(col);
				auto const base = groundAt(dtm, layout.left + (col + 0.5) * layout.cell, layout.bottom + (layout.height - row - 0.5) * layout.cell);
				has_return[k] = !std::isnan(base);
				heights[k] = has_return[k] ? heights[k] + float(base) : std::numeric_limits<float>::quiet_NaN();
			}
		}
	}
	ElevationModel grid(layout.width, layout.height, QPointF(extent.left(), extent.top() + extent.height()), layout.cell, std::move(heights), layout.crs_spec);
	// The slope shading of hillShade() with exaggeration 1, inline: hillShade() would pull
	// the template code of raster_products.cpp into every user of this file.
	product.image = QImage(layout.width, layout.height, QImage::Format_Grayscale8);
	if (product.image.isNull())
	{
		product.error = ElevationModel::tr("The raster is too large");
		return product;
	}
	auto const half_pi = std::atan(1.0) * 2.0;
	for (int row = 0; row < layout.height; ++row)
	{
		auto* line = product.image.scanLine(row);
		for (int col = 0; col < layout.width; ++col)
		{
			if (!has_return[std::size_t(row) * std::size_t(layout.width) + std::size_t(col)])
			{
				line[col] = 0;
				continue;
			}
			auto const g = grid.gradientAt(col, row);
			line[col] = std::isnan(g.x()) || std::isnan(g.y()) ? 0 : toGrey(1.0 - std::atan(std::hypot(g.x(), g.y())) / half_pi);
		}
	}
	product.extent = extent;
	product.crs_spec = layout.crs_spec;
	return product;
}


template <class Grid>
RasterProduct intensityMapOf(const QStringList& paths, const Grid& dtm, double cell)
{
	RasterProduct product;
	CellLayout const layout(dtm, cell);
	if (!layout.error.isEmpty())
	{
		product.error = layout.error;
		return product;
	}
	auto const n = layout.size();
	std::vector<double> sums(n, 0.0);
	std::vector<quint32> counts(n, 0);
	auto const add = [&](const CloudPoint& p) {
		if (p.return_number > 1)
			return;
		auto const k = layout.index(p.x, p.y);
		if (k < 0)
			return;
		sums[std::size_t(k)] += p.intensity;
		++counts[std::size_t(k)];
	};
	for (auto const& path : paths)
	{
		if (!PointCloud::read(path, add, nullptr, &product.error))
			return product;
	}
	std::vector<double> means;
	means.reserve(n);
	for (std::size_t k = 0; k < n; ++k)
	{
		if (counts[k] > 0)
		{
			sums[k] /= counts[k];
			means.push_back(sums[k]);
		}
	}
	// Percentiles by rank.
	double low = 0.0, high = 0.0;
	if (!means.empty())
	{
		auto const rank = [&means](double p) {
			auto const i = std::size_t(std::floor(p * double(means.size() - 1)));
			std::nth_element(means.begin(), means.begin() + std::ptrdiff_t(i), means.end());
			return means[i];
		};
		low = rank(0.02);
		high = rank(0.98);
	}
	product.image = QImage(layout.width, layout.height, QImage::Format_Grayscale8);
	if (product.image.isNull())
	{
		product.error = ElevationModel::tr("The raster is too large");
		return product;
	}
	for (int row = 0; row < layout.height; ++row)
	{
		auto* line = product.image.scanLine(row);
		for (int col = 0; col < layout.width; ++col)
		{
			auto const k = std::size_t(row) * std::size_t(layout.width) + std::size_t(col);
			if (counts[k] == 0)
				line[col] = 0;
			else if (high > low)
				line[col] = toGrey((sums[k] - low) / (high - low));
			else
				line[col] = 254;  // for a cloud whose intensities are all equal
		}
	}
	product.extent = layout.extent();
	product.crs_spec = layout.crs_spec;
	return product;
}


}  // namespace


RasterProduct featureMap(const QStringList& paths, const ElevationModel& dtm, const FeatureMapParams& params)
{
	return featureMapOf(paths, dtm, params);
}

RasterProduct featureMap(const QStringList& paths, const GridTileCache& dtm, const FeatureMapParams& params)
{
	return featureMapOf(paths, dtm, params);
}

RasterProduct intensityMap(const QStringList& paths, const ElevationModel& dtm, double cell)
{
	return intensityMapOf(paths, dtm, cell);
}

RasterProduct intensityMap(const QStringList& paths, const GridTileCache& dtm, double cell)
{
	return intensityMapOf(paths, dtm, cell);
}


QRgb classColor(int classification)
{
	switch (classification)
	{
	case 0:  return qRgb(200, 200, 200);
	case 1:  return qRgb(150, 150, 150);
	case 2:  return qRgb(170, 110, 50);
	case 3:  return qRgb(190, 240, 120);
	case 4:  return qRgb(90, 200, 60);
	case 5:  return qRgb(0, 120, 0);
	case 6:  return qRgb(230, 50, 40);
	case 7:
	case 18: return qRgb(255, 0, 255);
	case 9:  return qRgb(40, 110, 240);
	case 17: return qRgb(255, 200, 0);
	default: return qRgb(100, 100, 100);
	}
}


namespace {

template <class Grid>
RasterProduct classificationMapOf(const QStringList& paths, const Grid& dtm, double cell)
{
	RasterProduct product;
	CellLayout const layout(dtm, cell);
	if (!layout.error.isEmpty())
	{
		product.error = layout.error;
		return product;
	}
	// Counts per cell for the classes 0 to 19 (the standard classes of LAS 1.4 and one more);
	// the higher classes share a counter, which remembers the lowest of them.
	constexpr int class_slots = 21;
	auto const n = layout.size();
	std::vector<quint16> counts(n * class_slots, 0);
	std::vector<quint8> other_class(n, 0);
	auto const add = [&](const CloudPoint& p) {
		auto const k = layout.index(p.x, p.y);
		if (k < 0)
			return;
		auto const c = p.classification;
		auto const slot = c < class_slots - 1 ? int(c) : class_slots - 1;
		auto& count = counts[std::size_t(k) * class_slots + std::size_t(slot)];
		if (count < std::numeric_limits<quint16>::max())
			++count;
		if (slot == class_slots - 1 && (other_class[std::size_t(k)] == 0 || c < other_class[std::size_t(k)]))
			other_class[std::size_t(k)] = c;
	};
	for (auto const& path : paths)
	{
		if (!PointCloud::read(path, add, nullptr, &product.error))
			return product;
	}
	product.image = QImage(layout.width, layout.height, QImage::Format_RGB32);
	if (product.image.isNull())
	{
		product.error = ElevationModel::tr("The raster is too large");
		return product;
	}
	for (int row = 0; row < layout.height; ++row)
	{
		auto* line = reinterpret_cast<QRgb*>(product.image.scanLine(row));
		for (int col = 0; col < layout.width; ++col)
		{
			auto const k = std::size_t(row) * std::size_t(layout.width) + std::size_t(col);
			auto const* cell_counts = &counts[k * class_slots];
			int best = -1;
			quint16 most = 0;
			for (int slot = 0; slot < class_slots; ++slot)
			{
				if (cell_counts[slot] > most)   // strictly more: a tie keeps the lower class
				{
					most = cell_counts[slot];
					best = slot;
				}
			}
			if (best < 0)
				line[col] = qRgb(0, 0, 0);
			else
				line[col] = classColor(best == class_slots - 1 ? int(other_class[k]) : best);
		}
	}
	product.extent = layout.extent();
	product.crs_spec = layout.crs_spec;
	return product;
}


}  // namespace


RasterProduct classificationMap(const QStringList& paths, const ElevationModel& dtm, double cell)
{
	return classificationMapOf(paths, dtm, cell);
}

RasterProduct classificationMap(const QStringList& paths, const GridTileCache& dtm, double cell)
{
	return classificationMapOf(paths, dtm, cell);
}


VegetationStatistics vegetationStatistics(const QStringList& paths, const ElevationModel& dtm,
                                          const std::vector<VegetationSample>& samples, const VegetationParams& params)
{
	// the design notes, "Vegetation statistics".
	VegetationStatistics statistics;
	if (samples.empty())
	{
		statistics.error = tr("Select at least one sample area.");
		return statistics;
	}
	CellLayout const layout(dtm, params.cell);
	if (!layout.error.isEmpty())
	{
		statistics.error = layout.error;
		return statistics;
	}
	auto const indices = vegetationIndices(paths, dtm, layout, params);
	if (!indices.error.isEmpty())
	{
		statistics.error = indices.error;
		return statistics;
	}

	// Running means and sums of squared deviations (Welford), exact for equal values.
	struct Running
	{
		qint64 n = 0;
		double mean = 0.0;
		double m2 = 0.0;
		void add(double x)
		{
			++n;
			auto const delta = x - mean;
			mean += delta / double(n);
			m2 += delta * (x - mean);
		}
	};
	struct Sums
	{
		Running v, u;
	};
	std::vector<Sums> sums;
	auto const extent = layout.extent();
	for (auto const& sample : samples)
	{
		auto found = std::find_if(statistics.classes.begin(), statistics.classes.end(), [&sample](const VegetationClassStatistics& c) {
			return c.name == sample.name;
		});
		if (found == statistics.classes.end())
		{
			VegetationClassStatistics entry;
			entry.name = sample.name;
			statistics.classes.push_back(entry);
			sums.emplace_back();
			found = statistics.classes.end() - 1;
		}
		auto& entry = *found;
		auto& sum = sums[std::size_t(found - statistics.classes.begin())];
		++entry.samples;
		auto const box = sample.polygon.boundingRect().intersected(extent);
		if (box.isEmpty())
			continue;
		auto const c0 = std::max(0, int(std::floor((box.left() - layout.left) / layout.cell)));
		auto const c1 = std::min(layout.width - 1, int(std::floor((box.right() - layout.left) / layout.cell)));
		auto const top = layout.bottom + layout.height * layout.cell;
		auto const r0 = std::max(0, int(std::floor((top - box.bottom()) / layout.cell)));
		auto const r1 = std::min(layout.height - 1, int(std::floor((top - box.top()) / layout.cell)));
		for (int row = r0; row <= r1; ++row)
		{
			for (int col = c0; col <= c1; ++col)
			{
				if (!sample.polygon.containsPoint(layout.center(col, row), Qt::OddEvenFill))
					continue;
				++entry.cells;
				auto const k = std::size_t(row) * std::size_t(layout.width) + std::size_t(col);
				auto const v = indices.vegetation[k];
				if (v <= 1.0)
				{
					++entry.vegetation_cells;
					sum.v.add(v);
				}
				auto const u = indices.undergrowth[k];
				if (u <= 1.0)
				{
					++entry.undergrowth_cells;
					sum.u.add(u);
				}
			}
		}
	}
	auto const nan = std::numeric_limits<double>::quiet_NaN();
	auto const finish = [nan](const Running& running, double& mean, double& sd) {
		if (running.n == 0)
		{
			mean = sd = nan;
			return;
		}
		mean = running.mean;
		sd = std::sqrt(std::max(0.0, running.m2 / double(running.n)));
	};
	for (std::size_t i = 0; i < statistics.classes.size(); ++i)
	{
		auto& entry = statistics.classes[i];
		finish(sums[i].v, entry.vegetation_mean, entry.vegetation_sd);
		finish(sums[i].u, entry.undergrowth_mean, entry.undergrowth_sd);
	}
	auto const midpoints = [&statistics](double VegetationClassStatistics::* mean) {
		std::vector<double> means;
		for (auto const& entry : statistics.classes)
		{
			if (!std::isnan(entry.*mean))
				means.push_back(entry.*mean);
		}
		std::sort(means.begin(), means.end());
		std::vector<double> result;
		for (std::size_t i = 1; i < means.size(); ++i)
			result.push_back((means[i - 1] + means[i]) / 2.0);
		return result;
	};
	statistics.vegetation_thresholds = midpoints(&VegetationClassStatistics::vegetation_mean);
	statistics.undergrowth_thresholds = midpoints(&VegetationClassStatistics::undergrowth_mean);
	return statistics;
}


bool writeVegetationStatisticsCsv(const VegetationStatistics& statistics, const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	QTextStream out(&file);
	out.setCodec("UTF-8");
	auto const number = [](double value) {
		return std::isnan(value) ? QString{} : QString::number(value, 'f', 3);
	};
	out << "class;samples;cells;vegetation_mean;vegetation_sd;undergrowth_mean;undergrowth_sd\n";
	for (auto const& entry : statistics.classes)
	{
		out << entry.name << ';' << entry.samples << ';' << entry.cells << ';'
		    << number(entry.vegetation_mean) << ';' << number(entry.vegetation_sd) << ';'
		    << number(entry.undergrowth_mean) << ';' << number(entry.undergrowth_sd) << '\n';
	}
	out << "\nthreshold;number;value\n";
	for (std::size_t i = 0; i < statistics.vegetation_thresholds.size(); ++i)
		out << "vegetation;" << (i + 1) << ';' << number(statistics.vegetation_thresholds[i]) << '\n';
	for (std::size_t i = 0; i < statistics.undergrowth_thresholds.size(); ++i)
		out << "undergrowth;" << (i + 1) << ';' << number(statistics.undergrowth_thresholds[i]) << '\n';
	out.flush();
	if (file.error() != QFileDevice::NoError)
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
