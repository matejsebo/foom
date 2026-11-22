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

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QMenu>
#include <QObject>
#include <QPointF>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTime>
#include <QWidget>

#include "global.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "sensors/gnss_file.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

QString walkPath()
{
	return QFINDTESTDATA("data/gnss/walk.nmea");
}

Georeferencing utm32Georeferencing()
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	return georef;
}

/// A map of 1:10,000 in UTM 13 N with the line symbol 1 and the point symbol 2.
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	map.setGeoreferencing(utm32Georeferencing());
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setNumberComponent(0, 1);
	line->setLineWidth(0.5);
	line->setColor(black);
	map.addSymbol(line, 0);
	auto* point = new PointSymbol();
	point->setNumberComponent(0, 2);
	map.addSymbol(point, 1);
}

}  // namespace


/**
 * @test Tests reading NMEA files as tracks, waypoints and positions.
 *
 * the design notes, "NMEA and FRWD files".
 */
class GnssFileTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void readTest();
	void objectsTest();
	void editorTest();

private:
	QTemporaryDir settings_dir;
};


void GnssFileTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());

	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void GnssFileTest::readTest()
{
	auto const data = readGnssFile(walkPath());
	QVERIFY2(data.ok(), qPrintable(data.error));
	QCOMPARE(int(data.fixes.size()), 4);
	QCOMPARE(data.skipped, 1);
	QCOMPARE(data.bad_checksums, 1);
	QCOMPARE(int(data.waypoints.size()), 2);

	auto const& first = data.fixes.front();
	QVERIFY(std::abs(first.latitude - 47.0) < 1e-9);
	QVERIFY(std::abs(first.longitude + 107.0) < 1e-9);
	QCOMPARE(first.altitude, 450.5);
	QCOMPARE(first.satellites, 8);
	QCOMPARE(first.hdop, 0.9);
	QCOMPARE(first.quality, 1);
	QCOMPARE(first.date, QDate(2026, 11, 22));
	QCOMPARE(first.utc, QTime(10, 0, 0));
	QCOMPARE(first.isoTime(), QStringLiteral("2026-11-22T10:00:00Z"));
	// The fourth fix is that of the fifth epoch, after the one without a position.
	QCOMPARE(data.fixes[2].utc, QTime(10, 0, 3));

	QCOMPARE(data.waypoints[0].name, QStringLiteral("CAR"));
	QCOMPARE(data.waypoints[1].name, QStringLiteral("TOP"));
	QVERIFY(std::abs(data.waypoints[1].latitude - (47.0 + 0.04 / 60.0)) < 1e-9);
	QVERIFY(std::abs(data.waypoints[1].longitude - (-107.0 + 0.1 / 60.0)) < 1e-9);

	// Another talker, line ends of CR LF, a sentence without a checksum.
	auto const gn = readNmea(QByteArrayLiteral("$GNGGA,120000.00,4700.0000,N,10700.0000,W,2,10,0.7,400.0,M,48.0,M,,\r\n"));
	QVERIFY(gn.ok());
	QCOMPARE(int(gn.fixes.size()), 1);
	QCOMPARE(gn.fixes[0].quality, 2);
	QVERIFY(!gn.fixes[0].date.isValid());
	QCOMPARE(gn.fixes[0].isoTime(), QStringLiteral("12:00:00Z"));

	QVERIFY(!readGnssFile(QStringLiteral("no such file.nmea")).ok());
	QVERIFY(!readNmea(QByteArrayLiteral("no sentence here\n")).ok());

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const frwd = dir.filePath(QStringLiteral("walk.frwd"));
	QVERIFY(QFile::copy(walkPath(), frwd));
	auto const refused = readGnssFile(frwd);
	QVERIFY(!refused.ok());
	QVERIFY(refused.error.contains(QStringLiteral("FRWD")));
}


