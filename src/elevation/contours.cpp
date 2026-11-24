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


#include "contours.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

#include <QtGlobal>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringLiteral>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/op.h"
#include "core/ops/smooth.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "elevation/elevation_model.h"
#include "undo/object_undo.h"
#include "undo/undo_manager.h"


namespace OpenOrienteering {

namespace {

/**
 * An edge of the lattice of cell centres.
 *
 * Edge 2 * (row * width + col) joins the centres (col, row) and (col + 1, row);
 * edge 2 * (row * width + col) + 1 joins (col, row) and (col, row + 1).
 */
using EdgeId = qint64;

struct Segment
{
	EdgeId a;
	EdgeId b;
};

/// A chain of edges at one level, closed when the first and last edge are joined.
struct Chain
{
	std::vector<EdgeId> edges;
	bool closed = false;
};


class ContourTracer
{
public:
	ContourTracer(const ElevationModel& grid, const ContourParams& params);

	std::vector<ContourLine> run();

private:
	EdgeId horizontalEdge(int col, int row) const { return 2 * (qint64(row) * grid.width() + col); }
	EdgeId verticalEdge(int col, int row) const { return 2 * (qint64(row) * grid.width() + col) + 1; }

	double level(int k) const { return first_level + k * params.interval; }

	/// The crossing point of the edge at the level, in projected coordinates.
	QPointF crossing(EdgeId edge, double level) const;

	void traceTile(int c0, int c1, int r0, int r1, std::vector<std::vector<Chain>>& chains_by_level) const;

	static std::vector<Chain> linkSegments(const std::vector<Segment>& segments);

	void joinChains(std::vector<Chain>& chains, double level) const;

	ContourLine makeLine(const Chain& chain, int k) const;

	void orient(ContourLine& line) const;

	void simplify(std::vector<QPointF>& points, bool closed) const;

