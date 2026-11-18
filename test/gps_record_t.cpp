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


#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QObject>
#include <QSettings>
#include <QStaticPlugin>
#include <QString>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QWidget>

#include "global.h"
#include "settings.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/widgets/symbol_widget.h"
#include "sensors/gps_path_recorder.h"
#include "sensors/nmea_position_plugin.h"
#include "sensors/nmea_source_settings.h"
#include "tools/tool.h"

Q_IMPORT_PLUGIN(NmeaPositionPlugin)

using namespace OpenOrienteering;


/**
 * @test Tests the recording of GPS positions into the path which is drawn.
 *
 * the design notes, "Recording into a symbol".
 */
class GpsRecordTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void recorderTest();
	void settingsTest();
	void editorTest();
	
private:
	QTemporaryDir settings_dir;
};


void GpsRecordTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	// The settings of this test go to a file in a temporary folder.
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void GpsRecordTest::recorderTest()
{
	// 1:10,000, so that 2 m are 0.2 mm on the map.
	GpsPathRecorder recorder(2.0, 10000);
	QCOMPARE(recorder.count(), 0);
	QVERIFY(recorder.accept(MapCoordF(0, 0)));
	QVERIFY(!recorder.accept(MapCoordF(0.1, 0)));
	QVERIFY(recorder.accept(MapCoordF(0.25, 0)));
	QCOMPARE(recorder.count(), 2);
	
	QVERIFY(!recorder.isPaused());
	recorder.setPaused(true);
	QVERIFY(recorder.isPaused());
	QVERIFY(!recorder.accept(MapCoordF(5, 0)));
	QCOMPARE(recorder.count(), 2);
	recorder.setPaused(false);
	QVERIFY(recorder.accept(MapCoordF(5, 0)));
	
	// A vertex which was added by hand is the one to measure from.
	recorder.take(MapCoordF(5.05, 0));
	QCOMPARE(recorder.count(), 4);
	QVERIFY(!recorder.accept(MapCoordF(5.2, 0)));
	QVERIFY(recorder.accept(MapCoordF(5.3, 0)));
	QCOMPARE(recorder.count(), 5);
	
	// The distance is measured in every direction.
	QVERIFY(!recorder.accept(MapCoordF(5.4, 0.1)));
	QVERIFY(recorder.accept(MapCoordF(5.45, 0.15)));
	
	// Without a minimum distance every position is taken.
	GpsPathRecorder every(0.0, 10000);
	QVERIFY(every.accept(MapCoordF(1, 1)));
	QVERIFY(every.accept(MapCoordF(1, 1)));
	QCOMPARE(every.count(), 2);
	
	// The scale counts: at 1:1,000 the 0.25 mm are 0.25 m.
	GpsPathRecorder large(2.0, 1000);
	QVERIFY(large.accept(MapCoordF(0, 0)));
	QVERIFY(!large.accept(MapCoordF(0.25, 0)));
	QVERIFY(large.accept(MapCoordF(2.5, 0)));
}


/// Accept (2)
void GpsRecordTest::settingsTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	
	QCOMPARE(GpsRecordSettings{}.min_distance, 2.0);
	GpsRecordSettings record;
	record.min_distance = 7.5;
	{
		QSettings ini(dir.filePath(QStringLiteral("record.ini")), QSettings::IniFormat);
		record.save(ini);
		QVERIFY(ini.contains(QStringLiteral("Sensors/record_min_distance")));
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("record.ini")), QSettings::IniFormat);
		QCOMPARE(GpsRecordSettings::load(ini).min_distance, 7.5);
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("empty.ini")), QSettings::IniFormat);
		QCOMPARE(GpsRecordSettings::load(ini).min_distance, 2.0);
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("odd.ini")), QSettings::IniFormat);
		ini.setValue(QStringLiteral("Sensors/record_min_distance"), -1);
		QCOMPARE(GpsRecordSettings::load(ini).min_distance, 2.0);
	}
	
	QWidget page;
	auto* layout = new QFormLayout(&page);
	GpsRecordRows rows(layout, &page);
	auto* distance_box = page.findChild<QDoubleSpinBox*>(QStringLiteral("record-min-distance"));
	QVERIFY(distance_box);
	QCOMPARE(rows.settings().min_distance, 2.0);
	rows.setSettings(record);
	QCOMPARE(distance_box->value(), 7.5);
	distance_box->setValue(0);
	QCOMPARE(rows.settings().min_distance, 0.0);
}


