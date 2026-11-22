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

#ifndef OPENORIENTEERING_TILE_MAP_WIDGET_H
#define OPENORIENTEERING_TILE_MAP_WIDGET_H

#include <memory>

#include <QImage>
#include <QObject>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QWidget>

#include "gdal/map_service.h"

class QPaintEvent;
class QResizeEvent;
class QTimer;

namespace OpenOrienteering {


/**
 * A small picture of a map service's map of a projected extent, with an outline over it.
 *
 * It renders by one GDAL warp of the extent at the widget's size, on a thread
 * of the global thread pool. It goes online only after load(). An XYZ service
 * with a file:// pattern reads tiles from local files.
 *
 * Projected rectangles have left/top = min x/y; y points north, up the widget.
 *
 * See the design notes, "The map of the surroundings".
 */
class TileMapWidget : public QWidget
{
Q_OBJECT
public:
	explicit TileMapWidget(QWidget* parent = nullptr);
	~TileMapWidget() override;

	void setService(const MapService& service);
	const MapService& service() const { return service_; }

	/// The projected coordinate system, a PROJ spec or anything GDAL takes (EPSG:3857).
	void setCrs(const QString& spec);
	const QString& crs() const { return crs_; }

	/// The projected extent to show.
	void setExtent(const QRectF& extent);
	const QRectF& extent() const { return extent_; }

	/// A projected polygon drawn over the map.
	void setOutline(const QPolygonF& outline);
	const QPolygonF& outline() const { return outline_; }

	/// The text shown while nothing is loaded.
	void setPlaceholderText(const QString& text);

	/// The extent grown to the widget's aspect ratio about its centre.
	QRectF renderedExtent() const;

	/// From projected coordinates to widget pixels for the rendered extent.
	QTransform projectedToWidget() const;

	/// The outline in widget pixels.
	QPolygonF outlineInWidget() const;

	/// Starts rendering, and renders again after every change from now on.
	void load();

	/// Whether load() was called.
	bool isLoaded() const { return loaded; }

	/// The last rendered image, null before the first or after a failure.
	const QImage& image() const { return image_; }

	/// The projected extent of image().
	const QRectF& imageExtent() const { return image_extent; }

	/// Why the last rendering failed, empty when it did not.
	const QString& errorString() const { return error; }

	QSize sizeHint() const override;

	/// The extent grown to the aspect ratio of size about its centre.
	static QRectF fitExtent(const QRectF& extent, const QSizeF& size);

signals:
	/// A rendering of the current request ended.
	void rendered(bool success);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	struct Relay;

	void changed();
	void request();
	void finished(int id, const QImage& image, const QRectF& extent, const QString& message);

	MapService service_;
	QString crs_;
	QRectF extent_;
	QPolygonF outline_;
	QString placeholder;
	QImage image_;
	QRectF image_extent;
	QString error;
	QSize requested_size;
	QTimer* timer;
	std::shared_ptr<Relay> relay;
	int request_id = 0;
	bool loaded = false;
	bool busy = false;
};


}  // namespace OpenOrienteering

#endif