/// Accept (2)
void GnssFileTest::objectsTest()
{
	Map map;
	fillMap(map);
	auto const* line = map.getSymbol(0);
	auto const* point = map.getSymbol(1);
	auto const data = readGnssFile(walkPath());
	QVERIFY(data.ok());

	auto track = gnssObjects(map, data, GnssImportKind::Track, line);
	QVERIFY2(track.error.isEmpty(), qPrintable(track.error));
	QCOMPARE(int(track.objects.size()), 1);
	auto const* path = track.objects.front()->asPath();
	QVERIFY(path);
	QCOMPARE(int(path->getCoordinateCount()), 4);
	bool ok = false;
	auto const expected = map.getGeoreferencing().toMapCoordF(LatLon(47.0, -107.0), &ok);
	QVERIFY(ok);
	auto const first = MapCoordF(path->getCoordinate(0));
	QVERIFY(std::abs(first.x() - expected.x()) < 0.01);
	QVERIFY(std::abs(first.y() - expected.y()) < 0.01);

	auto waypoints = gnssObjects(map, data, GnssImportKind::Waypoints, point);
	QCOMPARE(int(waypoints.objects.size()), 2);
	QCOMPARE(waypoints.objects[0]->getTag(QStringLiteral("name")), QStringLiteral("CAR"));
	QCOMPARE(waypoints.objects[1]->getTag(QStringLiteral("name")), QStringLiteral("TOP"));

	auto positions = gnssObjects(map, data, GnssImportKind::Positions, point);
	QCOMPARE(int(positions.objects.size()), 4);
	auto const& tagged = *positions.objects.front();
	QCOMPARE(tagged.getTag(QStringLiteral("time")), QStringLiteral("2026-11-22T10:00:00Z"));
	QCOMPARE(tagged.getTag(QStringLiteral("ele")), QStringLiteral("450.5"));
	QCOMPARE(tagged.getTag(QStringLiteral("fix")), QStringLiteral("1"));
	QCOMPARE(tagged.getTag(QStringLiteral("satellites")), QStringLiteral("8"));
	QCOMPARE(tagged.getTag(QStringLiteral("hdop")), QStringLiteral("0.9"));

	// The wrong symbols.
	auto wrong = gnssObjects(map, data, GnssImportKind::Track, point);
	QVERIFY(wrong.objects.empty());
	QVERIFY(!wrong.error.isEmpty());
	wrong = gnssObjects(map, data, GnssImportKind::Positions, line);
	QVERIFY(wrong.objects.empty());
	QVERIFY(!wrong.error.isEmpty());

	// A map which is not georeferenced.
	Map local;
	local.setScaleDenominator(10000);
	auto* local_line = new LineSymbol();
	local.addSymbol(local_line, 0);
	auto none = gnssObjects(local, data, GnssImportKind::Track, local_line);
	QVERIFY(none.objects.empty());
	QVERIFY(none.error.contains(QStringLiteral("georeferenced")));
}


/// Accept (3)
void GnssFileTest::editorTest()
{
	auto* map = new Map();
	fillMap(*map);
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);

	auto* action = editor->getAction("gps-import-file");
	QVERIFY(action);
	auto in_gps_menu = false;
	for (auto* widget : action->associatedWidgets())
	{
		if (auto* menu = qobject_cast<QMenu*>(widget))
			in_gps_menu = in_gps_menu || menu->title() == QStringLiteral("GPS");
	}
	QVERIFY(in_gps_menu);

	auto const objects = map->getNumObjects();
	QString error;
	QCOMPARE(editor->importGnssFile(walkPath(), GnssImportKind::Positions, map->getSymbol(1), &error), 4);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(map->getNumObjects(), objects + 4);
	QCOMPARE(map->getNumSelectedObjects(), 4);
	QVERIFY(map->undoManager().canUndo());
	map->undoManager().undo();
	QCOMPARE(map->getNumObjects(), objects);

	QCOMPARE(editor->importGnssFile(walkPath(), GnssImportKind::Track, map->getSymbol(1), &error), 0);
	QVERIFY(!error.isEmpty());

	window->deleteLater();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}


QTEST_MAIN(GnssFileTest)
#include "gnss_file_t.moc"  // IWYU pragma: keep
