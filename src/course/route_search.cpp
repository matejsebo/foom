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


#include "route_search.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace OpenOrienteering {

namespace {

/// The steps from a pixel: the 16 headings.
struct Step
{
	int dx;
	int dy;
	/** The pixels between the two ends which the step touches, as offsets; unused ones are (0, 0). */
	int mx[2];
	int my[2];
	int between;
	double length;
};

const Step steps[] = {
    { 1, 0, { 0, 0 }, { 0, 0 }, 0, 1.0 },   { -1, 0, { 0, 0 }, { 0, 0 }, 0, 1.0 },
    { 0, 1, { 0, 0 }, { 0, 0 }, 0, 1.0 },   { 0, -1, { 0, 0 }, { 0, 0 }, 0, 1.0 },
    { 1, 1, { 1, 0 }, { 0, 1 }, 2, 1.41421356 },    { -1, 1, { -1, 0 }, { 0, 1 }, 2, 1.41421356 },
    { 1, -1, { 1, 0 }, { 0, -1 }, 2, 1.41421356 },  { -1, -1, { -1, 0 }, { 0, -1 }, 2, 1.41421356 },
    { 2, 1, { 1, 1 }, { 0, 1 }, 2, 2.23606798 },    { -2, 1, { -1, -1 }, { 0, 1 }, 2, 2.23606798 },
    { 2, -1, { 1, 1 }, { 0, -1 }, 2, 2.23606798 },  { -2, -1, { -1, -1 }, { 0, -1 }, 2, 2.23606798 },
    { 1, 2, { 0, 1 }, { 1, 1 }, 2, 2.23606798 },    { -1, 2, { 0, -1 }, { 1, 1 }, 2, 2.23606798 },
    { 1, -2, { 0, 1 }, { -1, -1 }, 2, 2.23606798 }, { -1, -2, { 0, -1 }, { -1, -1 }, 2, 2.23606798 },
};

/// The smallest factor of a gradient: the heuristic of the search must not be larger than the truth.
constexpr double least_slope_factor = 0.85;

/// How far, in pixels, a place on an impassable pixel is taken to a passable one.
constexpr int snap_radius = 8;

/// Half the distance in m over which the slope of the terrain is taken.
constexpr double slope_half_distance_m = 5.0;

/// From this runnability on, as on roads, tracks and footpaths, running across a slope costs nothing extra.
constexpr int path_runnability = 93;

}  // namespace



double slopeFactor(double m)
{
	if (std::isnan(m))
		return 1.0;
	if (m > 0.0163)
		return 11.45 * m + (m < 0.31 ? std::pow(1.0 - m / 0.31, 2.55) : 0.0);
	if (m >= -0.0198)
		return 1.0;
	auto const a = -m;
	if (m >= -0.1598)
		return 0.998 - 3.225 * a + 18.07 * a * a;
	if (m >= -0.1976)
		return 1.0;
	if (m >= -0.2618)
		return 4.409 * a + 0.177;
	return 4.30 * a;
}


double sideSlopeFactor(double angle_to_fall_line)
{
	// At the headings 0, atan(1/2), 45 degrees, atan(2) and 90 degrees to the fall line; joined by straight lines.
	auto const quarter = 2 * std::atan(1.0);
	auto const narrow = std::atan(0.5);
	auto const wide = std::atan(2.0);
	auto const angle = std::max(0.0, std::min(std::abs(angle_to_fall_line), quarter));
	if (angle <= narrow)
		return angle / narrow / 3.0;
	if (angle <= wide)
		return 1.0 / 3.0;
	return 1.0 / 3.0 + (angle - wide) / (quarter - wide) * 2.0 / 3.0;
}

double sideSlopeExtra(double slope, double angle_to_fall_line)
{
	if (!(slope > 0.0))
		return 0.0;
	return 5.0 * slope * slope * slope * sideSlopeFactor(angle_to_fall_line);
}

double edgeSlopeFactor(double gx, double gy, double ux, double uy, double edge_m, double cell_m)
{
	if (!(cell_m > 0.0) || !(edge_m > 0.0))
		return slopeFactor(gx * ux + gy * uy);
	// The lines between columns and between rows which the edge crosses: the whole number or one more.
	auto const columns = edge_m * std::abs(ux) / cell_m;
	auto const rows = edge_m * std::abs(uy) / cell_m;
	auto const column_count = std::floor(columns);
	auto const row_count = std::floor(rows);
	auto const column_more = columns - column_count;
	auto const row_more = rows - row_count;
	// A crossing changes the height of the edge by a cell's step of the slope.
	auto const column_step = cell_m * gx * (ux < 0.0 ? -1.0 : 1.0);
	auto const row_step = cell_m * gy * (uy < 0.0 ? -1.0 : 1.0);
	auto factor = 0.0;
	for (int c = 0; c < 2; ++c)
	{
		auto const column_weight = c == 0 ? 1.0 - column_more : column_more;
		if (column_weight <= 0.0)
			continue;
		for (int r = 0; r < 2; ++r)
		{
			auto const row_weight = r == 0 ? 1.0 - row_more : row_more;
			if (row_weight <= 0.0)
				continue;
			auto const rise = (column_count + c) * column_step + (row_count + r) * row_step;
			factor += column_weight * row_weight * slopeFactor(rise / edge_m);
		}
	}
	return factor;
}



