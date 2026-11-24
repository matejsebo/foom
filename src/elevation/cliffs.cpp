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
#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
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
#include "core/symbols/line_symbol.h"
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
// Known at 30, 35, 40, 45 and 50 degrees; at 60 nothing is black on a fixture whose
// steepest corner has 79.5 degrees, which the last segment continued gives too.
constexpr Turn steep_turns[] = { { 0.0, 0.0 }, { 30.0, 35.47 }, { 35.0, 41.76 }, { 40.0, 48.02 }, { 45.0, 54.71 }, { 50.0, 69.49 } };

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


/// Drops the points which lie within the tolerance of the line through their kept neighbours (Douglas and Peucker).
std::vector<QPointF> simplified(const std::vector<QPointF>& points, double tolerance)
{
	if (points.size() < 3)
		return points;
	std::vector<bool> keep(points.size(), false);
	keep.front() = true;
	keep.back() = true;
	std::vector<std::pair<std::size_t, std::size_t>> spans = { { 0, points.size() - 1 } };
	while (!spans.empty())
	{
		auto const span = spans.back();
		spans.pop_back();
		auto const& a = points[span.first];
		auto const& b = points[span.second];
		auto const length = std::hypot(b.x() - a.x(), b.y() - a.y());
		auto worst = tolerance;
		auto at = span.first;
		for (auto k = span.first + 1; k < span.second; ++k)
		{
			auto const& p = points[k];
			auto const distance = length > 0.0
			                      ? std::abs((b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x())) / length
			                      : std::hypot(p.x() - a.x(), p.y() - a.y());
			if (distance > worst)
			{
				worst = distance;
				at = k;
			}
		}
		if (at != span.first)
		{
			keep[at] = true;
			spans.emplace_back(span.first, at);
			spans.emplace_back(at, span.second);
		}
	}
	std::vector<QPointF> result;
	for (std::size_t k = 0; k < points.size(); ++k)
	{
		if (keep[k])
			result.push_back(points[k]);
	}
	return result;
}


/// The line along the middle of a narrow region, in corners (x eastwards, y southwards), and the region's length in cells.
struct MiddleLine
{
	std::vector<QPointF> points;
	double length = 0.0;
};

/// the design notes, "Cliff lines": no points when the region is to stay an area.
MiddleLine middleLine(const std::vector<std::pair<int, int>>& pixels, double line_width)
{
	MiddleLine none;
	if (pixels.size() < 2 || !(line_width > 0.0))
		return none;
	std::map<std::pair<int, int>, int> place;
	for (std::size_t k = 0; k < pixels.size(); ++k)
		place.emplace(pixels[k], int(k));
	// The distance of every corner from one of them, through the region and its eight neighbours.
	auto const distances = [&pixels, &place](int start) {
		std::vector<int> distance(pixels.size(), -1);
		std::vector<int> queue = { start };
		distance[std::size_t(start)] = 0;
		for (std::size_t head = 0; head < queue.size(); ++head)
		{
			auto const k = std::size_t(queue[head]);
			for (int dj = -1; dj <= 1; ++dj)
			{
				for (int di = -1; di <= 1; ++di)
				{
					auto const it = place.find({ pixels[k].first + di, pixels[k].second + dj });
					if (it != place.end() && distance[std::size_t(it->second)] < 0)
					{
						distance[std::size_t(it->second)] = distance[k] + 1;
						queue.push_back(it->second);
					}
				}
			}
		}
		return distance;
	};
	auto const from_first = distances(0);
	auto const end = int(std::max_element(from_first.begin(), from_first.end()) - from_first.begin());
	auto const distance = distances(end);
	auto const last = *std::max_element(distance.begin(), distance.end());
	if (last < 1)
		return none;

	// The groups of equal distance; the first two and the last two are taken together,
	// which puts the line's ends on the middle of the region's ends.
	struct Group
	{
		double x = 0.0;
		double y = 0.0;
		int count = 0;
		int min_i = INT_MAX;
		int max_i = INT_MIN;
		int min_j = INT_MAX;
		int max_j = INT_MIN;
	};
	auto const merged = last >= 3;
	auto const group_of = [merged, last](int d) {
		if (!merged)
			return d;
		return d <= 1 ? 0 : (d >= last - 1 ? last - 2 : d - 1);
	};
	std::vector<Group> groups(std::size_t(merged ? last - 1 : last + 1));
	std::vector<Group> fronts(std::size_t(last + 1));
	for (std::size_t k = 0; k < pixels.size(); ++k)
	{
		auto& group = groups[std::size_t(group_of(distance[k]))];
		group.x += pixels[k].first;
		group.y += pixels[k].second;
		++group.count;
		auto& front = fronts[std::size_t(distance[k])];
		front.min_i = std::min(front.min_i, pixels[k].first);
		front.max_i = std::max(front.max_i, pixels[k].first);
		front.min_j = std::min(front.min_j, pixels[k].second);
		front.max_j = std::max(front.max_j, pixels[k].second);
	}
	// A ring or a fork has corners of one distance far apart: no line.
	for (auto const& front : fronts)
	{
		if (std::max(front.max_i - front.min_i, front.max_j - front.min_j) + 1 > 2 * line_width)
			return none;
	}
	std::vector<QPointF> points;
	for (auto const& group : groups)
	{
		if (group.count > 0)
			points.emplace_back(group.x / group.count, group.y / group.count);
	}
	points = simplified(points, 0.5);
	if (points.size() < 2)
		return none;
	// Half a cell more at each end: to the middle of the region's outermost corners.
	if (merged)
	{
		auto const lengthen = [](const QPointF& from, QPointF& end) {
			auto const length = std::hypot(end.x() - from.x(), end.y() - from.y());
			if (length > 0.0)
				end += (end - from) * (0.5 / length);
		};
		lengthen(points[1], points[0]);
		lengthen(points[points.size() - 2], points[points.size() - 1]);
	}
	auto length = 1.0;
	for (std::size_t k = 1; k < points.size(); ++k)
		length += std::hypot(points[k].x() - points[k - 1].x(), points[k].y() - points[k - 1].y());
	auto const width = double(pixels.size()) / length;
	if (width > line_width || length < 2 * width)
		return none;
	return { std::move(points), length };
}

}  // namespace



