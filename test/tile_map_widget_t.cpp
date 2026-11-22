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

#include <cmath>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRectF>
#include <QSettings>
#include <QSignalSpy>
#include <QSizeF>
#include <QString>
#include <QTemporaryDir>
#include <QTransform>
#include <QUrl>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_transform.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "gdal/map_service.h"
#include "gdal/tile_map_widget.h"
#include "gdal/tile_service_template.h"
#include "gui/map/map_transform_dialogs.h"

using namespace OpenOrienteering;


namespace {

/// The colour of each zoom level's tiles.
QColor levelColor(int level)
{
	static QColor const colors[] = { QColor(200, 40, 40), QColor(40, 200, 40), QColor(40, 40, 200) };
	return colors[level];
}

/// Writes the tiles of zoom levels 0 to 2, 256 px each, one colour per level.
bool writeTiles(const QString& root)
{
	for (int z = 0; z <= 2; ++z)
	{
		QImage tile(256, 256, QImage::Format_RGB32);
		tile.fill(levelColor(z));
		for (int x = 0; x < (1 << z); ++x)
		{
			auto const dir = QStringLiteral("%1/%2/%3").arg(root).arg(z).arg(x);
			if (!QDir().mkpath(dir))
				return false;
			for (int y = 0; y < (1 << z); ++y)
			{
				if (!tile.save(QStringLiteral("%1/%2.png").arg(dir).arg(y)))
					return false;
			}
		}
	}
	return true;
}

/// An XYZ service reading the tiles below root through a file:// pattern.
MapService offlineService(const QString& root, int max_zoom = 2)
{
	MapService service;
	service.kind = MapService::Xyz;
	service.name = QStringLiteral("Offline tiles");
	service.url = QUrl::fromLocalFile(root).toString() + QStringLiteral("/${z}/${x}/${y}.png");
	service.min_zoom = 0;
	service.max_zoom = max_zoom;
	service.attribution = QStringLiteral("Test tiles");
	return service;
}

bool near(const QPointF& a, const QPointF& b, double tolerance)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

bool near(const QRectF& a, const QRectF& b, double tolerance)
{
	return near(a.topLeft(), b.topLeft(), tolerance) && near(a.bottomRight(), b.bottomRight(), tolerance);
}

/// True when every pixel is within 2 per channel of the colour.
bool uniform(const QImage& image, const QColor& color)
{
	auto const argb = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < argb.height(); ++y)
	{
		for (int x = 0; x < argb.width(); ++x)
		{
			auto const pixel = argb.pixel(x, y);
			if (std::abs(qRed(pixel) - color.red()) > 2
			    || std::abs(qGreen(pixel) - color.green()) > 2
			    || std::abs(qBlue(pixel) - color.blue()) > 2
			    || qAlpha(pixel) < 253)
				return false;
		}
	}
	return true;
}

/// The map of Accept (6): 1:10000, EPSG:32613, (500000, 5200000) at map (0, 0), one object (-50, -20) to (50, 20) mm.
void setupMap(Map& map, double grivation, bool with_object = true, bool geospatial = true)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	if (geospatial)
		georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setMapRefPoint(MapCoord(0.0, 0.0));
	georef.setProjectedRefPoint(QPointF(500000.0, 5200000.0), false, false);
	georef.setCombinedScaleFactor(1.0);
	georef.setGrivation(grivation);
	if (!geospatial)
		georef.setLocalState();
	map.setGeoreferencing(georef);
	if (with_object)
	{
		auto* line = new LineSymbol();
		line->setLineWidth(0.3);
		map.addSymbol(line, 0);
		map.addObject(new PathObject(line, MapCoordVector{ MapCoord(-50.0, -20.0), MapCoord(50.0, 20.0) }));
	}
}

/// The corners of the box (x0, y0) to (x1, y1) in the order top left, top right, bottom right, bottom left with y up.
QPolygonF corners(double x0, double y0, double x1, double y1)
{
	return QPolygonF({ QPointF(x0, y1), QPointF(x1, y1), QPointF(x1, y0), QPointF(x0, y0) });
}

}  // namespace


/**
 * Tests the tile map widget and the map of the surroundings.
 *
 * See the design notes, "The map of the surroundings".
 */
class TileMapWidgetTest : public QObject
{
Q_OBJECT
private:
	QTemporaryDir tiles_dir;
	QString tiles;

private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
		QSettings().remove(QStringLiteral("MagneticNorthDialog"));

