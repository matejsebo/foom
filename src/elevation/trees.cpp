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

#include "trees.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <QtGlobal>
#include <QLatin1String>
#include <QString>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "elevation/cliffs.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

namespace {

/// Where the parabola through three values along an axis peaks, relative to the middle one, at most half a cell.
double peakOffset(double before, double middle, double after)
{
	auto const curvature = before - 2 * middle + after;
	if (!(curvature < 0.0))
		return 0.0;
	return qBound(-0.5, (before - after) / (2 * curvature), 0.5);
}

const MapColor* greenColor(Map& map)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (color->getName().compare(QLatin1String("green"), Qt::CaseInsensitive) == 0)
			return color;
	}
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (color->getName().contains(QLatin1String("green"), Qt::CaseInsensitive))
			return color;
	}
	// ISOM 2017-2 green, 100%.
	auto* color = new MapColor(QStringLiteral("Green"), 0);
	color->setCmyk(MapColorCmyk(0.76f, 0.0f, 0.91f, 0.0f));
	map.addColor(color, 0);
	// What is drawn already refers to the colours by their place in the table.
	map.updateAllObjects();
	return color;
}

}  // namespace



std::vector<Tree> findTrees(const ElevationModel& heights, const TreeParams& params)
{
	// the design notes, "Trees".
	struct Candidate
	{
		QPointF position;
		double height;
	};
	std::vector<Candidate> candidates;
	auto const filtered = gaussianFiltered(heights, 2);
	auto const& g = *filtered;
	auto const cell = g.cellSize();
	for (int row = 1; row + 1 < g.height(); ++row)
	{
		for (int col = 1; col + 1 < g.width(); ++col)
		{
			auto const value = double(g.value(col, row));
			if (std::isnan(value) || value < params.min_height)
				continue;
			auto highest = true;
			for (int dr = -1; dr <= 1 && highest; ++dr)
			{
				for (int dc = -1; dc <= 1; ++dc)
				{
					auto const neighbour = g.value(col + dc, row + dr);
					if (std::isnan(neighbour) || neighbour > value)
					{
						highest = false;
						break;
					}
				}
			}
			if (!highest)
				continue;
			auto const dx = peakOffset(g.value(col - 1, row), value, g.value(col + 1, row));
			auto const dy = peakOffset(g.value(col, row - 1), value, g.value(col, row + 1));
			auto const centre = g.cellCenter(col, row);
			candidates.push_back({ QPointF(centre.x() + dx * cell, centre.y() - dy * cell), value });
		}
	}
	
	// Candidates closer than the merge distance, directly or through others, are one tree.
	std::vector<int> group(candidates.size(), -1);
	std::vector<Tree> trees;
	auto const limit = params.merge_distance * params.merge_distance;
	for (std::size_t first = 0; first < candidates.size(); ++first)
	{
		if (group[first] >= 0)
			continue;
		auto const label = int(trees.size());
		std::vector<std::size_t> members = { first };
		group[first] = label;
		for (std::size_t k = 0; k < members.size(); ++k)
		{
			auto const& p = candidates[members[k]].position;
			for (std::size_t other = 0; other < candidates.size(); ++other)
			{
				if (group[other] >= 0)
					continue;
				auto const& q = candidates[other].position;
				auto const dx = p.x() - q.x();
				auto const dy = p.y() - q.y();
				if (dx * dx + dy * dy < limit)
				{
					group[other] = label;
					members.push_back(other);
				}
			}
		}
		Tree tree;
		double x = 0.0, y = 0.0;
		for (auto const m : members)
		{
			x += candidates[m].position.x();
			y += candidates[m].position.y();
			tree.height = std::max(tree.height, candidates[m].height);
		}
		tree.projected = QPointF(x / members.size(), y / members.size());
		trees.push_back(tree);
	}
	return trees;
}


const PointSymbol* treeSymbol(Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Point
		    && symbol->getNumberComponent(0) == 418
		    && symbol->getNumberComponent(1) <= 0)
			return symbol->asPoint();
	}
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, 418);
	symbol->setNumberComponent(1, -1);
	symbol->setName(QStringLiteral("Prominent bush or tree"));
	symbol->setInnerRadius(400);
	symbol->setInnerColor(greenColor(map));
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}


std::vector<std::unique_ptr<Object>> makeTreeObjects(const Map& map, const std::vector<Tree>& trees, const PointSymbol* symbol)
{
	std::vector<std::unique_ptr<Object>> objects;
	if (!symbol)
		return objects;
	auto const& georef = map.getGeoreferencing();
	for (auto const& tree : trees)
	{
		auto object = std::make_unique<PointObject>(symbol);
		object->setPosition(georef.toMapCoordF(tree.projected));
		object->setTag(QStringLiteral("feature"), QStringLiteral("tree"));
		object->setTag(QStringLiteral("height"), QString::number(tree.height, 'f', 2));
		objects.push_back(std::move(object));
	}
	return objects;
}


}  // namespace OpenOrienteering