std::unique_ptr<ElevationModel> gaussianFiltered(const ElevationModel& grid, int n)
{
	// the design notes, "Cliffs from the slope gradient" and "Trees": the filter of n cells.
	auto result = std::make_unique<ElevationModel>(grid);
	auto const width = grid.width();
	auto const height = grid.height();
	if (n < 1 || width < 2 * n + 1 || height < 2 * n + 1)
		return result;
	std::vector<double> weights;
	for (int k = -n; k <= n; ++k)
		weights.push_back(std::exp(-0.5 * k * k / (double(n) * n)));
	auto const total = std::accumulate(weights.begin(), weights.end(), 0.0);
	for (auto& w : weights)
		w /= total;
	for (int row = n; row + n < height; ++row)
	{
		for (int col = n; col + n < width; ++col)
		{
			double sum = 0.0;
			auto valid = true;
			for (int dr = -n; dr <= n && valid; ++dr)
			{
				for (int dc = -n; dc <= n; ++dc)
				{
					auto const value = grid.value(col + dc, row + dr);
					if (std::isnan(value))
					{
						valid = false;
						break;
					}
					sum += weights[std::size_t(dr + n)] * weights[std::size_t(dc + n)] * value;
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
	// the design notes, "Cliffs from the slope gradient" and "Cliff lines".
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
			while (!stack.empty())
			{
				auto const p = stack.back();
				stack.pop_back();
				pixels.push_back(p);
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
			// A narrow region is a line along its middle; the minimum length is of the lines
			// and the minimum area of the areas.
			auto middle = middleLine(pixels, params.line_width);
			if (!middle.points.empty())
			{
				if (middle.length < params.min_length)
					continue;
				CliffArea cliff;
				cliff.pixels = int(pixels.size());
				for (auto const& point : middle.points)
					cliff.line.emplace_back(raster.first.x() + point.x() * raster.cell, raster.first.y() - point.y() * raster.cell);
				areas.push_back(std::move(cliff));
				continue;
			}
			if (int(pixels.size()) < params.min_area)
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
	auto cliffs = cliffAreas(cliffRaster(grid, params.threshold), params);
	// Cliff lines run with the lower ground on their right.
	auto const reach = 2 * grid.cellSize();
	for (auto& cliff : cliffs)
	{
		auto balance = 0.0;
		for (std::size_t k = 1; k < cliff.line.size(); ++k)
		{
			auto const& a = cliff.line[k - 1];
			auto const& b = cliff.line[k];
			auto const length = std::hypot(b.x() - a.x(), b.y() - a.y());
			if (!(length > 0.0))
				continue;
			// Projected coordinates: x eastwards, y northwards.
			QPointF const middle((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
			QPointF const right((b.y() - a.y()) / length * reach, -(b.x() - a.x()) / length * reach);
			auto const right_height = grid.at(middle + right);
			auto const left_height = grid.at(middle - right);
			if (!std::isnan(right_height) && !std::isnan(left_height))
				balance += (right_height - left_height) * length;
		}
		if (balance > 0.0)
			std::reverse(cliff.line.begin(), cliff.line.end());
	}
	return cliffs;
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


const LineSymbol* cliffLineSymbol(Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Line
		    && symbol->getNumberComponent(0) == 10082
		    && symbol->getNumberComponent(1) <= 0)
			return symbol->asLine();
	}
	auto* symbol = new LineSymbol();
	symbol->setNumberComponent(0, 10082);
	symbol->setNumberComponent(1, -1);
	symbol->setName(QStringLiteral("Cliff from slope gradient (line)"));
	symbol->setColor(blackColor(map));
	symbol->setLineWidth(0.18);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}


std::vector<std::unique_ptr<Object>> makeCliffObjects(const Map& map, const std::vector<CliffArea>& areas, const AreaSymbol* symbol, const LineSymbol* line_symbol)
{
	std::vector<std::unique_ptr<Object>> objects;
	auto const& georef = map.getGeoreferencing();
	for (auto const& area : areas)
	{
		if (area.isLine())
		{
			if (!line_symbol)
				continue;
			auto line = std::make_unique<PathObject>(line_symbol);
			for (auto const& point : area.line)
				line->addCoordinate(MapCoord(georef.toMapCoordF(point)));
			line->setTag(QStringLiteral("feature"), QStringLiteral("cliff"));
			objects.push_back(std::move(line));
			continue;
		}
		if (!symbol)
			continue;
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