struct RouteSearch::Data
{
	const RouteTerrain& terrain;
	RouteSettings settings;
	/** The size of a pixel in m of the terrain. */
	double pixel_m;
	/** The pace in s per m. */
	double pace;
	RouteHeights heights;
	bool use_heights = false;
	/** The size of the cells of the grid of the heights in m; 0 for none. */
	double cell_m = 0.0;
	
	std::vector<float> cost;
	std::vector<qint32> parent;
	std::vector<float> height;
	std::vector<quint8> height_known;  ///< 0 not asked, 1 known, 2 not known
	std::vector<float> gradient_x;     ///< the slope of the terrain, per m to the east
	std::vector<float> gradient_y;     ///< per m to the south
	std::vector<quint8> gradient_known;  ///< 0 not asked, 1 known, 2 not known
	std::vector<std::size_t> touched;
	
	Data(const RouteTerrain& terrain, const RouteSettings& settings, unsigned int scale_denominator)
	: terrain(terrain)
	, settings(settings)
	, pixel_m(terrain.pixel * scale_denominator / 1000.0)
	, pace(settings.base_pace / 1000.0)
	{}
	
	std::size_t indexOf(int x, int y) const { return std::size_t(y) * std::size_t(terrain.width) + std::size_t(x); }
	bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < terrain.width && y < terrain.height; }
	bool passable(int x, int y) const
	{
		auto const i = indexOf(x, y);
		return !terrain.impassable[i] && terrain.runnability[i] > 0;
	}
	MapCoordF centre(int x, int y) const
	{
		return MapCoordF(terrain.area.left() + (x + 0.5) * terrain.pixel, terrain.area.top() + (y + 0.5) * terrain.pixel);
	}
	
	/// The height of a pixel; false where it is not known.
	bool heightOf(int x, int y, double& result)
	{
		auto const i = indexOf(x, y);
		if (height_known.empty())
		{
			height.assign(terrain.runnability.size(), 0.0f);
			height_known.assign(terrain.runnability.size(), 0);
		}
		if (height_known[i] == 0)
		{
			auto const value = heights(centre(x, y));
			height_known[i] = std::isnan(value) ? 2 : 1;
			height[i] = float(value);
		}
		result = height[i];
		return height_known[i] == 1;
	}
	
	/// The gradient of the heights at a pixel, over the slope distance; false where it is not known.
	bool gradientOf(int x, int y, double& gx, double& gy)
	{
		auto const i = indexOf(x, y);
		if (gradient_known.empty())
		{
			gradient_x.assign(terrain.runnability.size(), 0.0f);
			gradient_y.assign(terrain.runnability.size(), 0.0f);
			gradient_known.assign(terrain.runnability.size(), 0);
		}
		if (gradient_known[i] == 0)
		{
			auto const c = centre(x, y);
			auto const d = slope_half_distance_m / pixel_m * terrain.pixel;  // in mm of the map
			auto const east = heights(MapCoordF(c.x() + d, c.y()));
			auto const west = heights(MapCoordF(c.x() - d, c.y()));
			auto const south = heights(MapCoordF(c.x(), c.y() + d));
			auto const north = heights(MapCoordF(c.x(), c.y() - d));
			auto const known = !std::isnan(east) && !std::isnan(west) && !std::isnan(south) && !std::isnan(north);
			gradient_known[i] = known ? 1 : 2;
			if (known)
			{
				gradient_x[i] = float((east - west) / (2 * slope_half_distance_m));
				gradient_y[i] = float((south - north) / (2 * slope_half_distance_m));
			}
		}
		gx = gradient_x[i];
		gy = gradient_y[i];
		return gradient_known[i] == 1;
	}
	
	/// The extra time across the slope of a step, in s; the design notes, "Route analysis: the extra time across a slope".
	double sideSlopeTime(int x, int y, int nx, int ny, const Step& step)
	{
		auto const off_path = (terrain.runnability[indexOf(x, y)] < path_runnability ? 1 : 0)
		                      + (terrain.runnability[indexOf(nx, ny)] < path_runnability ? 1 : 0);
		if (off_path == 0)
			return 0.0;
		double ax, ay, bx, by;
		if (!gradientOf(x, y, ax, ay) || !gradientOf(nx, ny, bx, by))
			return 0.0;
		auto const gx = (ax + bx) / 2;
		auto const gy = (ay + by) / 2;
		auto const slope = std::hypot(gx, gy);
		if (slope <= 0.0)
			return 0.0;
		auto const along = std::abs(step.dx * gx + step.dy * gy) / (std::hypot(double(step.dx), double(step.dy)) * slope);
		auto const angle = std::acos(std::min(1.0, along));
		return step.length * pixel_m * pace * sideSlopeExtra(slope, angle) * off_path / 2;
	}
	
	/// The time of a step from a pixel, in s; negative when the step cannot be made.
	double stepTime(int x, int y, const Step& step)
	{
		auto const nx = x + step.dx;
		auto const ny = y + step.dy;
		if (!inside(nx, ny) || !passable(nx, ny))
			return -1.0;
		auto slowness = 100.0 / terrain.runnability[indexOf(x, y)] + 100.0 / terrain.runnability[indexOf(nx, ny)];
		auto count = 2;
		for (int b = 0; b < step.between; ++b)
		{
			auto const bx = x + step.mx[b];
			auto const by = y + step.my[b];
			if (!passable(bx, by))
				return -1.0;
			// A diagonal step touches the two pixels beside it only at their corners: they bar it, and do not slow it.
			if (step.length > 2.0)
			{
				slowness += 100.0 / terrain.runnability[indexOf(bx, by)];
				++count;
			}
		}
		auto time = step.length * pixel_m * pace * slowness / count;
		if (use_heights)
		{
			double here;
			double there;
			double ax, ay, bx, by;
			if (cell_m > 0.0 && gradientOf(x, y, ax, ay) && gradientOf(nx, ny, bx, by))
			{
				// the design notes.
				auto const norm = std::hypot(double(step.dx), double(step.dy));
				time *= edgeSlopeFactor((ax + bx) / 2, (ay + by) / 2, step.dx / norm, step.dy / norm, 4 * step.length * pixel_m, cell_m);
			}
			else if (heightOf(x, y, here) && heightOf(nx, ny, there))
			{
				time *= slopeFactor((there - here) / (step.length * pixel_m));
			}
			time += sideSlopeTime(x, y, nx, ny, step);
		}
		return time;
	}
	
	bool pixelOf(const MapCoordF& place, int& x, int& y) const
	{
		x = int(std::floor((place.x() - terrain.area.left()) / terrain.pixel));
		y = int(std::floor((place.y() - terrain.area.top()) / terrain.pixel));
		return inside(x, y);
	}
	
	/// Takes a pixel to the nearest passable one within the snap radius.
	bool snap(int& x, int& y) const
	{
		if (passable(x, y))
			return true;
		auto best = std::numeric_limits<int>::max();
		auto bx = x;
		auto by = y;
		for (int dy = -snap_radius; dy <= snap_radius; ++dy)
		{
			for (int dx = -snap_radius; dx <= snap_radius; ++dx)
			{
				auto const distance = dx * dx + dy * dy;
				if (distance < best && inside(x + dx, y + dy) && passable(x + dx, y + dy))
				{
					best = distance;
					bx = x + dx;
					by = y + dy;
				}
			}
		}
		if (best == std::numeric_limits<int>::max())
			return false;
		x = bx;
		y = by;
		return true;
	}
};



