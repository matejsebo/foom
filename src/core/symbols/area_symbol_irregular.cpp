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


#include "area_symbol.h"

#include <cmath>
#include <cstdint>

#include <QtGlobal>

#include "core/map_coord.h"
#include "core/renderables/renderable.h"
#include "core/renderables/renderable_implementation.h"
#include "core/symbols/point_symbol.h"


// Irregular area pattern, see the design notes, "Area symbol properties".

namespace OpenOrienteering {

namespace {

/** A fixed hash of the cell and the pattern geometry, the same in every session. */
quint64 cellHash(qint64 row, qint64 column, int interval, int spacing, int angle_key, int line_offset, int along_offset)
{
	auto h = quint64(0x9E3779B97F4A7C15ULL);
	for (auto value : { quint64(row), quint64(column), quint64(interval), quint64(spacing), quint64(angle_key), quint64(line_offset), quint64(along_offset) })
	{
		h ^= value + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
		h *= 0xBF58476D1CE4E5B9ULL;
		h ^= h >> 31;
	}
	return h;
}

}  // namespace


void AreaSymbol::FillPattern::createIrregularPointPatternLine(
        const MapCoordF& first,
        const MapCoordF& direction,
        qreal start_length,
        qreal length,
        qreal step_length,
        qreal rotation,
        const AreaRenderable& outline,
        ObjectRenderables& output ) const
{
	auto const normal = direction.perpRight();
	auto const spacing = 0.001 * line_spacing;
	auto const along_offset = 0.001 * offset_along_line;
	// No element is left out, and an element moves by at most the
	// variation's share of half a cell, and by no more than half of what its size and the minimum distance
	// leave of the way to the next element and to the next row. Two elements so stay apart by the distance
	// whatever the two of them do, and an element's place depends on its own cell only.
	auto const reserve = element_size + 0.001 * minimum_distance;
	auto const along_room = ((neighbour_along > 0.0 ? neighbour_along : step_length) - reserve) / 2;
	auto const across_room = ((neighbour_across > 0.0 ? neighbour_across : spacing) - reserve) / 2;
	auto const dx = 2 * qMax(0.0, qMin(qBound(0, variation_x, 100) / 100.0 * step_length / 2, along_room));
	auto const dy = 2 * qMax(0.0, qMin(qBound(0, variation_y, 100) / 100.0 * spacing / 2, across_room));
	auto const angle_key = qRound(1000.0 * angle);
	auto const clipping = flags & Option::AlternativeToClipping;
	
	for (auto cur = start_length; cur < length; cur += step_length)
	{
		auto const node = first + direction * cur;
		auto const row = qint64(std::lround((MapCoordF::dotProduct(normal, node) - 0.001 * line_offset) / spacing));
		auto const column = qint64(std::lround((MapCoordF::dotProduct(direction, node) - along_offset) / step_length));
		
		auto const hash = cellHash(row, column, point_distance, line_spacing, angle_key, line_offset, offset_along_line);
		auto const u = double(hash & 0xffff) / 65535.0 - 0.5;
		auto const v = double((hash >> 16) & 0xffff) / 65535.0 - 0.5;
		auto const coord = node + direction * (u * dx) + normal * (v * dy);
		
		switch (clipping)
		{
		case Option::NoClippingIfCenterInside:
			point->createRenderablesIfCenterInside(coord, -rotation, outline.painterPath(), output);
			break;
		case Option::NoClippingIfCompletelyInside:
			point->createRenderablesIfCompletelyInside(coord, -rotation, outline.painterPath(), output);
			break;
		case Option::NoClippingIfPartiallyInside:
			point->createRenderablesIfPartiallyInside(coord, -rotation, outline.painterPath(), output);
			break;
		default:
			point->createRenderablesScaled(coord, -rotation, output);
			break;
		}
	}
}


}  // namespace OpenOrienteering
