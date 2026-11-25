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

#include "landforms.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QString>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "elevation/cliffs.h"
#include "elevation/contours.h"
#include "elevation/elevation_georef.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

namespace {

const PointSymbol* findPointSymbol(const Map& map, int number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Point
		    && symbol->getNumberComponent(0) == number
		    && symbol->getNumberComponent(1) <= 0)
			return symbol->asPoint();
	}
	return nullptr;
}

}  // namespace



namespace {

/**
 * The prominence of the summit at (col, row) of the grid g times sign (1 for a
 * knoll, -1 for a depression, of which it is the depth to the spill point):
 * the region about it is grown downwards, highest cell first, until a cell
 * higher than the summit is met; the summit less the lowest level reached is
 * the prominence. Growing stops at the edge of the grid, at a void cell, and
 * when the region passes max_cells; what was reached by then is a lower bound.
 *
 * Returns the prominence reached while the region was no larger than max_cells.
 */
double prominenceAt(const ElevationModel& g, int col, int row, double sign, int max_cells)
{
	struct Cell { double level; int col; int row; };
	auto const lower = [](const Cell& a, const Cell& b) { return a.level < b.level; };
	std::priority_queue<Cell, std::vector<Cell>, decltype(lower)> open(lower);
	std::unordered_set<qint64> seen;
	auto const width = g.width();
	auto const height = g.height();
	auto const key = [width](int c, int r) { return qint64(r) * width + c; };
	auto const top = sign * double(g.value(col, row));
	auto low = top;
	auto cells = 0;
	open.push({ top, col, row });
	seen.insert(key(col, row));
	while (!open.empty())
	{
		auto const cell = open.top();
		open.pop();
		if (cell.level > top)
			break;              // higher ground: the lowest level so far is the saddle (the spill point)
		if (++cells > max_cells)
			break;              // too wide for a point feature
		low = std::min(low, cell.level);
		for (int dr = -1; dr <= 1; ++dr)
		{
			for (int dc = -1; dc <= 1; ++dc)
			{
				auto const c = cell.col + dc, r = cell.row + dr;
				if ((dc == 0 && dr == 0) || seen.count(key(c, r)))
					continue;
				if (c < 0 || r < 0 || c >= width || r >= height || std::isnan(g.value(c, r)))
					return top - low;   // the edge of what is known
				seen.insert(key(c, r));
				open.push({ sign * double(g.value(c, r)), c, r });
			}
		}
	}
	return top - low;
}

}  // namespace


std::vector<Landform> findLandforms(const ElevationModel& grid, const LandformParams& params)
{
	// the design notes, "Knolls and small depressions by prominence".
	std::vector<Landform> found;
	if (!params.knolls && !params.depressions)
		return found;
	auto const smoothed = gaussianFiltered(grid);
	auto const& g = *smoothed;
	auto const width = g.width();
	auto const height = g.height();
	auto const max_cells = std::max(9, int(std::lround(params.max_area / (g.cellSize() * g.cellSize()))));
	
	for (int row = 1; row < height - 1; ++row)
	{
		for (int col = 1; col < width - 1; ++col)
		{
			auto const centre = g.value(col, row);
			if (std::isnan(centre))
				continue;
			auto higher = true;
			auto lower = true;
			for (int dr = -1; dr <= 1 && (higher || lower); ++dr)
			{
				for (int dc = -1; dc <= 1; ++dc)
				{
					if (dr == 0 && dc == 0)
						continue;
					auto const neighbour = g.value(col + dc, row + dr);
					if (std::isnan(neighbour) || !(centre > neighbour))
						higher = false;
					if (std::isnan(neighbour) || !(centre < neighbour))
						lower = false;
				}
			}
			if (!(higher && params.knolls) && !(lower && params.depressions))
				continue;
			Landform landform;
			landform.projected = g.cellCenter(col, row);
			landform.kind = higher ? Landform::Knoll : Landform::Depression;
			landform.size = prominenceAt(g, col, row, higher ? 1.0 : -1.0, max_cells);
			if (landform.size + 1e-6 < (higher ? params.min_height : params.min_depth))
				continue;
			found.push_back(landform);
		}
	}
	return found;
}


LandformSymbols landformSymbols(Map& map)
{
	LandformSymbols symbols;
	symbols.knoll = findPointSymbol(map, 109);
	symbols.depression = findPointSymbol(map, 111);
	if (!symbols.knoll)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, 109);
		symbol->setNumberComponent(1, -1);
		symbol->setName(QStringLiteral("Small knoll"));
		symbol->setInnerRadius(400);
		symbol->setInnerColor(contourBrown(map));
		map.addSymbol(symbol, map.getNumSymbols());
		symbols.knoll = symbol;
	}
	if (!symbols.depression)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, 111);
		symbol->setNumberComponent(1, -1);
		symbol->setName(QStringLiteral("Small depression"));
		symbol->setInnerRadius(220);
		symbol->setOuterWidth(180);
		symbol->setOuterColor(contourBrown(map));
		map.addSymbol(symbol, map.getNumSymbols());
		symbols.depression = symbol;
	}
	return symbols;
}


std::vector<std::unique_ptr<Object>> makeLandformObjects(const Map& map, const std::vector<Landform>& landforms, const LandformSymbols& symbols)
{
	std::vector<std::unique_ptr<Object>> objects;
	auto const georef = elevationGeoreferencing(map);  // in metres also for a map which counts in feet
	for (auto const& landform : landforms)
	{
		auto const* symbol = landform.kind == Landform::Knoll ? symbols.knoll : symbols.depression;
		if (!symbol)
			continue;
		auto object = std::make_unique<PointObject>(symbol);
		object->setPosition(georef.toMapCoordF(landform.projected));
		object->setTag(QStringLiteral("feature"), landform.kind == Landform::Knoll ? QStringLiteral("knoll") : QStringLiteral("depression"));
		object->setTag(QStringLiteral("size"), QString::number(landform.size, 'f', 2));
		objects.push_back(std::move(object));
	}
	return objects;
}


}  // namespace OpenOrienteering