RouteSearch::RouteSearch(const RouteTerrain& terrain, const RouteSettings& settings, unsigned int scale_denominator)
: d(new Data(terrain, settings, scale_denominator))
{}

RouteSearch::~RouteSearch()
{
	delete d;
}


void RouteSearch::setHeights(RouteHeights heights)
{
	d->heights = std::move(heights);
	d->use_heights = d->settings.use_elevation && bool(d->heights);
	d->height.clear();
	d->height_known.clear();
	d->gradient_x.clear();
	d->gradient_y.clear();
	d->gradient_known.clear();
}


bool RouteSearch::usesHeights() const
{
	return d->use_heights;
}


void RouteSearch::setGridCell(double cell_m)
{
	d->cell_m = cell_m > 0.0 ? cell_m : 0.0;
}


double RouteSearch::straightTime(const MapCoordF& from, const MapCoordF& to) const
{
	auto const length_mm = std::hypot(to.x() - from.x(), to.y() - from.y());
	auto const steps_count = std::max(1, int(std::ceil(length_mm / d->terrain.pixel)));
	auto slowness = 0.0;
	for (int i = 0; i < steps_count; ++i)
	{
		auto const t = (i + 0.5) / steps_count;
		auto const runnability = d->terrain.runnabilityAt(MapCoordF(from.x() + (to.x() - from.x()) * t, from.y() + (to.y() - from.y()) * t));
		slowness += 100.0 / std::max(runnability, 1);
	}
	return length_mm * d->pixel_m / d->terrain.pixel * d->pace * slowness / steps_count;
}