	const ElevationModel& grid;
	const ContourParams& params;
	double first_level = 0.0;
	int num_levels = 0;
};


/// The levels of the range; returns their number and sets the first.
int contourLevels(double grid_minimum, double grid_maximum, const ContourParams& params, double& first_level)
{
	if (std::isnan(grid_minimum) || std::isnan(grid_maximum) || !(params.interval > 0.0))
		return 0;
	// The first level is the smallest multiple of the interval above the grid's minimum:
	// a level equal to the minimum would have no cell below it. A lowest level
	// chosen above the grid's minimum is a level itself (the design notes,
	// "Levels in the range").
	auto const k0 = (!std::isnan(params.minimum) && params.minimum > grid_minimum)
	                ? std::ceil(params.minimum / params.interval - 1e-9)
	                : std::floor(grid_minimum / params.interval + 1e-9) + 1;
	auto highest = grid_maximum;
	if (!std::isnan(params.maximum))
		highest = qMin(highest, params.maximum);
	auto const k1 = std::floor(highest / params.interval + 1e-9);
	first_level = k0 * params.interval;
	return qMax(0, int(k1 - k0) + 1);
}


ContourTracer::ContourTracer(const ElevationModel& grid, const ContourParams& params)
: grid(grid)
, params(params)
{
	auto const info = grid.info();
	num_levels = contourLevels(info.minimum, info.maximum, params, first_level);
}


QPointF ContourTracer::crossing(EdgeId edge, double level) const
{
	auto const cell_index = edge / 2;
	auto const col = int(cell_index % grid.width());
	auto const row = int(cell_index / grid.width());
	auto const vertical = (edge % 2) != 0;
	auto const za = double(grid.value(col, row));
	auto const zb = double(vertical ? grid.value(col, row + 1) : grid.value(col + 1, row));
	auto t = (zb == za) ? 0.5 : (level - za) / (zb - za);
	t = qBound(0.0, t, 1.0);
	auto const a = grid.cellCenter(col, row);
	auto const b = vertical ? grid.cellCenter(col, row + 1) : grid.cellCenter(col + 1, row);
	return a + t * (b - a);
}


void ContourTracer::traceTile(int c0, int c1, int r0, int r1, std::vector<std::vector<Chain>>& chains_by_level) const
{
	std::vector<std::vector<Segment>> segments_by_level{ std::size_t(num_levels) };
	for (int r = r0; r < r1; ++r)
	{
		for (int c = c0; c < c1; ++c)
		{
			auto const tl = double(grid.value(c, r)), tr = double(grid.value(c + 1, r));
			auto const bl = double(grid.value(c, r + 1)), br = double(grid.value(c + 1, r + 1));
			if (std::isnan(tl) || std::isnan(tr) || std::isnan(bl) || std::isnan(br))
				continue;
			auto const zmin = qMin(qMin(tl, tr), qMin(bl, br));
			auto const zmax = qMax(qMax(tl, tr), qMax(bl, br));
			auto const k_lo = qMax(0, int(std::floor((zmin - first_level) / params.interval)) + 1);
			auto const k_hi = qMin(num_levels - 1, int(std::floor((zmax - first_level) / params.interval + 1e-9)));
			if (k_lo > k_hi)
				continue;

			auto const top = horizontalEdge(c, r), bottom = horizontalEdge(c, r + 1);
			auto const left = verticalEdge(c, r), right = verticalEdge(c + 1, r);
			auto const center = 0.25 * (tl + tr + bl + br);
			for (int k = k_lo; k <= k_hi; ++k)
			{
				auto const l = level(k);
				if (!(zmin < l && l <= zmax))
					continue;
				auto const code = (tl >= l ? 8 : 0) | (tr >= l ? 4 : 0) | (br >= l ? 2 : 0) | (bl >= l ? 1 : 0);
				auto& segments = segments_by_level[std::size_t(k)];
				switch (code)
				{
				case 1:  case 14: segments.push_back({ left, bottom }); break;
				case 2:  case 13: segments.push_back({ bottom, right }); break;
				case 3:  case 12: segments.push_back({ left, right }); break;
				case 4:  case 11: segments.push_back({ top, right }); break;
				case 6:  case 9:  segments.push_back({ top, bottom }); break;
				case 7:  case 8:  segments.push_back({ left, top }); break;
				case 5:
					// TR and BL above. Saddle resolved with the cell-centre mean.
					if (center >= l)
					{
						segments.push_back({ left, top });
						segments.push_back({ bottom, right });
					}
					else
					{
						segments.push_back({ top, right });
						segments.push_back({ left, bottom });
					}
					break;
				case 10:
					// TL and BR above.
					if (center >= l)
					{
						segments.push_back({ top, right });
						segments.push_back({ left, bottom });
					}
					else
					{
						segments.push_back({ left, top });
						segments.push_back({ bottom, right });
					}
					break;
				default:
					break;
				}
			}
		}
	}

	for (int k = 0; k < num_levels; ++k)
	{
		auto chains = linkSegments(segments_by_level[std::size_t(k)]);
		auto& target = chains_by_level[std::size_t(k)];
		target.insert(target.end(), std::make_move_iterator(chains.begin()), std::make_move_iterator(chains.end()));
	}
}


// static
std::vector<Chain> ContourTracer::linkSegments(const std::vector<Segment>& segments)
{
	std::vector<Chain> chains;
	if (segments.empty())
		return chains;

	// Each edge carries at most two segments at one level.
	struct Slot { int first = -1; int second = -1; };
	std::unordered_map<EdgeId, Slot> by_edge;
	by_edge.reserve(2 * segments.size());
	for (std::size_t i = 0; i < segments.size(); ++i)
	{
		for (auto edge : { segments[i].a, segments[i].b })
		{
			auto& slot = by_edge[edge];
			if (slot.first < 0)
				slot.first = int(i);
			else
				slot.second = int(i);
		}
	}

	std::vector<bool> used(segments.size(), false);
	auto other_segment = [&by_edge](EdgeId edge, int segment) {
		auto const& slot = by_edge[edge];
		return slot.first == segment ? slot.second : (slot.second == segment ? slot.first : -1);
	};
	auto other_end = [&segments](int segment, EdgeId edge) {
		return segments[std::size_t(segment)].a == edge ? segments[std::size_t(segment)].b : segments[std::size_t(segment)].a;
	};
	auto extend = [&](std::vector<EdgeId>& edges, int segment, EdgeId from) {
		// Walk from the segment's end "from" onwards.
		auto current = segment;
		auto edge = from;
		while (true)
		{
			auto const next = other_segment(edge, current);
			if (next < 0 || used[std::size_t(next)])
				return false;
			used[std::size_t(next)] = true;
			edge = other_end(next, edge);
			if (edge == edges.front())
				return true;  // closed
			edges.push_back(edge);
			current = next;
		}
	};

	for (std::size_t i = 0; i < segments.size(); ++i)
	{
		if (used[i])
			continue;
		used[i] = true;
		Chain chain;
		std::vector<EdgeId> forward = { segments[i].a, segments[i].b };
		if (extend(forward, int(i), segments[i].b))
		{
			chain.edges = std::move(forward);
			chain.closed = true;
		}
		else
		{
			// Walk backwards from a as well: backward = b, a, e1, e2, ...
			std::vector<EdgeId> backward = { segments[i].b, segments[i].a };
			extend(backward, int(i), segments[i].a);
			// ..., e2, e1, then a, b, f1, f2, ...
			chain.edges.assign(backward.rbegin(), backward.rend() - 2);
			chain.edges.insert(chain.edges.end(), forward.begin(), forward.end());
		}
		chains.push_back(std::move(chain));
	}
	return chains;
}


void ContourTracer::joinChains(std::vector<Chain>& chains, double level) const
{
	// Open chains whose ends meet continue each other: first by edge, at tile
	// seams, then by position, where a line passes exactly through a lattice
	// point on the grid border and the pieces end on different edges.
	auto const scale = 1e6 / grid.cellSize();
	auto point_key = [this, level, scale](EdgeId edge) {
		auto const p = crossing(edge, level);
		return qint64(std::llround(p.x() * scale)) * 2000003LL + qint64(std::llround(p.y() * scale));
	};
	auto edge_key = [](EdgeId edge) { return edge; };
	
	auto join_by = [&chains](const std::function<qint64(EdgeId)>& key_of) {
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (auto& chain : chains)
			{
				if (!chain.closed && chain.edges.size() > 2 && key_of(chain.edges.front()) == key_of(chain.edges.back()))
				{
					chain.edges.pop_back();
					chain.closed = true;
				}
			}
			std::unordered_map<qint64, std::vector<std::size_t>> ends;
			for (std::size_t i = 0; i < chains.size(); ++i)
			{
				if (chains[i].closed || chains[i].edges.size() < 2)
					continue;
				ends[key_of(chains[i].edges.front())].push_back(i);
				ends[key_of(chains[i].edges.back())].push_back(i);
			}
			for (auto const& item : ends)
			{
				if (item.second.size() != 2 || item.second[0] == item.second[1])
					continue;
				auto& first = chains[item.second[0]];
				auto& second = chains[item.second[1]];
				if (key_of(first.edges.front()) == item.first)
					std::reverse(first.edges.begin(), first.edges.end());
				if (key_of(second.edges.back()) == item.first)
					std::reverse(second.edges.begin(), second.edges.end());
				// first ends at the meeting point, second starts there.
				auto const same_edge = first.edges.back() == second.edges.front();
				first.edges.insert(first.edges.end(), second.edges.begin() + (same_edge ? 1 : 0), second.edges.end());
				second.edges.clear();
				changed = true;
				break;
			}
			if (changed)
				chains.erase(std::remove_if(chains.begin(), chains.end(), [](const Chain& c) { return c.edges.empty(); }), chains.end());
		}
	};
	join_by(edge_key);
	join_by(point_key);
}


ContourLine ContourTracer::makeLine(const Chain& chain, int k) const
{
	ContourLine line;
	line.level = level(k);
	line.tag = contourLevelText(line.level);
	line.closed = chain.closed;
	auto edges = chain.edges;
	if (chain.closed && !edges.empty())
	{
		// Start at the smallest edge, so that tiled and untiled runs agree.
		auto const smallest = std::min_element(edges.begin(), edges.end());
		std::rotate(edges.begin(), smallest, edges.end());
	}
	line.points.reserve(edges.size());
	for (auto edge : edges)
		line.points.push_back(crossing(edge, line.level));

	auto const multiple = [](double value, double interval) {
		if (interval <= 0.0)
			return false;
		auto const ratio = value / interval;
		return qAbs(ratio - std::round(ratio)) < 1e-6;
	};
	if (multiple(line.level, params.index_interval))
		line.cls = ContourLine::Index;
	else if (multiple(line.level, params.main_interval))
		line.cls = ContourLine::Main;
	else
		line.cls = ContourLine::Smallest;

	orient(line);
	if (params.simplify_cells > 0.0)
		simplify(line.points, line.closed);
	return line;
}


void ContourTracer::orient(ContourLine& line) const
{
	auto& points = line.points;
	auto const n = points.size();
	if (n < 2)
		return;

	// Sample a quarter cell to the left of the longest segment's midpoint.
	std::size_t best = 0;
	double best_length = -1.0;
	auto const segments = line.closed ? n : n - 1;
	for (std::size_t i = 0; i < segments; ++i)
	{
		auto const d = points[(i + 1) % n] - points[i];
		auto const l = std::hypot(d.x(), d.y());
		if (l > best_length)
		{
			best_length = l;
			best = i;
		}
	}
	if (best_length <= 0.0)
		return;
	auto const p = points[best], q = points[(best + 1) % n];
	auto const d = (q - p) / best_length;
	auto const normal = QPointF(-d.y(), d.x());  // to the left in x-east, y-north coordinates
	auto const mid = 0.5 * (p + q);
	auto const offset = 0.25 * grid.cellSize();
	auto left = grid.at(mid + offset * normal);
	bool higher_on_left;
	if (!std::isnan(left))
	{
		higher_on_left = left >= line.level;
	}
	else
	{
		auto const right = grid.at(mid - offset * normal);
		if (std::isnan(right))
			return;
		higher_on_left = right < line.level;
	}
	if (!higher_on_left)
	{
		std::reverse(points.begin(), points.end());
		if (line.closed)
			std::rotate(points.begin(), points.end() - 1, points.end());  // keep the start point
	}
	if (line.closed)
	{
		double area = 0.0;
		for (std::size_t i = 0; i < n; ++i)
		{
			auto const& a = points[i];
			auto const& b = points[(i + 1) % n];
			area += a.x() * b.y() - b.x() * a.y();
		}
		line.depression = area < 0.0;
	}
}


namespace {

void douglasPeucker(const std::vector<QPointF>& points, std::size_t first, std::size_t last, double tolerance, std::vector<bool>& keep)
{
	if (last <= first + 1)
		return;
	auto const& a = points[first];
	auto const& b = points[last];
	auto const d = b - a;
	auto const length_sq = d.x() * d.x() + d.y() * d.y();
	double max_dist = -1.0;
	std::size_t index = first;
	for (std::size_t i = first + 1; i < last; ++i)
	{
		auto const& p = points[i];
		double dist;
		if (length_sq <= 0.0)
			dist = std::hypot(p.x() - a.x(), p.y() - a.y());
		else
			dist = qAbs(d.x() * (p.y() - a.y()) - d.y() * (p.x() - a.x())) / std::sqrt(length_sq);
		if (dist > max_dist)
		{
			max_dist = dist;
			index = i;
		}
	}
	if (max_dist > tolerance)
	{
		keep[index] = true;
		douglasPeucker(points, first, index, tolerance, keep);
		douglasPeucker(points, index, last, tolerance, keep);
	}
}

}  // namespace


void ContourTracer::simplify(std::vector<QPointF>& points, bool closed) const
{
	auto const n = points.size();
	if (n < 3)
		return;
	auto const tolerance = params.simplify_cells * grid.cellSize();
	std::vector<bool> keep(n, false);
	keep.front() = true;
	if (closed)
	{
		// Split at the point farthest from the first one.
		std::size_t far = 1;
		double far_dist = -1.0;
		for (std::size_t i = 1; i < n; ++i)
		{
			auto const dist = std::hypot(points[i].x() - points[0].x(), points[i].y() - points[0].y());
			if (dist > far_dist)
			{
				far_dist = dist;
				far = i;
			}
		}
		keep[far] = true;
		douglasPeucker(points, 0, far, tolerance, keep);
		// The second half closes back to the first point.
		std::vector<QPointF> second(points.begin() + std::ptrdiff_t(far), points.end());
		second.push_back(points.front());
		std::vector<bool> keep_second(second.size(), false);
		keep_second.front() = keep_second.back() = true;
		douglasPeucker(second, 0, second.size() - 1, tolerance, keep_second);
		for (std::size_t i = 1; i + 1 < second.size(); ++i)
			if (keep_second[i])
				keep[far + i] = true;
	}
	else
	{
		keep.back() = true;
		douglasPeucker(points, 0, n - 1, tolerance, keep);
	}
	std::vector<QPointF> result;
	result.reserve(n);
	for (std::size_t i = 0; i < n; ++i)
		if (keep[i])
			result.push_back(points[i]);
	if (result.size() >= (closed ? 3u : 2u))
		points = std::move(result);
}


std::vector<ContourLine> ContourTracer::run()
{
	std::vector<ContourLine> lines;
	if (num_levels <= 0 || grid.width() < 2 || grid.height() < 2)
		return lines;

	std::vector<std::vector<Chain>> chains_by_level{ std::size_t(num_levels) };
	auto const tile = qMax(1, params.tile_size);
	auto const cols = grid.width() - 1, rows = grid.height() - 1;  // marching squares
	for (int r0 = 0; r0 < rows; r0 += tile)
		for (int c0 = 0; c0 < cols; c0 += tile)
			traceTile(c0, qMin(c0 + tile, cols), r0, qMin(r0 + tile, rows), chains_by_level);

	for (int k = 0; k < num_levels; ++k)
	{
		auto& chains = chains_by_level[std::size_t(k)];
		joinChains(chains, level(k));
		for (auto const& chain : chains)
		{
			if (chain.edges.size() < 2)
				continue;
			auto line = makeLine(chain, k);
			// A level which touches a single lattice point gives a collapsed loop.
			double length = 0.0;
			for (std::size_t i = 1; i < line.points.size(); ++i)
				length += std::hypot(line.points[i].x() - line.points[i-1].x(), line.points[i].y() - line.points[i-1].y());
			if (line.points.size() < 2 || (line.closed && line.points.size() < 3) || length < 1e-9 * grid.cellSize())
				continue;
			lines.push_back(std::move(line));
		}
	}
	return lines;
}


const MapColor* contourColor(Map& map)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (color->getName().contains(QLatin1String("brown"), Qt::CaseInsensitive))
			return color;
	}
	// ISOM 2017-2 brown.
	auto* color = new MapColor(QStringLiteral("Brown"), 0);
	color->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
	map.addColor(color, 0);
	// What is drawn already refers to the colours by their place in the table.
	map.updateAllObjects();
	return color;
}

