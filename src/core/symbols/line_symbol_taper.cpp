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


#include "line_symbol.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"
#include "core/path_coord.h"
#include "core/renderables/renderable.h"
#include "core/renderables/renderable_implementation.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/virtual_coord_vector.h"
#include "core/virtual_path.h"


// Taper, secondary mid symbol: see the design notes, "Line symbol properties".

namespace OpenOrienteering {

double LineSymbol::taperScale(double fraction) const
{
	auto const p = qBound(0.0, taper_end_percent / 100.0, 1.0);
	auto const f = qBound(0.0, fraction, 1.0);
	switch (taper_mode)
	{
	case TaperTowardEnd:
		return 1.0 - (1.0 - p) * f;
	case TaperTowardStart:
		return p + (1.0 - p) * f;
	case TaperBothEnds:
		return p + (1.0 - p) * (1.0 - std::abs(2.0 * f - 1.0));
	default:
		return 1.0;
	}
}


void LineSymbol::placeMidSymbol(const SplitPathCoord& split, qreal orientation, ObjectRenderables& /*output*/) const
{
	pending_mid_symbols.emplace_back(split, orientation);
}


void LineSymbol::flushMidSymbols(const VirtualPath& /*path*/, ObjectRenderables& output) const
{
	auto spots = std::move(pending_mid_symbols);
	pending_mid_symbols.clear();
	auto const n = spots.size();
	if (n == 0 || !mid_symbol)
		return;
	
	std::vector<double> scale(n, 1.0);
	auto const tapered = taper_mode != NoTaper && n > 1;
	if (tapered)
	{
		for (std::size_t k = 0; k < n; ++k)
			scale[k] = taperScale(double(k) / double(n - 1));
	}
	
	// With the spacing flag, the gaps follow the scales; the first and the last placement stay.
	std::vector<bool> moved(n, false);
	if (tapered && taper_spacing && !dashed)
	{
		auto total = 0.0;
		for (std::size_t k = 0; k + 1 < n; ++k)
			total += (scale[k] + scale[k + 1]) / 2;
		auto const first_clen = spots.front().first.clen;
		auto const span = spots.back().first.clen - first_clen;
		if (total > 0.0 && span > 0.0)
		{
			auto accumulated = 0.0;
			for (std::size_t k = 1; k + 1 < n; ++k)
			{
				accumulated += (scale[k - 1] + scale[k]) / 2;
				auto const clen = PathCoord::length_type(first_clen + span * accumulated / total);
				spots[k].first = SplitPathCoord::at(clen, spots[k - 1].first);
				moved[k] = true;
			}
		}
	}
	
	auto const per_spot = std::size_t(qMax(1, mid_symbols_per_spot));
	auto const secondary = (mid_symbol_2 && !mid_symbol_2->isEmpty()) ? mid_symbol_2 : nullptr;
	for (std::size_t k = 0; k < n; ++k)
	{
		auto const* symbol = (secondary && (k / per_spot) % 2 == 1) ? secondary : mid_symbol;
		auto orientation = spots[k].second;
		if (moved[k] && symbol->isRotatable())
			orientation = spots[k].first.tangentVector().angle();
		if (std::abs(scale[k] - 1.0) < 1e-9)
		{
			symbol->createRenderablesScaled(spots[k].first.pos, orientation, output);
		}
		else
		{
			// The renderables copy what they need, so a scaled temporary copy is safe.
			std::unique_ptr<PointSymbol> scaled(static_cast<PointSymbol*>(Symbol::duplicate(*symbol).release()));
			scaled->scale(scale[k]);
			scaled->createRenderablesScaled(spots[k].first.pos, orientation, output);
		}
	}
}


void LineSymbol::createTaperedLineRenderables(const VirtualPath& path, ObjectRenderables& output) const
{
	auto const& path_coords = path.path_coords;
	auto const count = path_coords.size();
	if (count < 2 || !color || line_width <= 0)
		return;
	auto const total = double(path_coords.back().clen);
	if (!(total > 0.0))
		return;
	
	// Offset the flattened path on both sides by the local half width.
	MapCoordVectorF left;
	MapCoordVectorF right;
	left.reserve(count);
	right.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		auto const& previous = path_coords[i == 0 ? 0 : i - 1].pos;
		auto const& next = path_coords[i + 1 < count ? i + 1 : count - 1].pos;
		auto tangent = next - previous;
		if (tangent.lengthSquared() <= 0.0)
			continue;
		tangent.normalize();
		auto const half_width = 0.5 * 0.001 * line_width * taperScale(double(path_coords[i].clen) / total);
		auto const offset = tangent.perpRight() * half_width;
		left.push_back(path_coords[i].pos + offset);
		right.push_back(path_coords[i].pos - offset);
	}
	if (left.size() < 2)
		return;
	
	MapCoordVectorF polygon_coords;
	polygon_coords.reserve(left.size() + right.size());
	polygon_coords.insert(polygon_coords.end(), left.begin(), left.end());
	polygon_coords.insert(polygon_coords.end(), right.rbegin(), right.rend());
	MapCoordVector polygon_flags(polygon_coords.size());
	polygon_flags.back().setHolePoint(true);
	VirtualPath polygon = { polygon_flags, polygon_coords };
	polygon.path_coords.update(0);
	
	AreaSymbol area_symbol;
	area_symbol.setColor(color);
	output.insertRenderable(new AreaRenderable(&area_symbol, polygon));
}


}  // namespace OpenOrienteering
