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
#include <QImage>
#include <QPointF>
#include <QRectF>

#include "elevation/elevation_model.h"

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

	CellLayout(const ElevationModel& dtm, double cell_size)
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

	QRectF extent() const
	{
		return { left, bottom, width * cell, height * cell };
	}
};


/// The terrain model's height under a point, falling back to the cell's value at the grid's edge.
double groundAt(const ElevationModel& dtm, double x, double y)
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


}  // namespace



RasterProduct featureMap(const QStringList& paths, const ElevationModel& dtm, const FeatureMapParams& params)
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
			line[col] = toGrey(1.0 - std::atan(std::hypot(g.x(), g.y())) / half_pi);
		}
	}
	product.extent = extent;
	product.crs_spec = layout.crs_spec;
	return product;
}


RasterProduct intensityMap(const QStringList& paths, const ElevationModel& dtm, double cell)
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


RasterProduct classificationMap(const QStringList& paths, const ElevationModel& dtm, double cell)
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


}  // namespace OpenOrienteering
