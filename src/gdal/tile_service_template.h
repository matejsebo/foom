/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_TILE_SERVICE_TEMPLATE_H
#define OPENORIENTEERING_TILE_SERVICE_TEMPLATE_H

#include <vector>

#include <QtGlobal>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QString>

#include "gdal/map_service.h"
#include "gdal/tile_pyramid.h"
#include "templates/template.h"

class QPainter;
class QThread;
class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

class Georeferencing;
class Map;
class TileWorker;


/**
 * A template showing a WMS, WMTS or XYZ tile service.
 *
 * The template keeps its own pyramid of 512 by 512 pixel tiles in the map's
 * projected coordinate system, fetched by a worker thread through GDAL's WMS
 * and WMTS drivers and cached in memory. It is always georeferenced, has no
 * file, and cannot be drawn onto or moved by hand.
 *
 * See the design notes, "Tile service templates:
 * detailed design".
 */
class TileServiceTemplate : public Template
{
Q_OBJECT
public:
	/** The template type string. */
	static const char* templateType() { return "TileServiceTemplate"; }

	/** Whether GDAL offers the WMS and WMTS drivers; otherwise reason says what is missing. */
	static bool driversAvailable(QString* reason = nullptr);

	/** The directory for GDAL's disk cache of fetched tiles. */
	static QString cacheDirectory();
	static void setCacheDirectory(const QString& path);

	/** The largest side of a Download extent raster, in pixels. */
	static constexpr int max_download_pixels = 20000;


	TileServiceTemplate(const MapService& service, Map* map);

	/** For Template::templateForType: the service is read from the XML later. */
	TileServiceTemplate(const QString& path, Map* map);

protected:
	TileServiceTemplate(const TileServiceTemplate& proto);

public:
	~TileServiceTemplate() override;

	TileServiceTemplate* duplicate() const override;

	const char* getTemplateType() const override { return templateType(); }
	bool isRasterGraphics() const override { return true; }
	
	/**
	 * A finite square of 100 m of paper around the map origin.
	 * 
	 * A service covers whatever the views show; the base class's extent of
	 * 10^11 mm overflows the pixel rectangle of a repaint, so loading would
	 * repaint nothing.
	 */
	QRectF getTemplateExtent() const override;

	const MapService& service() const { return service_; }


	bool fileExists() const override { return true; }
	LookupResult tryToFindTemplateFile(const QString& map_path) override;

	bool loadTemplateFileImpl() override;
	void unloadTemplateFileImpl() override;

	void drawTemplate(QPainter* painter, const QRectF& clip_rect, double scale, bool on_screen, qreal opacity) const override;

	void saveTypeSpecificTemplateConfiguration(QXmlStreamWriter& xml) const override;
	bool loadTypeSpecificTemplateConfiguration(QXmlStreamReader& xml) override;


	/** The bounding rectangle, in projected coordinates, of a rectangle in map coordinates (mm). */
	static QRectF projectedBounds(const Georeferencing& georef, const QRectF& map_rect_mm);
	QRectF projectedBounds(const QRectF& map_rect_mm) const;

	/** The ground resolution of level 0, in projected units per pixel; 0 before loading. */
	double baseResolution() const { return r0; }

	/** The tiles queued by the last on-screen paint. */
	std::vector<TileKey> lastRequests() const { return last_requests; }

	/** The number of tiles requested and not yet received. */
	int pendingCount() const { return in_flight.size(); }

	/** The number of tiles in the memory cache. */
	int cachedTileCount() const { return cache.count(); }

	/** The message of the last failed tile, empty when none failed. */
	QString lastTileError() const { return last_tile_error; }


	/**
	 * Starts writing the given window, in projected coordinates and metres
	 * per pixel, to a tiled compressed GeoTIFF in the map's system.
	 *
	 * Returns false, with a message, when the window exceeds
	 * max_download_pixels on a side, when the service forbids bulk download,
	 * or when a download is running. Otherwise downloadProgress and
	 * downloadFinished report the outcome.
	 */
	bool downloadExtent(const QRectF& projected_rect, double resolution, const QString& path, QString* error = nullptr);

	/** Asks a running download to stop. */
	void cancelDownload();

	bool isDownloading() const { return downloading; }

signals:
	/** A tile arrived or failed. */
	void tilesChanged();

	void downloadProgress(int percent);
	void downloadFinished(bool success, const QString& message);

private slots:
	void tileReady(const OpenOrienteering::TileKey& key, const QImage& image);
	void tileFailed(const OpenOrienteering::TileKey& key, const QString& message);
	void workerDownloadFinished(bool success, const QString& message);
	void projectionChanged();

private:
	void startWorker();
	void stopWorker();
	void drawCached(QPainter* painter, const QRectF& projected_bounds, int level) const;

	MapService service_;
	double r0 = 0;
	bool downloading = false;
	QString last_tile_error;

	mutable QCache<TileKey, QImage> cache;
	mutable QHash<TileKey, qint64> failed_until;
	mutable QSet<TileKey> in_flight;
	mutable std::vector<TileKey> last_requests;

	QThread* thread = nullptr;
	TileWorker* worker = nullptr;
};


}  // namespace OpenOrienteering

#endif
