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
#include <QRectF>
#include <QString>

#include "core/map_coord.h"

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

/** How fast a symbol lets a runner go, in percent of the full speed. */
struct Runnability
{
	enum Kind
	{
		None,        ///< the symbol does not count
		Ground,      ///< open ground: the percentage of full speed there
		Vegetation,  ///< forest with undergrowth: the percentage of the speed in forest; it covers the ground below
		Hindrance,   ///< undergrowth drawn over ground or forest, and lines which are crossed: the slowest of what covers a place counts
		Surface,     ///< stony or broken ground: multiplies what is there
		Path,        ///< a road, a path or a ride: its percentage in place of everything
		Impassable
	};
	
	Kind kind = None;
	int percent = 100;
};

inline bool operator==(const Runnability& lhs, const Runnability& rhs) { return lhs.kind == rhs.kind && lhs.percent == rhs.percent; }
inline bool operator!=(const Runnability& lhs, const Runnability& rhs) { return !(lhs == rhs); }

/** The runnability of forest, which is also that of bare paper. */
constexpr int forest_runnability = 83;

/** The runnability of a symbol by its number in ISOM 2017-2. */
Runnability defaultRunnability(const Symbol& symbol);

/** The number of a symbol as the settings name it: "406.000". */
QString routeSymbolNumber(const Symbol& symbol);

/** The settings of the route analysis. */
struct RouteSettings
{
	/** The pace on flat ground of full runnability, in seconds per kilometre. */
	int base_pace = 200;
	bool use_runnability = true;
	bool use_elevation = true;
	/** The record's correction of the effect of the climb, from 0 to 1. */
	double climb_factor = 1.0;
	/** The runnability of single symbols where it is not the default, by their numbers. */
	std::vector<std::pair<QString, Runnability>> symbols;
};

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
	
	bool isEmpty() const { return width <= 0 || height <= 0; }
	/** The runnability at a place of the map; that of forest outside the area. */
	int runnabilityAt(const MapCoordF& place) const;
	/** False outside the area. */
	bool isImpassableAt(const MapCoordF& place) const;
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


}  // namespace OpenOrienteering

#endif