/// Accept (3) and (4)
void GpsRecordTest::editorTest()
{
	// The GPS display gets a source which never delivers: a network
	// receiver on a port on which nothing listens.
	quint16 port = 0;
	{
		QTcpServer probe;
		QVERIFY(probe.listen(QHostAddress::LocalHost));
		port = probe.serverPort();
	}
	Settings::getInstance().setPositionSource(QStringLiteral("NMEA (OpenOrienteering)"));
	NmeaSourceSettings nmea;
	nmea.tcp = QStringLiteral("127.0.0.1:%1").arg(port);
	nmea.save();
	
	auto* map = new Map();
	map->setScaleDenominator(10000);
	{
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
		georef.setGeographicRefPoint(LatLon(50.0, -105.0));
		map->setGeoreferencing(georef);
	}
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map->addColor(black, 0);
	auto* line = new LineSymbol();
	line->setLineWidth(0.5);
	line->setColor(black);
	map->addSymbol(line, 0);
	
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	editor->getSymbolWidget()->selectSingleSymbol(line);
	// Choosing a line symbol may bring the path tool with it; start from the edit tool.
	editor->setEditTool();
	// The tool which was replaced unticks its action when it is deleted.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(editor->getTool() && !editor->getTool()->acceptsNumericVertices());
	
	auto* display_act = editor->getAction("gpsdisplay");
	auto* path_act = editor->getAction("drawpath");
	auto* record_act = editor->getAction("gps-record");
	auto* pause_act = editor->getAction("gps-record-pause");
	auto* vertex_act = editor->getAction("gps-add-vertex");
	QVERIFY(display_act && path_act && record_act && pause_act && vertex_act);
	QVERIFY(record_act->isCheckable());
	QVERIFY(pause_act->isCheckable());
	QVERIFY(display_act->isEnabled());
	
	// Neither the display nor the tool.
	QVERIFY(!record_act->isEnabled());
	QVERIFY(!pause_act->isEnabled());
	QVERIFY(!vertex_act->isEnabled());
	
	// The display, but no tool which takes vertices.
	display_act->trigger();
	QVERIFY(display_act->isChecked());
	QVERIFY(!record_act->isEnabled());
	QVERIFY(!vertex_act->isEnabled());
	
	// Both.
	QVERIFY(path_act->isEnabled());
	path_act->trigger();
	QVERIFY(editor->getTool() && editor->getTool()->acceptsNumericVertices());
	QVERIFY(record_act->isEnabled());
	QVERIFY(vertex_act->isEnabled());
	QVERIFY(!pause_act->isEnabled());
	
	// A position which arrives while nothing is recorded adds nothing.
	editor->gpsRecordPosition(MapCoordF(-5, -5), 1.0f);
	QVERIFY(!editor->getTool()->editingInProgress());
	
	record_act->trigger();
	QVERIFY(record_act->isChecked());
	QVERIFY(pause_act->isEnabled());
	QVERIFY(!pause_act->isChecked());
	
	editor->gpsRecordPosition(MapCoordF(0, 0), 1.0f);
	QVERIFY(editor->getTool()->editingInProgress());
	editor->gpsRecordPosition(MapCoordF(0.1, 0), 1.0f);
	editor->gpsRecordPosition(MapCoordF(1, 0), 1.0f);
	pause_act->trigger();
	QVERIFY(pause_act->isChecked());
	editor->gpsRecordPosition(MapCoordF(5, 0), 1.0f);
	pause_act->trigger();
	QVERIFY(!pause_act->isChecked());
	editor->gpsRecordPosition(MapCoordF(5, 0), 1.0f);
	editor->gpsAddVertexAt(MapCoordF(5.05, 0));
	// Too near to the vertex which was added by hand.
	editor->gpsRecordPosition(MapCoordF(5.1, 0), 1.0f);
	
	auto const* part = map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 0);
	{
		// Return alone closes the path before it finishes it.
		QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
		editor->getTool()->keyPressEvent(&finish);
	}
	QCOMPARE(part->getNumObjects(), 1);
	auto const* path = part->getObject(0);
	QCOMPARE(path->getSymbol(), static_cast<const Symbol*>(line));
	auto const& coords = path->getRawCoordinateVector();
	QString listed;
	for (auto const& coord : coords)
		listed += QStringLiteral("(%1, %2) ").arg(coord.x()).arg(coord.y());
	QVERIFY2(coords.size() == 4, qPrintable(listed));
	QCOMPARE(coords[0], MapCoord(0, 0));
	QCOMPARE(coords[1], MapCoord(1, 0));
	QCOMPARE(coords[2], MapCoord(5, 0));
	QCOMPARE(coords[3], MapCoord(5.05, 0.0));
	
	// Finishing the path ended the recording.
	QVERIFY(!record_act->isChecked());
	QVERIFY(record_act->isEnabled());
	QVERIFY(!pause_act->isEnabled());
	editor->gpsRecordPosition(MapCoordF(20, 20), 1.0f);
	QVERIFY(!editor->getTool()->editingInProgress());
	
	// Another tool ends it, too, and finishes the path.
	record_act->trigger();
	editor->gpsRecordPosition(MapCoordF(10, 10), 1.0f);
	editor->gpsRecordPosition(MapCoordF(11, 10), 1.0f);
	editor->setEditTool();
	// The tool which was replaced unticks its action when it is deleted.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!record_act->isChecked());
	QVERIFY(!record_act->isEnabled());
	QVERIFY(!vertex_act->isEnabled());
	QCOMPARE(part->getNumObjects(), 2);
	editor->gpsRecordPosition(MapCoordF(20, 20), 1.0f);
	editor->gpsAddVertexAt(MapCoordF(21, 21));
	QCOMPARE(part->getNumObjects(), 2);
	QVERIFY(!editor->getTool()->editingInProgress());
	
	// And so does switching the GPS display off.
	path_act->trigger();
	QVERIFY(record_act->isEnabled());
	record_act->trigger();
	QVERIFY(record_act->isChecked());
	display_act->trigger();
	QVERIFY(!display_act->isChecked());
	QVERIFY(!record_act->isChecked());
	QVERIFY(!record_act->isEnabled());
	editor->gpsRecordPosition(MapCoordF(30, 30), 1.0f);
	QVERIFY(!editor->getTool()->editingInProgress());
	
	editor->setEditTool();
	// The tool which was replaced unticks its action when it is deleted.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
	window->deleteLater();
}


QTEST_MAIN(GpsRecordTest)
#include "gps_record_t.moc"  // IWYU pragma: keep
