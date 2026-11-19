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


#ifndef OPENORIENTEERING_TILE_PYRAMID_H
#define OPENORIENTEERING_TILE_PYRAMID_H

#include <vector>

#include <QtGlobal>
#include <QMetaType>
#include <QPointF>
#include <QRectF>

namespace OpenOrienteering {


/**
 * The key of a tile in a TileServiceTemplate's own pyramid.
 *
 * Level k has ground resolution r_0 * 2^k. Tile (k, column, row) covers
 * 512 pixels times that resolution on each side, with its lower left corner
 * at (column * extent, row * extent) in the map's projected coordinates.
 *
 * See the design notes, "Own tile pyramid".
 */
struct TileKey
{
	int level = 0;
	qint64 column = 0;
	qint64 row = 0;
};

constexpr bool operator==(const TileKey& lhs, const TileKey& rhs) noexcept
{
	return lhs.level == rhs.level && lhs.column == rhs.column && lhs.row == rhs.row;
}

constexpr bool operator!=(const TileKey& lhs, const TileKey& rhs) noexcept
{
	return !(lhs == rhs);
}

inline uint qHash(const TileKey& key, uint seed = 0) noexcept
{
	return ::qHash(quint64(key.column) * 73856093u ^ quint64(key.row) * 19349663u ^ quint64(key.level) * 83492791u, seed);
}


/**
 * The tile mathematics of the pyramid, as pure functions.
 *
 * Rectangles are in projected coordinates with the convention that left()
 * and top() are the minimum x and y, right() and bottom() the maximum,
 * i.e. top() is the southern edge.
 */
namespace TilePyramid {

constexpr int tile_size = 512;

/** The ground resolution of a level. */
constexpr double levelResolution(double r0, int level) noexcept
{
	return r0 * double(qint64(1) << level);
}

/** The side of a tile at a level, in projected units. */
constexpr double tileExtent(double r0, int level) noexcept
{
	return tile_size * levelResolution(r0, level);
}

/**
 * The level for painting at the given ground resolution per device pixel:
 * the coarsest level whose resolution is not above it, so that tiles are
 * never upscaled by more than a factor of two; level 0 when even that is
 * finer than the paint.
 */
int levelForResolution(double r0, double resolution) noexcept;

/** The rectangle of a tile in projected coordinates. */
QRectF tileRect(double r0, const TileKey& key) noexcept;

/** The tile of a level which contains a projected point. */
TileKey tileAt(double r0, int level, const QPointF& projected) noexcept;

/** The tiles of a level which intersect a projected rectangle, by row and then by column. */
std::vector<TileKey> tilesIntersecting(double r0, int level, const QRectF& projected_rect);

/** The tile of a coarser level which contains the given tile. */
TileKey ancestor(const TileKey& key, int coarser_level) noexcept;

}  // namespace TilePyramid


}  // namespace OpenOrienteering

Q_DECLARE_METATYPE(OpenOrienteering::TileKey)

#endif