const LineSymbol* findContourSymbol(const Map& map, int number, int variant)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Line
		    && symbol->getNumberComponent(0) == number
		    && symbol->getNumberComponent(1) == variant)
			return symbol->asLine();
	}
	return nullptr;
}

/// foom: A symbol number without its trailing ".0": 101.000 of an OCD file is "101.0" in the map read from it and "101" in FOOM's sets.
QString plainSymbolNumber(QString number)
{
	while (number.endsWith(QLatin1String(".0")))
		number.chop(2);
	return number;
}

/**
 * foom: The map's line symbol of the number: the one whose number is this text,
 * or else the one whose number is the same without trailing ".0".
 * When there is none, other is set if a symbol of another kind has the number.
 */
const LineSymbol* findSymbolByNumber(const Map& map, const QString& number, bool* other = nullptr)
{
	const LineSymbol* loose = nullptr;
	auto const plain = plainSymbolNumber(number);
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		auto const symbol_number = symbol->getNumberAsString();
		auto const exact = symbol_number == number;
		if (!exact && plainSymbolNumber(symbol_number) != plain)
			continue;
		if (symbol->getType() != Symbol::Line)
		{
			if (other)
				*other = true;
			continue;
		}
		if (exact)
			return symbol->asLine();
		if (!loose)
			loose = symbol->asLine();
	}
	return loose;
}

