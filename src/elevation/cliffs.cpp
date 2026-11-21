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

#include "cliffs.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

#include <QtGlobal>
#include <QtMath>
#include <QLatin1String>
#include <QString>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/symbol.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

namespace {

/// A threshold: the degrees asked for and the slope, in degrees, at which a pixel turns black.
struct Turn
{
	double threshold;
	double slope;
};

// the design notes, "Cliffs from the slope gradient": the gradients
// 0.7125, 1.1115 and 1.413, as angles.
constexpr Turn steep_turns[] = { { 0.0, 0.0 }, { 30.0, 35.47 }, { 40.0, 48.02 }, { 45.0, 54.71 } };

/// A region of steep corners is a cliff when its steepest corner reaches this factor of the threshold (1.28 to 1.32).
constexpr double cliff_factor = 1.3;

template <std::size_t N>
double interpolated(const Turn (&turns)[N], double threshold)
{
	auto k = std::size_t(1);
	while (k + 1 < N && threshold > turns[k].threshold)
		++k;
	auto const& a = turns[k - 1];
	auto const& b = turns[k];
	auto const slope = a.slope + (threshold - a.threshold) * (b.slope - a.slope) / (b.threshold - a.threshold);
	return std::tan(qDegreesToRadians(qBound(0.0, slope, 89.9)));
}

const MapColor* blackColor(Map& map)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (color->getName().compare(QLatin1String("black"), Qt::CaseInsensitive) == 0)
			return color;
	}
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (color->getName().contains(QLatin1String("black"), Qt::CaseInsensitive))
			return color;
	}
	auto* color = new MapColor(QStringLiteral("Black"), 0);
	color->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(color, 0);
	// What is drawn already refers to the colours by their place in the table.
	map.updateAllObjects();
	return color;
}

/// An edge of the outline, between vertices of the lattice of the squares' corners.
struct Edge
{
	int u0, v0, u1, v1;
	bool used = false;
};

}  // namespace



std::unique_ptr<ElevationModel> gaussianFiltered(const ElevationModel& grid)
{
	// the design notes, "Cliffs from the slope gradient": the filter of one cell.
	auto result = std::make_unique<ElevationModel>(grid);
	auto const width = grid.width();
	auto const height = grid.height();
	if (width < 3 || height < 3)
		return result;
	auto const side = std::exp(-0.5);
	double const weights[3] = { side / (1 + 2 * side), 1 / (1 + 2 * side), side / (1 + 2 * side) };
	for (int row = 1; row + 1 < height; ++row)
	{
		for (int col = 1; col + 1 < width; ++col)
		{
			double sum = 0.0;
			auto valid = true;
			for (int dr = -1; dr <= 1 && valid; ++dr)
			{
				for (int dc = -1; dc <= 1; ++dc)
				{
					auto const value = grid.value(col + dc, row + dr);
					if (std::isnan(value))
					{
						valid = false;
						break;
					}
					sum += weights[dr + 1] * weights[dc + 1] * value;
				}
			}
			if (valid)
				result->setValue(col, row, float(sum));
		}
	}
	return result;
}


double steepGradient(double threshold)
{
	return interpolated(steep_turns, threshold);
}


double cliffGradient(double threshold)
{
	return cliff_factor * steepGradient(threshold);
}


