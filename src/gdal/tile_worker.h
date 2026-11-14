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


#ifndef OPENORIENTEERING_TILE_WORKER_H
#define OPENORIENTEERING_TILE_WORKER_H

#include <atomic>
#include <mutex>
#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "gdal/tile_pyramid.h"

namespace OpenOrienteering {


/**
 * The worker of a TileServiceTemplate.
 *
 * It lives in the template's thread and owns the GDAL dataset, which must
 * not be used from any other thread. open() and close() are called through
 * blocking queued connections; tile requests arrive through setPending()
 * (thread-safe) followed by a queued invocation of work().
 */
class TileWorker : public QObject
{
Q_OBJECT
public:
	TileWorker();
	~TileWorker() override;

	/**
	 * Opens the GDAL source and prepares reprojection to the map's system.
	 *
	 * reference is a point in the map's projected system where the base
	 * resolution is measured. On success, r0 receives that resolution.
	 */
	Q_INVOKABLE bool open(const QByteArray& gdal_source, const QString& map_crs_spec, const QPointF& reference,
	                      const QString& user, const QString& password, QString* error, double* r0);

	Q_INVOKABLE void close();

	/** Replaces the pending list; the front is served first. Thread-safe. */
	void setPending(std::vector<TileKey> keys);

	/** Drops the pending list and asks a running download to stop. Thread-safe. */
	void cancel();

	/** Serves the pending list until it is empty. */
	Q_INVOKABLE void work();

	/** Writes the window to a GeoTIFF; reports through downloadProgress and downloadFinished. */
	Q_INVOKABLE void download(const QRectF& projected_rect, int width, int height, const QString& path);

	double baseResolution() const { return r0; }

signals:
	void tileReady(const OpenOrienteering::TileKey& key, const QImage& image);
	void tileFailed(const OpenOrienteering::TileKey& key, const QString& message);
	void downloadProgress(int percent);
	void downloadFinished(bool success, const QString& message);

private:
	bool fetch(const TileKey& key, QImage& image, QString& error);
	static int progressCallback(double complete, const char* message, void* data);

	void* dataset = nullptr;      ///< GDALDatasetH
	QByteArray source_wkt;
	QByteArray target_wkt;
	bool same_crs = true;
	bool has_alpha = false;
	double r0 = 0;

	std::mutex mutex;
	std::vector<TileKey> pending;
	std::atomic<bool> cancelled{ false };
	int last_percent = -1;
};


}  // namespace OpenOrienteering

#endif