struct OwnContourClass
{
	const char* name;
	double width;
	bool dashed;
};
OwnContourClass const own_contour_classes[3] = {
    { "Form line", 0.1, true },
    { "Contour", 0.14, false },
    { "Index contour", 0.25, false } };

const LineSymbol* makeContourSymbol(Map& map, int number, int variant, const QString& name, double width_mm, bool dashed)
{
	auto* symbol = new LineSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setNumberComponent(1, variant);
	symbol->setNumberComponent(2, -1);
	symbol->setName(name);
	symbol->setColor(contourColor(map));
	symbol->setLineWidth(width_mm);
	symbol->setCapStyle(LineSymbol::RoundCap);
	symbol->setJoinStyle(LineSymbol::RoundJoin);
	if (dashed)
	{
		symbol->setDashed(true);
		symbol->setDashLength(2000);
		symbol->setBreakLength(250);
	}
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

/// foom: FOOM's own symbol of a class, found or made; variant 1 is the depression's.
const LineSymbol* ownContourSymbol(Map& map, bool smoothed, int cls, int variant)
{
	auto const number = (smoothed ? 10060 : 10050) + cls;
	if (auto const* symbol = findContourSymbol(map, number, variant))
		return symbol;
	QString name = QLatin1String(own_contour_classes[cls].name);
	if (variant == 1)
		name += QLatin1String(", depression");
	if (smoothed)
		name += QLatin1String(", smoothed");
	return makeContourSymbol(map, number, variant, name, own_contour_classes[cls].width, own_contour_classes[cls].dashed);
}

}  // namespace



