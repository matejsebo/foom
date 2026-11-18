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


#include <algorithm>
#include <cstddef>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGeoPositionInfoSource>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QSettings>
#include <QStaticPlugin>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
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
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "gui/main_window.h"
#include "gui/util_gui.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/symbol_widget.h"
#include "sensors/fix_gate.h"
#include "sensors/gps_display.h"
#include "sensors/gps_path_recorder.h"
#include "sensors/gps_tail.h"
#include "sensors/nmea_fix.h"
#include "sensors/nmea_position_plugin.h"
#include "sensors/nmea_source_settings.h"
#include "tools/tool.h"

Q_IMPORT_PLUGIN(NmeaPositionPlugin)

using namespace OpenOrienteering;


namespace {

QString sourceName()
{
	return QStringLiteral("NMEA (OpenOrienteering)");
}

/// Returns the sentence with its checksum and line end for the text between $ and *.
QByteArray sentence(const QByteArray& body)
{
	int checksum = 0;
	for (auto c : body)
		checksum ^= c;
	return QByteArray("$") + body + QByteArray("*") + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper() + QByteArray("\r\n");
}

QByteArray rmc(const char* time, const char* latitude)
{
	return sentence(QByteArray("GPRMC,") + time + ",A," + latitude + ",N,10500.00000,W,0.0,,181126,,,A");
}

QByteArray gga(const char* time, const char* latitude, const char* satellites)
{
	return sentence(QByteArray("GPGGA,") + time + "," + latitude + ",N,10500.00000,W,2," + satellites + ",0.9,362.4,M,47.0,M,,");
}

/// A server on this computer which plays the receiver.
struct Receiver
{
	QTcpServer server;
	QTcpSocket* client = nullptr;
	
	bool listen()
	{
		return server.listen(QHostAddress::LocalHost);
	}
	
	QString address() const
	{
		return QStringLiteral("127.0.0.1:%1").arg(server.serverPort());
	}
	
	bool accept()
	{
		if (!server.hasPendingConnections())
			return false;
		client = server.nextPendingConnection();
		return client != nullptr;
	}
	
	void send(const QByteArray& data)
	{
		client->write(data);
		client->flush();
	}
};

bool near(const MapCoordF& actual, double x, double y, double tolerance = 1e-9)
{
	return qAbs(actual.x() - x) <= tolerance && qAbs(actual.y() - y) <= tolerance;
}

QString listed(const std::vector<MapCoordF>& points)
{
	QString result;
	for (auto const& point : points)
		result += QStringLiteral("(%1, %2) ").arg(point.x()).arg(point.y());
	return result;
}

void setGeoreferencing(Map& map)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setGeographicRefPoint(LatLon(50.0, -105.0));
	map.setGeoreferencing(georef);
}

}  // namespace



/**
 * @test Tests the antenna height, the averaged vertex and the tail of
 *       the GPS position.
 *
 * the design notes, "Antenna height, averaged vertex and tail".
 */
class GnssExtrasTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void tailTest();
	void averageTest();
	void settingsTest();
	void displayTest();
	void editorTest();
	
private:
	QTemporaryDir settings_dir;
};


void GnssExtrasTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	// The settings of this test go to a file in a temporary folder.
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	
	QVERIFY(QGeoPositionInfoSource::availableSources().contains(sourceName()));
}