CliffRaster cliffRaster(const ElevationModel& grid, double threshold)
{
	// the design notes, "Cliffs from the slope gradient".
	CliffRaster raster;
	if (grid.width() < 2 || grid.height() < 2)
		return raster;
	auto const filtered = gaussianFiltered(grid);
	auto const& f = *filtered;
	raster.width = grid.width() - 1;
	raster.height = grid.height() - 1;
	raster.cell = grid.cellSize();
	auto const centre = grid.cellCenter(0, 0);
	raster.first = QPointF(centre.x() + raster.cell / 2, centre.y() - raster.cell / 2);
	auto const count = std::size_t(raster.width) * std::size_t(raster.height);
	raster.gradient.assign(count, std::numeric_limits<float>::quiet_NaN());
	raster.state.assign(count, CliffRaster::Flat);
	auto const steep = steepGradient(threshold);
	auto const cliff = cliffGradient(threshold);
	for (int j = 0; j < raster.height; ++j)
	{
		for (int i = 0; i < raster.width; ++i)
		{
			auto const a = f.value(i, j);
			auto const b = f.value(i + 1, j);
			auto const c = f.value(i, j + 1);
			auto const d = f.value(i + 1, j + 1);
			if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
				continue;
			auto const gx = ((double(b) + d) - (double(a) + c)) / (2 * raster.cell);
			auto const gy = ((double(c) + d) - (double(a) + b)) / (2 * raster.cell);
			auto const g = std::hypot(gx, gy);
			auto const index = std::size_t(j) * std::size_t(raster.width) + std::size_t(i);
			raster.gradient[index] = float(g);
			if (g > steep)
				raster.state[index] = CliffRaster::Steep;
		}
	}
	
	// A region of steep corners, joined through the eight neighbours, is a cliff
	// when its steepest corner reaches the cliff gradient.
	std::vector<bool> seen(count, false);
	for (std::size_t start = 0; start < count; ++start)
	{
		if (seen[start] || raster.state[start] != CliffRaster::Steep)
			continue;
		std::vector<std::size_t> region;
		std::vector<std::size_t> stack = { start };
		seen[start] = true;
		auto steepest = 0.0;
		while (!stack.empty())
		{
			auto const index = stack.back();
			stack.pop_back();
			region.push_back(index);
			steepest = std::max(steepest, double(raster.gradient[index]));
			auto const i = int(index % std::size_t(raster.width));
			auto const j = int(index / std::size_t(raster.width));
			for (int dj = -1; dj <= 1; ++dj)
			{
				for (int di = -1; di <= 1; ++di)
				{
					auto const ni = i + di;
					auto const nj = j + dj;
					if (ni < 0 || nj < 0 || ni >= raster.width || nj >= raster.height)
						continue;
					auto const next = std::size_t(nj) * std::size_t(raster.width) + std::size_t(ni);
					if (!seen[next] && raster.state[next] == CliffRaster::Steep)
					{
						seen[next] = true;
						stack.push_back(next);
					}
				}
			}
		}
		if (steepest >= cliff)
		{
			for (auto const index : region)
				raster.state[index] = CliffRaster::Cliff;
		}
	}
	return raster;
}