		QString reason;
		if (!TileServiceTemplate::driversAvailable(&reason))
			QSKIP(qPrintable(reason));
		QVERIFY(tiles_dir.isValid());
		tiles = tiles_dir.path() + QStringLiteral("/tiles");
		QVERIFY(writeTiles(tiles));
	}

	void cleanupTestCase()
	{
		QSettings().remove(QStringLiteral("MagneticNorthDialog"));
		MapService::toSettings(MapService::presets());
	}


	void fitExtentTest()
	{
		// Accept (1)
		auto const extent = QRectF(QPointF(0, 0), QPointF(1000, 500));
		QCOMPARE(TileMapWidget::fitExtent(extent, QSizeF(400, 400)), QRectF(QPointF(0, -250), QPointF(1000, 750)));
		QCOMPARE(TileMapWidget::fitExtent(extent, QSizeF(200, 50)), QRectF(QPointF(-500, 0), QPointF(1500, 500)));
		QCOMPARE(TileMapWidget::fitExtent(extent, QSizeF(37, 91)).center(), extent.center());
	}

	void geometryTest()
	{
		// Accept (2)
		TileMapWidget widget;
		widget.resize(400, 300);
		widget.setExtent(QRectF(QPointF(1000, 2000), QPointF(1400, 2300)));
		QVERIFY(near(widget.renderedExtent(), QRectF(QPointF(1000, 2000), QPointF(1400, 2300)), 1e-9));
		auto const transform = widget.projectedToWidget();
		QVERIFY(near(transform.map(QPointF(1000, 2300)), QPointF(0, 0), 1e-9));
		QVERIFY(near(transform.map(QPointF(1400, 2000)), QPointF(400, 300), 1e-9));
		widget.setOutline(corners(1100, 2100, 1300, 2200));
		auto const outline = widget.outlineInWidget();
		QCOMPARE(outline.size(), 4);
		QVERIFY(near(outline[0], QPointF(100, 100), 1e-9));
		QVERIFY(near(outline[1], QPointF(300, 100), 1e-9));
		QVERIFY(near(outline[2], QPointF(300, 200), 1e-9));
		QVERIFY(near(outline[3], QPointF(100, 200), 1e-9));
	}

	void offlineSourceTest()
	{
		// Accept (3)
		for (int max_zoom : { 2, 0 })
		{
			TileMapWidget widget;
			widget.resize(64, 64);
			widget.setService(offlineService(tiles, max_zoom));
			widget.setCrs(QStringLiteral("EPSG:3857"));
			widget.setExtent(QRectF(QPointF(-500000, -500000), QPointF(500000, 500000)));
			QVERIFY(!widget.isLoaded());
			QSignalSpy spy(&widget, &TileMapWidget::rendered);
			widget.load();
			QVERIFY(widget.isLoaded());
			QVERIFY(spy.wait(20000));
			QCOMPARE(spy.count(), 1);
			QVERIFY2(spy.front().front().toBool(), qPrintable(widget.errorString()));
			QCOMPARE(widget.image().size(), QSize(64, 64));
			QVERIFY2(uniform(widget.image(), levelColor(max_zoom)), qPrintable(QStringLiteral("max_zoom %1").arg(max_zoom)));
		}
	}

	void failureTest()
	{
		// Accept (4)
		TileMapWidget widget;
		widget.resize(64, 64);
		widget.setService(offlineService(tiles_dir.path() + QStringLiteral("/missing")));
		widget.setCrs(QStringLiteral("EPSG:3857"));
		widget.setExtent(QRectF(QPointF(-500000, -500000), QPointF(500000, 500000)));
		widget.setOutline(corners(-100000, -100000, 100000, 100000));
		QSignalSpy spy(&widget, &TileMapWidget::rendered);
		widget.load();
		QVERIFY(spy.wait(20000));
		QCOMPARE(spy.front().front().toBool(), false);
		QVERIFY(!widget.errorString().isEmpty());
		QVERIFY(widget.image().isNull());
		QCOMPARE(widget.outlineInWidget().size(), 4);
	}

	void staleResultTest()
	{
		// Accept (5)
		TileMapWidget widget;
		widget.resize(64, 64);
		widget.setService(offlineService(tiles));
		widget.setCrs(QStringLiteral("EPSG:3857"));
		QSignalSpy spy(&widget, &TileMapWidget::rendered);
		widget.setExtent(QRectF(QPointF(-500000, -500000), QPointF(500000, 500000)));
		widget.load();
		auto const second = QRectF(QPointF(1000000, 1000000), QPointF(1500000, 1500000));
		widget.setExtent(second);
		widget.load();
		QVERIFY(spy.wait(20000));
		QTest::qWait(500);
		QCOMPARE(spy.count(), 1);
		QVERIFY(spy.front().front().toBool());
		QVERIFY(near(widget.imageExtent(), second, 1e-6));
	}

	void surroundingsTest()
	{
		// Accept (6)
		{
			Map map;
			setupMap(map, 0.0);
			auto const s = mapSurroundings(map);
			auto const expected = corners(499500, 5199800, 500500, 5200200);
			QCOMPARE(s.outline.size(), 4);
			for (int i = 0; i < 4; ++i)
				QVERIFY2(near(s.outline[i], expected[i], 0.01), qPrintable(QStringLiteral("corner %1: %2 %3").arg(i).arg(s.outline[i].x(), 0, 'f', 3).arg(s.outline[i].y(), 0, 'f', 3)));
			QVERIFY(near(s.extent, QRectF(QPointF(499000, 5199300), QPointF(501000, 5200700)), 0.01));
		}
		{
			Map map;
			setupMap(map, 10.0);
			auto const s = mapSurroundings(map);
			auto const expected = corners(-500, -200, 500, 200);
			auto const a = qDegreesToRadians(10.0);
			QCOMPARE(s.outline.size(), 4);
			for (int i = 0; i < 4; ++i)
			{
				// Clockwise by 10 degrees with y up.
				auto const p = expected[i];
				auto const turned = QPointF(500000 + p.x() * std::cos(a) + p.y() * std::sin(a),
				                            5200000 - p.x() * std::sin(a) + p.y() * std::cos(a));
				QVERIFY2(near(s.outline[i], turned, 0.01), qPrintable(QStringLiteral("corner %1").arg(i)));
			}
		}
		{
			Map map;
			setupMap(map, 0.0, false);
			auto const s = mapSurroundings(map);
			QVERIFY(s.outline.isEmpty());
			QVERIFY(near(s.extent, QRectF(QPointF(499000, 5199000), QPointF(501000, 5201000)), 0.01));
		}
		{
			Map map;
			setupMap(map, 0.0, true, false);
			auto const s = mapSurroundings(map);
			QVERIFY(s.outline.isEmpty());
			QVERIFY(s.extent.isNull());
		}
	}

	void dialogTest()
	{
		// Accept (7)
		MapService::toSettings({ offlineService(tiles) });
		QSettings().remove(QStringLiteral("MagneticNorthDialog"));
		Map map;
		setupMap(map, 0.0);
		{
			MagneticNorthDialog dialog(map.getGeoreferencing(), nullptr, &map);
			auto* widget = dialog.surroundings();
			QVERIFY(widget);
			QVERIFY(!widget->isLoaded());
			auto const expected = mapSurroundings(map);
			QCOMPARE(widget->outline(), expected.outline);
			QCOMPARE(widget->extent(), expected.extent);
			QVERIFY(dialog.surroundingsServiceBox());
			QCOMPARE(dialog.surroundingsServiceBox()->currentText(), QStringLiteral("Offline tiles"));
			QSignalSpy spy(widget, &TileMapWidget::rendered);
			QVERIFY(dialog.showMapButton());
			dialog.showMapButton()->click();
			QVERIFY(widget->isLoaded());
			QVERIFY(spy.wait(20000));
			QVERIFY2(spy.front().front().toBool(), qPrintable(widget->errorString()));
			QCOMPARE(QSettings().value(QStringLiteral("MagneticNorthDialog/showSurroundings")).toBool(), true);
		}
		{
			MagneticNorthDialog dialog(map.getGeoreferencing(), nullptr, &map);
			auto* widget = dialog.surroundings();
			QVERIFY(widget);
			// It loads as soon as the event loop runs, after the dialog is shown.
			QSignalSpy spy(widget, &TileMapWidget::rendered);
			QVERIFY(spy.wait(20000));
			QVERIFY(widget->isLoaded());
			QVERIFY(spy.front().front().toBool());
		}
		{
			Map local;
			setupMap(local, 0.0, true, false);
			MagneticNorthDialog dialog(local.getGeoreferencing(), nullptr, &local);
			QVERIFY(!dialog.surroundings());
		}
	}
};


QTEST_MAIN(TileMapWidgetTest)

#include "tile_map_widget_t.moc"