RouteResult RouteSearch::fastest(const MapCoordF& from, const MapCoordF& to)
{
	RouteResult result;
	auto const& terrain = d->terrain;
	if (terrain.isEmpty())
		return result;
	auto sx = 0;
	auto sy = 0;
	auto gx = 0;
	auto gy = 0;
	if (!d->pixelOf(from, sx, sy) || !d->pixelOf(to, gx, gy) || !d->snap(sx, sy) || !d->snap(gx, gy))
		return result;
	
	auto const count = terrain.runnability.size();
	if (d->cost.size() != count)
	{
		d->cost.assign(count, std::numeric_limits<float>::infinity());
		d->parent.assign(count, -1);
	}
	for (auto const i : d->touched)
	{
		d->cost[i] = std::numeric_limits<float>::infinity();
		d->parent[i] = -1;
	}
	d->touched.clear();
	
	// The least time which is left from a pixel: the straight way at the full speed, downhill all along.
	auto const least = d->pixel_m * d->pace * (d->use_heights ? least_slope_factor : 1.0);
	auto const estimate = [gx, gy, least](int x, int y) { return float(std::hypot(double(gx - x), double(gy - y)) * least); };
	
	using Entry = std::pair<float, std::size_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	auto const start = d->indexOf(sx, sy);
	auto const goal = d->indexOf(gx, gy);
	d->cost[start] = 0.0f;
	d->touched.push_back(start);
	open.push({ estimate(sx, sy), start });
	auto reached = false;
	while (!open.empty())
	{
		auto const entry = open.top();
		open.pop();
		auto const i = entry.second;
		auto const x = int(i % std::size_t(terrain.width));
		auto const y = int(i / std::size_t(terrain.width));
		// An entry which was overtaken by a faster way to its pixel.
		if (entry.first > d->cost[i] + estimate(x, y) + 1e-3f)
			continue;
		if (i == goal)
		{
			reached = true;
			break;
		}
		for (auto const& step : steps)
		{
			auto const time = d->stepTime(x, y, step);
			if (time < 0.0)
				continue;
			auto const next = d->indexOf(x + step.dx, y + step.dy);
			auto const cost = d->cost[i] + float(time);
			if (cost < d->cost[next])
			{
				if (d->parent[next] < 0 && next != start)
					d->touched.push_back(next);
				d->cost[next] = cost;
				d->parent[next] = qint32(i);
				open.push({ cost + estimate(x + step.dx, y + step.dy), next });
			}
		}
	}
	if (!reached)
		return result;
	
	// The route, from the goal back; pixels in a straight row are left out.
	std::vector<std::size_t> pixels;
	for (auto i = goal; ; i = std::size_t(d->parent[i]))
	{
		pixels.push_back(i);
		if (i == start)
			break;
	}
	std::reverse(begin(pixels), end(pixels));
	auto const xOf = [&terrain](std::size_t i) { return int(i % std::size_t(terrain.width)); };
	auto const yOf = [&terrain](std::size_t i) { return int(i / std::size_t(terrain.width)); };
	result.found = true;
	result.time_s = d->cost[goal];
	result.points.push_back(from);
	for (std::size_t p = 0; p < pixels.size(); ++p)
	{
		auto const x = xOf(pixels[p]);
		auto const y = yOf(pixels[p]);
		if (p > 0)
		{
			auto const px = xOf(pixels[p - 1]);
			auto const py = yOf(pixels[p - 1]);
			result.length_m += std::hypot(double(x - px), double(y - py)) * d->pixel_m;
			if (d->use_heights)
			{
				double here;
				double before;
				if (d->heightOf(x, y, here) && d->heightOf(px, py, before) && here > before)
					result.climb_m += here - before;
			}
			if (p + 1 < pixels.size())
			{
				auto const nx = xOf(pixels[p + 1]);
				auto const ny = yOf(pixels[p + 1]);
				if ((x - px) * (ny - y) == (y - py) * (nx - x))
					continue;
			}
		}
		result.points.push_back(d->centre(x, y));
	}
	result.points.push_back(to);
	return result;
}


RouteResult RouteSearch::fastestVia(const MapCoordF& from, const std::vector<MapCoordF>& via, const MapCoordF& to)
{
	RouteResult result;
	auto here = from;
	for (std::size_t i = 0; i <= via.size(); ++i)
	{
		auto const there = i < via.size() ? via[i] : to;
		auto const piece = fastest(here, there);
		if (!piece.found)
			return {};
		// The place at which two pieces meet is in the route once.
		result.points.insert(result.points.end(), piece.points.begin() + (result.points.empty() ? 0 : 1), piece.points.end());
		result.length_m += piece.length_m;
		result.time_s += piece.time_s;
		result.climb_m += piece.climb_m;
		here = there;
	}
	result.found = true;
	return result;
}


}  // namespace OpenOrienteering
