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


#ifndef OPENORIENTEERING_NEW_MAP_H
#define OPENORIENTEERING_NEW_MAP_H

#include <memory>

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "core/latlon.h"

class QUrl;

namespace OpenOrienteering {

class Map;


/**
 * foom: A box in WGS 84 latitude and longitude, in degrees.
 */
struct GeoBox
{
	double south = 0;
	double west = 0;
	double north = 0;
	double east = 0;
	
	bool isValid() const { return south < north && west < east && south >= -90 && north <= 90; }
	LatLon centre() const { return { (south + north) / 2, (west + east) / 2 }; }
};


/** foom: The largest box for an OpenStreetMap download, in square kilometres. */
constexpr double new_map_max_download_km2 = 25.0;

/**
 * foom: Returns the EPSG code of the UTM zone of a point, north or south.
 *
 * The zone is floor((lon + 180) / 6) + 1; the Norway and Svalbard exceptions are not applied.
 */
int newMapUtmEpsg(const LatLon& lat_lon);

/** foom: Returns the area of the box on the sphere, in square kilometres. */
double newMapAreaKm2(const GeoBox& box);

/** foom: Returns the Overpass query for the box. */
QString newMapOverpassQuery(const GeoBox& box);

/** foom: Returns the Overpass endpoint from the settings, by default the public interpreter. */
QString newMapDefaultEndpoint();

/** foom: Returns the box's corners in the map's coordinates, in millimetres. */
QPolygonF newMapBoxPolygon(const Map& map, const GeoBox& box);

/**
 * foom: Returns the bounding rectangle of the box's outline in the map's
 * projected system (left, bottom, width, height), every edge sampled at 16 points.
 */
QRectF newMapProjectedArea(const Map& map, const GeoBox& box);


/** foom: The most cells a side which the elevation service of the USGS gives in one request. */
constexpr int new_map_usgs_max_cells = 8000;

/**
 * foom: Returns the image service of the 3D Elevation Program of the U.S. Geological Survey from the settings.
 *
 * See the design notes, "Elevation from USGS 3DEP in the new map wizard".
 */
QString newMapDefaultUsgsElevationUrl();

/** foom: Says whether the point lies where the 3D Elevation Program has data, by rough boxes around the United States. */
bool newMapUsgsCovers(const LatLon& lat_lon);

/**
 * foom: Returns the rectangle (left, bottom, width, height) in the UTM zone of
 * the box's centre which holds the box, its edges on whole multiples of the
 * cell size; `epsg` gets the code of the zone.
 */
QRectF newMapUsgsElevationArea(const GeoBox& box, double cell, int* epsg = nullptr);

/** foom: Returns the smallest of the cell sizes 1, 2, 5 and 10 m at which the area fits one request, 0 when none does. */
double newMapUsgsCellFor(const QRectF& area);

/** foom: Returns the request for the elevation grid of the area, which is in the system of the EPSG code. */
QUrl newMapUsgsElevationRequest(const QString& endpoint, const QRectF& area, int epsg, double cell);

/**
 * foom: Downloads the elevation grid of the area as a GeoTIFF.
 *
 * Runs a local event loop until done. After an HTTP status of 500 to 599 it
 * waits retry_delay_ms and tries once more. An answer which is no TIFF fails.
 */
bool downloadUsgsElevation(const QString& endpoint, const QRectF& area, int epsg, double cell, const QString& out_path, QString* error, int retry_delay_ms = 3000);


/**
 * foom: The parameters of createNewMap().
 *
 * See the design notes, "New map flow".
 */
struct NewMapParams
{
	GeoBox box;
	QString symbol_set_path;          ///< A map file whose colours and symbols the new map gets; may be empty.
	unsigned int scale = 0;           ///< 0: the symbol set's scale.
	bool scale_symbols = true;        ///< Scale the symbols when scale differs from the symbol set's.
	QString crs_spec;                 ///< Empty: UTM from the box centre; else "EPSG:<code>" or a PROJ string.
	double declination = 0;
	bool rotate_to_magnetic = true;   ///< False: grivation 0, grid north up.
	QString osm_path;                 ///< An OpenStreetMap file to import; empty: download.
	QString osm_config;               ///< GDAL's OSM_CONFIG_FILE for the import; empty: as GDAL is configured.
	QString endpoint;                 ///< Empty: newMapDefaultEndpoint().
	int retry_delay_ms = 30000;       ///< Before the one retry after HTTP 429 or 504.
	QString crt_file;                 ///< Empty: OSM-<symbol set id>.crt from the symbol sets.
	QString tile_service;             ///< The name of a map service to add as template; empty: none.
	QString elevation_path;           ///< A grid or a point cloud (.las, .laz); empty: none. Needs map_path.
	double elevation_cell = 1.0;      ///< The cell size for gridding a point cloud and for the download, in metres.
	bool usgs_elevation = false;      ///< Download the terrain from the USGS 3D Elevation Program when elevation_path is empty. Needs map_path.
	QString usgs_elevation_url;       ///< Empty: newMapDefaultUsgsElevationUrl().
	QString map_path;                 ///< Where the map is saved; the download goes beside it. Empty: not saved.
};


/**
 * foom: The outcome of createNewMap().
 */
struct NewMapResult
{
	bool ok = false;
	QString error;
	QStringList warnings;
	std::unique_ptr<Map> map;
	QString osm_path;
	QString elevation_path;           ///< The terrain grid saved beside the map; empty without one.
	int objects = 0;
};


/**
 * foom: Downloads the OpenStreetMap data of the box from an Overpass endpoint.
 *
 * Runs a local event loop until done. After HTTP 429 or 504 it waits
 * retry_delay_ms and tries once more.
 */
bool downloadOverpass(const QString& endpoint, const GeoBox& box, const QString& out_path, int retry_delay_ms, QString* error);

/**
 * foom: Creates a map for the box: symbols, georeferencing, OpenStreetMap
 * data clipped to the box, the tile service as template, and saves it.
 *
 * See the design notes, "New map flow".
 */
NewMapResult createNewMap(const NewMapParams& params);


}  // namespace OpenOrienteering

#endif