std::vector<CliffArea> cliffAreas(const CliffRaster& raster, const CliffParams& params)
{
	// the design notes, "Cliffs from the slope gradient".
	std::vector<CliffArea> areas;
	auto const width = raster.width;
	auto const height = raster.height;
	auto const index = [width](int i, int j) { return std::size_t(j) * std::size_t(width) + std::size_t(i); };
	std::vector<int> component(std::size_t(width) * std::size_t(height), -1);
	auto const isCliff = [&raster, width, height](int i, int j) {
		return i >= 0 && j >= 0 && i < width && j < height && raster.at(i, j) == CliffRaster::Cliff;
	};
	
	int label = 0;
	for (int j0 = 0; j0 < height; ++j0)
	{
		for (int i0 = 0; i0 < width; ++i0)
		{
			if (!isCliff(i0, j0) || component[index(i0, j0)] >= 0)
				continue;
			// The component, through the eight neighbours.
			std::vector<std::pair<int, int>> pixels;
			std::vector<std::pair<int, int>> stack = { { i0, j0 } };
			component[index(i0, j0)] = label;
			int min_i = i0, max_i = i0, min_j = j0, max_j = j0;
			while (!stack.empty())
			{
				auto const p = stack.back();
				stack.pop_back();
				pixels.push_back(p);
				min_i = std::min(min_i, p.first);
				max_i = std::max(max_i, p.first);
				min_j = std::min(min_j, p.second);
				max_j = std::max(max_j, p.second);
				for (int dj = -1; dj <= 1; ++dj)
				{
					for (int di = -1; di <= 1; ++di)
					{
						auto const i = p.first + di;
						auto const j = p.second + dj;
						if ((di || dj) && isCliff(i, j) && component[index(i, j)] < 0)
						{
							component[index(i, j)] = label;
							stack.emplace_back(i, j);
						}
					}
				}
			}
			auto const this_label = label++;
			if (int(pixels.size()) < params.min_area || std::max(max_i - min_i, max_j - min_j) + 1 < params.min_length)
				continue;
			
			// The outline: the sides of the squares which no square of the component shares,
			// clockwise on screen (rows grow southwards).
			auto const inside = [&](int i, int j) { return isCliff(i, j) && component[index(i, j)] == this_label; };
			std::vector<Edge> edges;
			for (auto const& p : pixels)
			{
				auto const i = p.first;
				auto const j = p.second;
				if (!inside(i, j - 1))
					edges.push_back({ i, j, i + 1, j });
				if (!inside(i + 1, j))
					edges.push_back({ i + 1, j, i + 1, j + 1 });
				if (!inside(i, j + 1))
					edges.push_back({ i + 1, j + 1, i, j + 1 });
				if (!inside(i - 1, j))
					edges.push_back({ i, j + 1, i, j });
			}
			std::multimap<std::pair<int, int>, std::size_t> starts;
			for (std::size_t k = 0; k < edges.size(); ++k)
				starts.emplace(std::make_pair(edges[k].u0, edges[k].v0), k);
			
			CliffArea area;
			area.pixels = int(pixels.size());
			for (std::size_t start = 0; start < edges.size(); ++start)
			{
				if (edges[start].used)
					continue;
				std::vector<std::pair<int, int>> ring;
				auto k = start;
				while (!edges[k].used)
				{
					auto& edge = edges[k];
					edge.used = true;
					ring.emplace_back(edge.u0, edge.v0);
					auto const du = edge.u1 - edge.u0;
					auto const dv = edge.v1 - edge.v0;
					// At a corner where two squares touch, turn right: around the same square.
					auto next = edges.size();
					auto best = 3;
					auto const range = starts.equal_range({ edge.u1, edge.v1 });
					for (auto it = range.first; it != range.second; ++it)
					{
						auto const& candidate = edges[it->second];
						if (candidate.used)
							continue;
						auto const cu = candidate.u1 - candidate.u0;
						auto const cv = candidate.v1 - candidate.v0;
						auto const rank = (cu == -dv && cv == du) ? 0 : (cu == du && cv == dv) ? 1 : 2;
						if (rank < best)
						{
							best = rank;
							next = it->second;
						}
					}
					if (next == edges.size())
						break;
					k = next;
				}
				// Collinear vertices are dropped.
				std::vector<QPointF> points;
				auto const n = ring.size();
				for (std::size_t m = 0; m < n; ++m)
				{
					auto const& prev = ring[(m + n - 1) % n];
					auto const& cur = ring[m];
					auto const& next = ring[(m + 1) % n];
					if ((cur.first - prev.first) * (next.second - cur.second) == (cur.second - prev.second) * (next.first - cur.first))
						continue;
					points.emplace_back(raster.first.x() + (cur.first - 0.5) * raster.cell, raster.first.y() - (cur.second - 0.5) * raster.cell);
				}
				if (points.size() >= 3)
					area.rings.push_back(std::move(points));
			}
			if (!area.rings.empty())
				areas.push_back(std::move(area));
		}
	}
	return areas;
}


std::vector<CliffArea> findCliffs(const ElevationModel& grid, const CliffParams& params)
{
	return cliffAreas(cliffRaster(grid, params.threshold), params);
}


const AreaSymbol* cliffSymbol(Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Area
		    && symbol->getNumberComponent(0) == 10080
		    && symbol->getNumberComponent(1) <= 0)
			return symbol->asArea();
	}
	auto* symbol = new AreaSymbol();
	symbol->setNumberComponent(0, 10080);
	symbol->setNumberComponent(1, -1);
	symbol->setName(QStringLiteral("Cliff from slope gradient (area)"));
	symbol->setColor(blackColor(map));
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}


std::vector<std::unique_ptr<Object>> makeCliffObjects(const Map& map, const std::vector<CliffArea>& areas, const AreaSymbol* symbol)
{
	std::vector<std::unique_ptr<Object>> objects;
	if (!symbol)
		return objects;
	auto const& georef = map.getGeoreferencing();
	for (auto const& area : areas)
	{
		auto object = std::make_unique<PathObject>(symbol);
		for (auto const& ring : area.rings)
		{
			auto start = true;
			for (auto const& point : ring)
			{
				object->addCoordinate(MapCoord(georef.toMapCoordF(point)), start);
				start = false;
			}
		}
		object->closeAllParts();
		object->setTag(QStringLiteral("feature"), QStringLiteral("cliff"));
		objects.push_back(std::move(object));
	}
	return objects;
}


}  // namespace OpenOrienteering