void GnssExtrasTest::tailTest()
{
	GpsTail tail;
	QCOMPARE(tail.maxLength(), 0.0);
	tail.add(MapCoordF(0, 0));
	QVERIFY(tail.points().empty());
	
	tail.setMaxLength(3);
	tail.add(MapCoordF(0, 0));
	tail.add(MapCoordF(1, 0));
	tail.add(MapCoordF(2, 0));
	QCOMPARE(int(tail.points().size()), 3);
	QCOMPARE(tail.length(), 2.0);
	
	tail.add(MapCoordF(4, 0));
	QVERIFY2(tail.points().size() == 3, qPrintable(listed(tail.points())));
	QVERIFY(near(tail.points()[0], 1, 0));
	QVERIFY(near(tail.points()[1], 2, 0));
	QVERIFY(near(tail.points()[2], 4, 0));
	QCOMPARE(tail.length(), 3.0);
	
	// The cut falls inside a piece.
	tail.add(MapCoordF(4, 2));
	QVERIFY2(tail.points().size() == 3, qPrintable(listed(tail.points())));
	QVERIFY(near(tail.points()[0], 3, 0));
	QVERIFY(near(tail.points()[1], 4, 0));
	QVERIFY(near(tail.points()[2], 4, 2));
	QVERIFY(qAbs(tail.length() - 3.0) < 1e-9);
	QVERIFY(qAbs(tail.opacityAt(2) - 1.0) < 1e-9);
	QVERIFY(qAbs(tail.opacityAt(1) - 1.0 / 3.0) < 1e-9);
	QVERIFY(qAbs(tail.opacityAt(0)) < 1e-9);
	
	// The same position again adds nothing.
	tail.add(MapCoordF(4, 2));
	QCOMPARE(int(tail.points().size()), 3);
	
	// A jump which is longer than the tail.
	tail.add(MapCoordF(20, 2));
	QVERIFY2(tail.points().size() == 2, qPrintable(listed(tail.points())));
	QVERIFY(near(tail.points()[0], 17, 2));
	QVERIFY(near(tail.points()[1], 20, 2));
	
	tail.setMaxLength(1);
	QVERIFY2(tail.points().size() == 2, qPrintable(listed(tail.points())));
	QVERIFY(near(tail.points()[0], 19, 2));
	QVERIFY(near(tail.points()[1], 20, 2));
	
	tail.clear();
	QVERIFY(tail.points().empty());
	QCOMPARE(tail.length(), 0.0);
	
	tail.add(MapCoordF(1, 1));
	QCOMPARE(int(tail.points().size()), 1);
	tail.setMaxLength(0);
	QVERIFY(tail.points().empty());
}


void GnssExtrasTest::averageTest()
{
	GpsPositionAverage average;
	QVERIFY(average.isEmpty());
	QCOMPARE(average.count(), 0);
	
	average.add(MapCoordF(0, 0), 1.0f);
	average.add(MapCoordF(2, 0), 1.0f);
	QVERIFY(!average.isEmpty());
	QCOMPARE(average.count(), 2);
	QVERIFY(near(average.mean(), 1, 0));
	
	average.clear();
	QVERIFY(average.isEmpty());
	
	// The weight is 1 / max(0.5 m, accuracy): 2 and 0.5.
	average.add(MapCoordF(0, 0), 0.25f);
	average.add(MapCoordF(3, 0), 2.0f);
	QVERIFY(near(average.mean(), 0.6, 0));
	
	// An unknown accuracy weighs as 1 m.
	GpsPositionAverage unknown;
	unknown.add(MapCoordF(0, 4), -1.0f);
	unknown.add(MapCoordF(0, 0), 1.0f);
	QVERIFY(near(unknown.mean(), 0, 2));
}


void GnssExtrasTest::settingsTest()
{
	auto const path = settings_dir.filePath(QStringLiteral("display.ini"));
	{
		QSettings file(path, QSettings::IniFormat);
		auto const defaults = GpsDisplaySettings::load(file);
		QCOMPARE(defaults.antenna_height, 0.0);
		QCOMPARE(defaults.tail_length, 30.0);
		QVERIFY(defaults == GpsDisplaySettings{});
		
		GpsDisplaySettings settings;
		settings.antenna_height = 1.85;
		settings.tail_length = 120.0;
		settings.save(file);
	}
	{
		QSettings file(path, QSettings::IniFormat);
		auto const settings = GpsDisplaySettings::load(file);
		QCOMPARE(settings.antenna_height, 1.85);
		QCOMPARE(settings.tail_length, 120.0);
		
		file.setValue(QStringLiteral("Sensors/antenna_height"), -1.0);
		file.setValue(QStringLiteral("Sensors/tail_length"), -5.0);
		QVERIFY(GpsDisplaySettings::load(file) == GpsDisplaySettings{});
		file.setValue(QStringLiteral("Sensors/tail_length"), 0.0);
		QCOMPARE(GpsDisplaySettings::load(file).tail_length, 0.0);
	}
	
	QWidget page;
	auto* layout = new QFormLayout(&page);
	GpsDisplayRows rows(layout, &page);
	auto* antenna_box = page.findChild<QDoubleSpinBox*>(QStringLiteral("antenna-height"));
	auto* tail_box = page.findChild<QDoubleSpinBox*>(QStringLiteral("tail-length"));
	QVERIFY(antenna_box && tail_box);
	QVERIFY(rows.settings() == GpsDisplaySettings{});
	
	GpsDisplaySettings settings;
	settings.antenna_height = 2.0;
	settings.tail_length = 50.0;
	rows.setSettings(settings);
	QCOMPARE(antenna_box->value(), 2.0);
	QCOMPARE(tail_box->value(), 50.0);
	antenna_box->setValue(1.5);
	tail_box->setValue(0.0);
	QCOMPARE(rows.settings().antenna_height, 1.5);
	QCOMPARE(rows.settings().tail_length, 0.0);
}


