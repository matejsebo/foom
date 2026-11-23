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

#ifndef FOOM_ONLINE_MAP_H
#define FOOM_ONLINE_MAP_H

#include <vector>

#include <QDialog>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>

#include "core/geoviewer.h"
#include "gdal/map_service.h"

class QComboBox;
class QEvent;
class QPushButton;

namespace OpenOrienteering {

class Map;
class TileMapWidget;


/**
 * The online map: a look at imagery and maps of the services around the
 * map, which can be kept as a background map.
 *
 * See the design notes, "Online map and geoviewer".
 */

/**
 * Writes the picture of the projected extent as a GeoTIFF in the system,
 * with the attribution at its lower right when one is given.
 */
bool saveOnlineMapImage(const QImage& image, const QRectF& extent, const QString& crs_spec,
                        const QString& attribution, const QString& path, QString* error = nullptr);

/** The first free <map>_online_<n>.tif beside the map; in the documents for an unsaved map. */
QString proposedOnlineMapPath(const QString& map_path);


class OnlineMapDialog : public QDialog
{
Q_OBJECT
public:
	OnlineMapDialog(Map& map, const QString& map_path, QWidget* parent = nullptr);
	~OnlineMapDialog() override;

	TileMapWidget* mapWidget() const { return map_widget; }

	/** The service shown. */
	const MapService& service() const;

	/** Chooses the service of that index. */
	void setServiceIndex(int index);

	/** The place at the centre of the picture, looking north. */
	bool centreTarget(GeoviewerTarget& target) const;

	/** Writes the picture shown to the file and attaches it as a template. */
	bool loadAsBackgroundMap(const QString& path, QString* error = nullptr);

	/** Moves the picture by pixels of the widget, as a drag does. */
	void pan(const QPoint& pixels);

	/** Zooms by the factor about the widget's pixel, as the wheel does (> 1 in). */
	void zoom(double factor, const QPointF& pixel);

signals:
	/** The geoviewer's URL for the centre; the editor gives it to the browser. */
	void webViewerRequested(const QUrl& url);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void serviceChanged(int index);
	void openWebViewer();
	void loadClicked();

	Map& map;
	QString map_path;
	std::vector<MapService> services;
	TileMapWidget* map_widget;
	QComboBox* service_box;
	QPushButton* web_button;
	QPushButton* load_button;
	QPoint drag_start;
	bool dragging = false;
};


}  // namespace OpenOrienteering

#endif