QString contourLevelText(double level)
{
	auto text = QString::number(level, 'f', 3);
	while (text.endsWith(QLatin1Char('0')))
		text.chop(1);
	if (text.endsWith(QLatin1Char('.')))
		text.chop(1);
	if (text == QLatin1String("-0"))
		text = QStringLiteral("0");
	return text;
}


int contourLevelCount(double grid_minimum, double grid_maximum, const ContourParams& params)
{
	double first = 0.0;
	return contourLevels(grid_minimum, grid_maximum, params, first);
}


std::vector<ContourLine> traceContours(const ElevationModel& grid, const ContourParams& params)
{
	ContourTracer tracer(grid, params);
	return tracer.run();
}


const MapColor* contourBrown(Map& map)
{
	return contourColor(map);
}


ContourSymbols contourSymbols(Map& map, bool smoothed)
{
	ContourSymbols symbols;
	for (int cls = 0; cls < 3; ++cls)
	{
		symbols.hill[cls] = ownContourSymbol(map, smoothed, cls, 0);
		symbols.depression[cls] = ownContourSymbol(map, smoothed, cls, 1);
	}
	return symbols;
}


QString contourSymbolsError(const Map& map, const ContourParams& params)
{
	for (auto const* numbers : { params.symbols.hill, params.symbols.depression })
	{
		for (int cls = 0; cls < 3; ++cls)
		{
			auto const& number = numbers[cls];
			auto other = false;
			if (number.isEmpty() || findSymbolByNumber(map, number, &other))
				continue;
			return other ? ElevationModel::tr("The symbol %1 is not a line symbol.").arg(number)
			             : ElevationModel::tr("The map has no line symbol %1.").arg(number);
		}
	}
	return {};
}


