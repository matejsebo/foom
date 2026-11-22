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


#ifndef FOOM_ROUTE_TERRAIN_H
#define FOOM_ROUTE_TERRAIN_H

#include <utility>
#include <vector>

#include <QtGlobal>
#include <QPolygonF>
#include <QByteArray>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"
#include "course/route_settings.h"

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * What the route analysis runs on: the runnability of the symbols, and
 * the rasters of runnability and impassability of the terrain.
 *
 * See the design notes, "Route analysis: runnability and
 * the terrain rasters".
 */

/** The runnability of forest, which is also that of bare paper. */
constexpr int forest_runnability = 83;

/** The runnability of a symbol by its number in ISOM 2017-2. */
Runnability defaultRunnability(const Symbol& symbol);

/** The number of a symbol as the settings name it: "406.000". */
QString routeSymbolNumber(const Symbol& symbol);

/** The runnability of a symbol by the settings, or else its default. */
Runnability runnabilityOf(const RouteSettings& settings, const Symbol& symbol);


/** The rasters of an area of the map. */
struct RouteTerrain
{
	/** The area in mm of the map. */
	QRectF area;
	/** The size of a pixel in mm of the map. */
	double pixel = 0.127;
	int width = 0;
	int height = 0;
	/** For every pixel, row by row from the top, the runnability in percent. */
	std::vector<quint8> runnability;
	/** For every pixel 1 where the terrain is impassable. */
	std::vector<quint8> impassable;
	/** The perimeter of the analysis in mm of the map; empty for none. Outside it every pixel is impassable. */
	QPolygonF perimeter;
	
	bool isEmpty() const { return width <= 0 || height <= 0; }
	/** The runnability at a place of the map; that of forest outside the area. */
	int runnabilityAt(const MapCoordF& place) const;
	/** False outside the area. */
	bool isImpassableAt(const MapCoordF& place) const;
	/** Whether a place is within the perimeter; every place is when there is none. */
	bool isWithinPerimeter(const MapCoordF& place) const;
};

/**
 * Makes the rasters from the objects of the map and of its background
 * maps which are maps and are loaded. Without an area, that of all these objects.
 */
RouteTerrain buildRouteTerrain(const Map& map, const RouteSettings& settings, const QRectF& area = {});

/**
 * Writes the rasters as "<base>.~run.png" and "<base>.~imp.png", each
 * with a world file in mm of the map. Returns false when a file cannot be written.
 */
bool saveRouteTerrain(const RouteTerrain& terrain, const QString& base_path);


/*
 * The cache of the rasters; the design notes, "Route
 * analysis: the cache of the rasters".
 */

/** Where the rasters which cachedRouteTerrain() gives came from. */
enum class RouteTerrainSource
{
	Memory,  ///< those which were made last in the program
	File,    ///< the file of the cache
	Made     ///< made anew
};

/**
 * A hash of everything the rasters of buildRouteTerrain() are made of:
 * the settings of the runnability and the perimeter, the area, the extent
 * of the map, and the objects whose symbols count. Not the base pace, the
 * elevation, or objects whose symbols do not count.
 */
QByteArray routeTerrainKey(const Map& map, const RouteSettings& settings, const QRectF& area = {});

/**
 * The rasters: those made last in the program when the key is theirs,
 * else those of the file at cache_path when it has the key, else made
 * anew, kept in memory, and written to cache_path (when it is not empty).
 */
RouteTerrain cachedRouteTerrain(const Map& map, const RouteSettings& settings, const QString& cache_path,
                                const QRectF& area = {}, RouteTerrainSource* source = nullptr);

/** The file of the cache beside a map file: "<file>.~route"; empty for none. */
QString routeTerrainCachePath(const QString& map_path);

/** Forgets the rasters which are kept in memory. */
void forgetRouteTerrainMemory();

/** Forgets the rasters in memory and removes the file of the cache, when a path is given. */
void resetRouteTerrainCache(const QString& cache_path);


}  // namespace OpenOrienteering

#endif
