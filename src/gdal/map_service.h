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


#ifndef OPENORIENTEERING_MAP_SERVICE_H
#define OPENORIENTEERING_MAP_SERVICE_H

#include <vector>

#include <QByteArray>
#include <QRectF>
#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {


/**
 * The description of a WMS, WMTS or XYZ tile service, as a template uses it.
 *
 * This is what the `service` element of a TileServiceTemplate holds, see
 * the design notes, "Tile service templates: detailed
 * design". The credentials are never written to a file.
 */
struct MapService
{
	enum Kind
	{
		Wms  = 0,  ///< OGC Web Map Service 1.1.1 or 1.3.0
		Wmts = 1,  ///< OGC Web Map Tile Service 1.0.0, read through GDAL's WMTS driver
		Xyz  = 2,  ///< A tile URL pattern with ${z}, ${x}, ${y} (or {z}, {x}, {y}) in web mercator
	};

	Kind kind = Xyz;
	QString name;                                 ///< The display name
	QString url;                                  ///< WMS base URL, WMTS capabilities URL, or XYZ pattern
	QString version = QStringLiteral("1.3.0");    ///< WMS version
	QString layers;                               ///< Comma-separated layer names (WMS), the layer id (WMTS)
	QString styles;                               ///< Comma-separated styles (WMS)
	QString format = QStringLiteral("image/png"); ///< Image format (WMS)
	QString crs = QStringLiteral("EPSG:3857");    ///< The coordinate system the images are requested in
	QRectF bbox;                                  ///< The layer's bounding box in crs (left/top = min x/y); null when unknown
	double resolution = 0.0;                      ///< The finest ground resolution offered (WMS), 0 when unknown
	QString matrix_set;                           ///< The tile matrix set (WMTS)
	int min_zoom = 0;                             ///< Coarsest zoom level (XYZ)
	int max_zoom = 19;                            ///< Finest zoom level (XYZ)
	QString attribution;                          ///< Drawn on the map view while the template is visible
	bool bulk_download = true;                    ///< False disables Download extent (fair use)
	QString user;                                 ///< Credentials for this session only, never saved
	QString password;


	/** Equality of everything that is saved. */
	bool operator==(const MapService& other) const;
	bool operator!=(const MapService& other) const { return !(*this == other); }

	static QString kindName(Kind kind);
	static Kind kindFromName(const QString& name, bool* ok = nullptr);

	/** Writes the `service` element. */
	void save(QXmlStreamWriter& xml) const;

	/** Reads the `service` element the reader is positioned at. */
	static MapService load(QXmlStreamReader& xml);

	/**
	 * The GDAL source for this service: a GDAL_WMS description in memory for
	 * WMS and XYZ, or a WMTS: connection string.
	 *
	 * The cache directory receives GDAL's disk cache of fetched tiles.
	 */
	QByteArray gdalSource(const QString& cache_dir) const;

	/** The coordinate system GDAL's dataset will be in. */
	QString effectiveCrs() const;

	/** The URL pattern with {z}/{x}/{y} normalized to GDAL's ${z}/${x}/${y}. */
	QString normalizedXyzUrl() const;


	/** The shipped presets. */
	static std::vector<MapService> presets();

	/** The user's service list from the application settings, the presets when there is none. */
	static std::vector<MapService> fromSettings();

	/** Stores the user's service list in the application settings (without credentials). */
	static void toSettings(const std::vector<MapService>& services);
};


}  // namespace OpenOrienteering

#endif