void GnssExtrasTest::displayTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	
	Map map;
	setGeoreferencing(map);
	QCOMPARE(map.getGeoreferencing().getState(), Georeferencing::Geospatial);
	
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = receiver.address();
	nmea.save();
	
	MapView view(&map);
	MapWidget widget(false, false);
	widget.setMapView(&view);
	widget.resize(600, 600);
	view.setZoom(32);
	view.setCenter(MapCoord(0.0, -1.0));
	
	GPSDisplay display(&widget, map.getGeoreferencing());
	QVERIFY(display.findChild<QGeoPositionInfoSource*>());
	auto gate = FixGateSettings{};
	display.setFixGate(gate);
	QCOMPARE(display.antennaHeight(), 0.0);
	display.setAntennaHeight(1.8);
	display.setTailLength(30);
	QCOMPARE(display.tail().maxLength(), 3.0);
	display.setVisible(true);
	
	auto checked = 0;
	auto passed = 0;
	auto last_verdict = -1;
	auto last_altitude = 0.0;
	connect(&display, &GPSDisplay::fixChecked, this, [&](const FixInfo&, int verdict) {
		++checked;
		last_verdict = verdict;
	});
	connect(&display, &GPSDisplay::latLonUpdated, this, [&](double, double, double altitude, float) {
		++passed;
		last_altitude = altitude;
	});
	
	display.startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	// A second in which only RMC arrives: there is no altitude.
	receiver.send(rmc("101500.00", "5000.00000"));
	QTRY_COMPARE(passed, 1);
	QCOMPARE(last_altitude, -9999.0);
	
	// The altitude of the antenna, less its height.
	receiver.send(rmc("101501.00", "5000.00000") + gga("101501.00", "5000.00000", "09"));
	QTRY_COMPARE(passed, 2);
	QVERIFY2(qAbs(last_altitude - 360.6) < 1e-6, qPrintable(QString::number(last_altitude, 'f', 6)));
	QCOMPARE(display.latestFix().altitude, 362.4);
	// The receiver stood still.
	QVERIFY2(display.tail().points().size() == 1, qPrintable(listed(display.tail().points())));
	
	// Two steps of 10 m to the north.
	receiver.send(rmc("101502.00", "5000.00539") + gga("101502.00", "5000.00539", "09"));
	QTRY_COMPARE(passed, 3);
	receiver.send(rmc("101503.00", "5000.01078") + gga("101503.00", "5000.01078", "09"));
	QTRY_COMPARE(passed, 4);
	{
		auto const& points = display.tail().points();
		QVERIFY2(points.size() == 3, qPrintable(listed(points)));
		QVERIFY2(near(points[0], 0, 0, 0.02), qPrintable(listed(points)));
		QVERIFY2(near(points[1], 0, -1, 0.02), qPrintable(listed(points)));
		QVERIFY2(near(points[2], 0, -2, 0.02), qPrintable(listed(points)));
	}
	
	auto paint = [&]() {
		QImage image(widget.size(), QImage::Format_RGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		display.paint(&painter);
		painter.end();
		return image;
	};
	// The reddest pixel at a place, and next to it.
	auto colorAt = [&](const QImage& image, const QPointF& viewport_pos) {
		auto const centre = viewport_pos.toPoint();
		auto result = QColor(Qt::white);
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				auto const c = QColor(image.pixel(centre.x() + dx, centre.y() + dy));
				if (c.green() < result.green())
					result = c;
			}
		}
		return result;
	};
	auto const head = widget.mapToViewport(MapCoordF(0, -2));
	auto const behind_5m = widget.mapToViewport(MapCoordF(0, -1.5));
	auto const behind_17m = widget.mapToViewport(MapCoordF(0, -0.25));
	auto const beyond = widget.mapToViewport(MapCoordF(0, 0.3));
	// On the eastern arm of the marker's cross.
	auto const on_marker = head + QPointF(Util::mmToPixelLogical(7.5), 0);
	
	auto image = paint();
	auto const strong = colorAt(image, behind_5m);
	auto const pale = colorAt(image, behind_17m);
	auto const describe = [](const QColor& c) { return QStringLiteral("(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue()); };
	QVERIFY2(strong.red() > 245 && strong.green() < 90, qPrintable(describe(strong)));
	QVERIFY2(pale.red() > 245 && pale.green() > strong.green() + 50 && pale.green() < 200, qPrintable(describe(pale) + describe(strong)));
	QCOMPARE(colorAt(image, beyond), QColor(Qt::white));
	QVERIFY(colorAt(image, on_marker).green() < 200);
	
	// A fix which fails, with failing fixes hidden: the tail stays, the marker goes.
	gate.policy = FixGateSettings::Hide;
	display.setFixGate(gate);
	auto const before = checked;
	receiver.send(rmc("101504.00", "5000.01617") + gga("101504.00", "5000.01617", "03"));
	QTRY_VERIFY(checked > before && last_verdict == int(FixGate::Satellites));
	QCOMPARE(passed, 4);
	QCOMPARE(int(display.tail().points().size()), 3);
	image = paint();
	QVERIFY(colorAt(image, behind_5m).green() < 90);
	QCOMPARE(colorAt(image, on_marker), QColor(Qt::white));
	
	// The next good fix starts the tail anew: it does not bridge the gap.
	receiver.send(rmc("101505.00", "5000.02156") + gga("101505.00", "5000.02156", "09"));
	QTRY_COMPARE(passed, 5);
	QVERIFY2(display.tail().points().size() == 1, qPrintable(listed(display.tail().points())));
	receiver.send(rmc("101506.00", "5000.02695") + gga("101506.00", "5000.02695", "09"));
	QTRY_COMPARE(passed, 6);
	QVERIFY2(display.tail().points().size() == 2, qPrintable(listed(display.tail().points())));
	QVERIFY2(near(display.tail().points()[0], 0, -4, 0.03), qPrintable(listed(display.tail().points())));
	// Look at the new tail: its piece is painted, and nothing where a bridge over the gap would run.
	view.setCenter(MapCoord(0.0, -4.5));
	auto const restart_sample = widget.mapToViewport(MapCoordF(0, -4.25));
	auto const bridge_sample = widget.mapToViewport(MapCoordF(0, -3.5));
	image = paint();
	QVERIFY2(colorAt(image, restart_sample).green() < 120, qPrintable(describe(colorAt(image, restart_sample))));
	QCOMPARE(colorAt(image, bridge_sample), QColor(Qt::white));
	
	// No tail.
	display.setTailLength(0);
	QVERIFY(display.tail().points().empty());
	image = paint();
	QCOMPARE(colorAt(image, restart_sample), QColor(Qt::white));
	
	// Stopping the updates empties the tail.
	display.setTailLength(30);
	receiver.send(rmc("101507.00", "5000.03234") + gga("101507.00", "5000.03234", "09"));
	QTRY_COMPARE(passed, 7);
	QCOMPARE(int(display.tail().points().size()), 1);
	display.stopUpdates();
	QVERIFY(display.tail().points().empty());
	
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
}


void GnssExtrasTest::editorTest()
{
	// The GPS display gets a source which never delivers: a network
	// receiver on a port on which nothing listens.
	quint16 port = 0;
	{
		QTcpServer probe;
		QVERIFY(probe.listen(QHostAddress::LocalHost));
		port = probe.serverPort();
	}
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = QStringLiteral("127.0.0.1:%1").arg(port);
	nmea.save();
	
	auto* map = new Map();
	map->setScaleDenominator(10000);
	setGeoreferencing(*map);
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
	
	auto* display_act = editor->getAction("gpsdisplay");
	auto* path_act = editor->getAction("drawpath");
	auto* record_act = editor->getAction("gps-record");
	auto* average_act = editor->getAction("gps-average-vertex");
	QVERIFY(display_act && path_act && record_act && average_act);
	QVERIFY(average_act->isCheckable());
	
	// Neither the display nor the tool; then the display alone; then both.
	QVERIFY(!average_act->isEnabled());
	display_act->trigger();
	QVERIFY(display_act->isChecked());
	QVERIFY(!average_act->isEnabled());
	path_act->trigger();
	QVERIFY(editor->getTool() && editor->getTool()->acceptsNumericVertices());
	QVERIFY(average_act->isEnabled());
	QVERIFY(!average_act->isChecked());
	
	record_act->trigger();
	QVERIFY(record_act->isChecked());
	editor->gpsRecordPosition(MapCoordF(0, 0), 1.0f);
	QVERIFY(editor->getTool()->editingInProgress());
	
	// While averaging, positions go into the mean and not into the path.
	average_act->trigger();
	QVERIFY(average_act->isChecked());
	editor->gpsRecordPosition(MapCoordF(2, 0), 1.0f);
	editor->gpsRecordPosition(MapCoordF(4, 0), 1.0f);
	average_act->trigger();
	QVERIFY(!average_act->isChecked());
	QVERIFY(record_act->isChecked());
	// Too near to the averaged vertex, from which the recording measures.
	editor->gpsRecordPosition(MapCoordF(3.1, 0), 1.0f);
	
	// Averaging with no position adds nothing.
	average_act->trigger();
	QVERIFY(average_act->isChecked());
	average_act->trigger();
	QVERIFY(!average_act->isChecked());
	
	editor->gpsRecordPosition(MapCoordF(6, 0), 1.0f);
	
	auto const* part = map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 0);
	{
		// Return alone closes the path before it finishes it.
		QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
		editor->getTool()->keyPressEvent(&finish);
	}
	QCOMPARE(part->getNumObjects(), 1);
	{
		auto const& coords = part->getObject(0)->getRawCoordinateVector();
		QString vertices;
		for (auto const& coord : coords)
			vertices += QStringLiteral("(%1, %2) ").arg(coord.x()).arg(coord.y());
		QVERIFY2(coords.size() == 3, qPrintable(vertices));
		QCOMPARE(coords[0], MapCoord(0, 0));
		QCOMPARE(coords[1], MapCoord(3, 0));
		QCOMPARE(coords[2], MapCoord(6, 0));
	}
	
	// Averaging for the first vertex of a path, given up by another tool.
	QVERIFY(average_act->isEnabled());
	average_act->trigger();
	QVERIFY(average_act->isChecked());
	editor->gpsRecordPosition(MapCoordF(10, 10), 1.0f);
	QVERIFY(!editor->getTool()->editingInProgress());
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!average_act->isChecked());
	QVERIFY(!average_act->isEnabled());
	QCOMPARE(part->getNumObjects(), 1);
	
	// Averaging which is ended in a path: the path is finished by the mouse meanwhile.
	path_act->trigger();
	QVERIFY(average_act->isEnabled());
	editor->gpsAddVertexAt(MapCoordF(20, 20));
	editor->gpsAddVertexAt(MapCoordF(22, 20));
	QVERIFY(editor->getTool()->editingInProgress());
	average_act->trigger();
	editor->gpsRecordPosition(MapCoordF(30, 30), 1.0f);
	{
		QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
		editor->getTool()->keyPressEvent(&finish);
	}
	QCOMPARE(part->getNumObjects(), 2);
	QVERIFY(!average_act->isChecked());
	QCOMPARE(int(part->getObject(1)->getRawCoordinateVector().size()), 2);
	QVERIFY(!editor->getTool()->editingInProgress());
	
	// And by switching the GPS display off.
	average_act->trigger();
	QVERIFY(average_act->isChecked());
	display_act->trigger();
	QVERIFY(!display_act->isChecked());
	QVERIFY(!average_act->isChecked());
	QVERIFY(!average_act->isEnabled());
	QVERIFY(!editor->getTool()->editingInProgress());
	
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
	window->deleteLater();
}


QTEST_MAIN(GnssExtrasTest)
#include "gnss_extras_t.moc"  // IWYU pragma: keep
