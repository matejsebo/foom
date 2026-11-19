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


#include "tile_pyramid.h"

#include <cmath>

namespace OpenOrienteering {

namespace TilePyramid {

namespace {

constexpr int max_level = 40;

qint64 floorDiv(qint64 value, int shift) noexcept
{
	// Arithmetic shift of a negative value rounds towards minus infinity on
	// the compilers used here, but spell it out.
	auto const divisor = qint64(1) << shift;
	auto q = value / divisor;
	if ((value % divisor) < 0)
		--q;
	return q;
}

}  // namespace


int levelForResolution(double r0, double resolution) noexcept
{
	if (!(r0 > 0) || !(resolution > r0))
		return 0;
	auto const level = int(std::floor(std::log2(resolution / r0)));
	return qBound(0, level, max_level);
}

QRectF tileRect(double r0, const TileKey& key) noexcept
{
	auto const extent = tileExtent(r0, key.level);
	return { key.column * extent, key.row * extent, extent, extent };
}

TileKey tileAt(double r0, int level, const QPointF& projected) noexcept
{
	auto const extent = tileExtent(r0, level);
	return { level, qint64(std::floor(projected.x() / extent)), qint64(std::floor(projected.y() / extent)) };
}

std::vector<TileKey> tilesIntersecting(double r0, int level, const QRectF& projected_rect)
{
	std::vector<TileKey> result;
	if (!(r0 > 0) || projected_rect.isEmpty())
		return result;

	auto const extent = tileExtent(r0, level);
	// Open intervals: a tile which merely touches the rectangle is not included.
	auto const first_column = qint64(std::floor(projected_rect.left() / extent));
	auto const last_column = qint64(std::ceil(projected_rect.right() / extent)) - 1;
	auto const first_row = qint64(std::floor(projected_rect.top() / extent));
	auto const last_row = qint64(std::ceil(projected_rect.bottom() / extent)) - 1;
	if (last_column < first_column || last_row < first_row)
		return result;

	result.reserve(std::size_t((last_column - first_column + 1) * (last_row - first_row + 1)));
	for (auto row = first_row; row <= last_row; ++row)
		for (auto column = first_column; column <= last_column; ++column)
			result.push_back({ level, column, row });
	return result;
}

TileKey ancestor(const TileKey& key, int coarser_level) noexcept
{
	if (coarser_level <= key.level)
		return key;
	auto const shift = coarser_level - key.level;
	return { coarser_level, floorDiv(key.column, shift), floorDiv(key.row, shift) };
}

}  // namespace TilePyramid

}  // namespace OpenOrienteering
