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
#include <utility>

#include <QtGlobal>
#include <QString>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "elevation/contours.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

namespace {

/// Places on the ring around a cell.
constexpr int ring_samples = 64;

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



std::vector<Landform> findLandforms(const ElevationModel& grid, const LandformParams& params)
{
	// the design notes, "Knolls and small depressions".
	std::vector<Landform> found;
	if (!params.knolls && !params.depressions)
		return found;
	auto const smoothed = grid.smoothed(1);
	auto const& g = *smoothed;
	auto const width = g.width();
	auto const height = g.height();
	// At least the next cell.
	auto const radius = std::max(1.0, params.ring_m / g.cellSize());
	auto const reach = int(std::ceil(radius));
	
	// The ring as offsets of cells.
	std::vector<std::pair<int, int>> ring;
	auto const turn = 8 * std::atan(1.0);
	for (int k = 0; k < ring_samples; ++k)
	{
		auto const angle = turn * k / ring_samples;
		ring.emplace_back(int(std::lround(radius * std::cos(angle))), int(std::lround(radius * std::sin(angle))));
	}
	
	for (int row = reach; row < height - reach; ++row)
	{
		for (int col = reach; col < width - reach; ++col)
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
			auto lowest = double(centre);
			auto highest = double(centre);
			auto known = true;
			for (auto const& offset : ring)
			{
				auto const value = g.value(col + offset.first, row + offset.second);
				if (std::isnan(value))
				{
					known = false;
					break;
				}
				lowest = std::min(lowest, double(value));
				highest = std::max(highest, double(value));
			}
			if (!known)
				continue;
			Landform landform;
			landform.projected = g.cellCenter(col, row);
			if (higher)
			{
				landform.kind = Landform::Knoll;
				landform.size = centre - lowest;
				if (landform.size + 1e-6 < params.min_height)
					continue;
			}
			else
			{
				landform.kind = Landform::Depression;
				landform.size = highest - centre;
				if (landform.size + 1e-6 < params.min_depth)
					continue;
			}
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
	auto const& georef = map.getGeoreferencing();
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