ContourSymbols contourSymbols(Map& map, const ContourParams& params, QString* error)
{
	auto const message = contourSymbolsError(map, params);
	if (!message.isEmpty())
	{
		if (error)
			*error = message;
		return {};
	}
	ContourSymbols symbols;
	for (int cls = 0; cls < 3; ++cls)
	{
		auto const& hill = params.symbols.hill[cls];
		auto const& depression = params.symbols.depression[cls];
		symbols.hill[cls] = hill.isEmpty() ? ownContourSymbol(map, params.smoothed, cls, 0) : findSymbolByNumber(map, hill);
		if (!depression.isEmpty())
			symbols.depression[cls] = findSymbolByNumber(map, depression);
		else if (!hill.isEmpty())
			symbols.depression[cls] = symbols.hill[cls];
		else
			symbols.depression[cls] = ownContourSymbol(map, params.smoothed, cls, 1);
	}
	return symbols;
}


ContourResult makeContourObjects(const Map& map, const ElevationModel& grid, const ContourParams& params, const ContourSymbols& symbols)
{
	ContourResult result;
	for (int cls = 0; cls < 3; ++cls)
	{
		if (!symbols.hill[cls] || (params.depression_symbols && !symbols.depression[cls]))
		{
			result.error = ElevationModel::tr("Contour symbols are missing");
			return result;
		}
	}

	std::unique_ptr<ElevationModel> working;
	if (params.smoothed)
		working = grid.smoothed(1);
	auto const lines = traceContours(params.smoothed ? *working : grid, params);
	auto const& georef = map.getGeoreferencing();

	for (auto const& line : lines)
	{
		if (line.points.size() < 2)
			continue;
		auto const* symbol = (line.depression && params.depression_symbols) ? symbols.depression[line.cls] : symbols.hill[line.cls];
		MapCoordVector coords;
		coords.reserve(line.points.size() + 1);
		for (auto const& point : line.points)
		{
			auto const map_coord = georef.toMapCoordF(point);
			coords.emplace_back(map_coord.x(), map_coord.y());
		}
		auto object = std::make_unique<PathObject>(symbol, std::move(coords));
		if (line.closed)
			object->closeAllParts();
		object->setTag(QStringLiteral("ele"), line.tag);
		if (line.depression)
			object->setTag(QStringLiteral("depression"), QStringLiteral("yes"));

		if (params.smoothed)
		{
			ops::SmoothParams smooth_params;
			smooth_params.tolerance = params.smooth_tolerance;
			auto smoothed = ops::smoothPaths(map, { object.get() }, smooth_params);
			if (smoothed.error.isEmpty() && smoothed.out.size() == 1 && smoothed.out.front()->getType() == Object::Path)
			{
				object.reset(smoothed.out.front().release()->asPath());
				object->setSymbol(symbol, true);
				object->setTag(QStringLiteral("ele"), line.tag);
				if (line.depression)
					object->setTag(QStringLiteral("depression"), QStringLiteral("yes"));
			}
			if (params.curves)
				object->convertToCurves();
		}
		result.objects.push_back(std::move(object));
	}
	return result;
}


bool addContours(Map& map, std::vector<std::unique_ptr<Object>>&& objects)
{
	if (objects.empty())
		return false;
	auto* part = map.getCurrentPart();
	map.clearObjectSelection(false);
	auto delete_step = std::unique_ptr<DeleteObjectsUndoStep>(new DeleteObjectsUndoStep(&map));
	std::vector<Object*> added;
	added.reserve(objects.size());
	for (auto& object : objects)
	{
		auto* output = object.release();
		map.addObject(output);
		map.addObjectToSelection(output, false);
		added.push_back(output);
	}
	for (auto* output : added)
		delete_step->addObject(part->findObjectIndex(output));
	map.push(delete_step.release());
	map.setObjectsDirty();
	map.emitSelectionChanged();
	map.emitSelectionEdited();
	return true;
}


}  // namespace OpenOrienteering
