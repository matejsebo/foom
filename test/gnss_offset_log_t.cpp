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


#include <cmath>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGeoPositionInfoSource>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QSettings>
#include <QSignalSpy>
#include <QStaticPlugin>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantMap>
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
#include "sensors/gps_tail.h"
#include "sensors/nmea_fix.h"
#include "sensors/nmea_log.h"
#include "sensors/nmea_position_plugin.h"
#include "sensors/nmea_source_settings.h"
#include "tools/gps_offset_tool.h"
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

/// A sentence of a type which the source does not use.
QByteArray gsv()
{
	return sentence(QByteArray("GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00"));
}

/// A line with a wrong checksum.
QByteArray broken()
{
	return QByteArray("$GPGGA,101500.00,5000.00000,N,10500.00000,W,2,09,0.9,362.4,M,47.0,M,,*00\r\n");
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

QString shown(const MapCoordF& point)
{
	return QStringLiteral("(%1, %2)").arg(point.x()).arg(point.y());
}

void setGeoreferencing(Map& map)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setGeographicRefPoint(LatLon(50.0, -105.0));
	map.setGeoreferencing(georef);
}

QByteArray contents(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QByteArray("(cannot read ") + path.toUtf8() + ")";
	return file.readAll();
}

/// What the logs of a folder hold, in the order of their days.
/// A test which runs over midnight finds its lines in two files.
QByteArray logged(const QString& folder)
{
	QByteArray result;
	auto const names = QDir(folder).entryList({ QStringLiteral("gnss-*.nmea") }, QDir::Files, QDir::Name);
	for (auto const& name : names)
		result += contents(QDir(folder).filePath(name));
	return result;
}

}  // namespace



/**
 * @test Tests the offset of the GPS position and the log of the
 *       sentences which a receiver sends.
 *
 * the design notes, "Position offset and sentence log".
 */
class GnssOffsetLogTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void logTest();
	void settingsTest();
	void sourceLogTest();
	void displayLogTest();
	void displayOffsetTest();
	void editorTest();
	
private:
	QTemporaryDir settings_dir;
};


void GnssOffsetLogTest::initTestCase()
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


/// Accept (1)
void GnssOffsetLogTest::logTest()
{
	auto const folder = settings_dir.filePath(QStringLiteral("log/of/today"));
	QVERIFY(!QFileInfo::exists(folder));
	auto const day = QDate(2026, 11, 18);
	auto const next_day = QDate(2026, 11, 19);
	QCOMPARE(NmeaLog::fileName(day), QStringLiteral("gnss-2026-11-18.nmea"));
	auto const first_file = QDir(folder).filePath(NmeaLog::fileName(day));
	auto const second_file = QDir(folder).filePath(NmeaLog::fileName(next_day));
	
	QByteArray const lines = rmc("101500.00", "5000.00000") + broken() + gsv();
	{
		NmeaLog log;
		QVERIFY(log.folder().isEmpty());
		// Without a folder nothing is written, and that is no error.
		QVERIFY(log.write(gsv(), day));
		QVERIFY(!log.hasError());
		QVERIFY(log.filePath().isEmpty());
		
		log.setFolder(folder);
		QCOMPARE(log.folder(), folder);
		QVERIFY(log.write(rmc("101500.00", "5000.00000"), day));
		QVERIFY(log.write(broken(), day));
		QVERIFY(log.write(gsv(), day));
		QVERIFY(!log.hasError());
		QCOMPARE(QFileInfo(log.filePath()), QFileInfo(first_file));
		// Every line is in the file before the next one comes.
		QCOMPARE(contents(first_file), lines);
		
		QVERIFY(log.write(gga("000001.00", "5000.00000", "09"), next_day));
		QCOMPARE(QFileInfo(log.filePath()), QFileInfo(second_file));
		QCOMPARE(contents(second_file), gga("000001.00", "5000.00000", "09"));
		QCOMPARE(contents(first_file), lines);
	}
	{
		// A file which exists is added to.
		NmeaLog log;
		log.setFolder(folder);
		QVERIFY(log.write(gga("101501.00", "5000.00000", "09"), day));
		QCOMPARE(contents(first_file), lines + gga("101501.00", "5000.00000", "09"));
		
		// No folder again: the file is left alone.
		log.setFolder({});
		QVERIFY(log.write(gsv(), day));
		QVERIFY(log.filePath().isEmpty());
		QCOMPARE(contents(first_file), lines + gga("101501.00", "5000.00000", "09"));
	}
	{
		// A folder which is in truth a file.
		NmeaLog log;
		log.setFolder(first_file);
		QVERIFY(!log.write(gsv(), day));
		QVERIFY(log.hasError());
		QCOMPARE(QFileInfo(log.filePath()), QFileInfo(QDir(first_file).filePath(NmeaLog::fileName(day))));
		QVERIFY(!log.write(gsv(), day));
		QCOMPARE(contents(first_file), lines + gga("101501.00", "5000.00000", "09"));
		
		// A folder which can be written again mends it.
		log.setFolder(folder);
		QVERIFY(log.write(gsv(), next_day));
		QVERIFY(!log.hasError());
	}
}


/// Accept (2)
void GnssOffsetLogTest::settingsTest()
{
	auto const path = settings_dir.filePath(QStringLiteral("log.ini"));
	{
		QSettings file(path, QSettings::IniFormat);
		QVERIFY(NmeaSourceSettings::load(file).log_folder.isEmpty());
		
		NmeaSourceSettings settings;
		settings.log_folder = QStringLiteral("C:/field/log");
		QVERIFY(settings != NmeaSourceSettings{});
		settings.save(file);
	}
	{
		QSettings file(path, QSettings::IniFormat);
		QCOMPARE(NmeaSourceSettings::load(file).log_folder, QStringLiteral("C:/field/log"));
	}
	
	QWidget page;
	auto* layout = new QFormLayout(&page);
	NmeaSourceRows rows(layout, &page);
	auto* folder_edit = page.findChild<QLineEdit*>(QStringLiteral("nmea-log-folder"));
	QVERIFY(folder_edit);
	QVERIFY(rows.settings().log_folder.isEmpty());
	
	NmeaSourceSettings settings;
	settings.log_folder = QStringLiteral("C:/field/log");
	rows.setSettings(settings);
	QCOMPARE(QDir::fromNativeSeparators(folder_edit->text()), QStringLiteral("C:/field/log"));
	QCOMPARE(rows.settings().log_folder, QStringLiteral("C:/field/log"));
	folder_edit->setText(QStringLiteral("  D:/other  "));
	QCOMPARE(rows.settings().log_folder, QStringLiteral("D:/other"));
	folder_edit->clear();
	QVERIFY(rows.settings().log_folder.isEmpty());
}


/// Accept (3)
void GnssOffsetLogTest::sourceLogTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	auto const folder = settings_dir.filePath(QStringLiteral("source-log"));
	
	QVariantMap parameters;
	parameters.insert(QStringLiteral("nmea.tcp"), receiver.address());
	parameters.insert(QStringLiteral("nmea.reconnect"), 100);
	auto* source = QGeoPositionInfoSource::createSource(sourceName(), parameters, this);
	QVERIFY(source);
	source->setProperty(NmeaLog::folderProperty(), folder);
	
	source->startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	QByteArray const sent = rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "09") + gsv() + broken();
	receiver.send(sent);
	QTRY_COMPARE(logged(folder), sent);
	auto const file = source->property(NmeaLog::fileProperty()).toString();
	QVERIFY(QFileInfo(file).fileName().startsWith(QStringLiteral("gnss-")));
	QCOMPARE(QFileInfo(file).absolutePath(), QFileInfo(folder).absoluteFilePath());
	QVERIFY(source->property(NmeaLog::errorProperty()).toString().isEmpty());
	
	// No folder: what comes now is not written.
	source->setProperty(NmeaLog::folderProperty(), QString{});
	QSignalSpy positions(source, &QGeoPositionInfoSource::positionUpdated);
	receiver.send(rmc("101501.00", "5000.00539") + gga("101501.00", "5000.00539", "09"));
	QTRY_VERIFY(positions.count() > 0);
	QCOMPARE(logged(folder), sent);
	QVERIFY(source->property(NmeaLog::fileProperty()).toString().isEmpty());
	
	source->stopUpdates();
	delete source;
}


/// Accept (4)
void GnssOffsetLogTest::displayLogTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	auto const folder = settings_dir.filePath(QStringLiteral("display-log"));
	
	Map map;
	setGeoreferencing(map);
	
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = receiver.address();
	nmea.log_folder = folder;
	nmea.save();
	
	MapView view(&map);
	MapWidget widget(false, false);
	widget.setMapView(&view);
	widget.resize(600, 600);
	
	GPSDisplay display(&widget, map.getGeoreferencing());
	QVERIFY(display.findChild<QGeoPositionInfoSource*>());
	display.setFixGate(FixGateSettings{});
	
	auto passed = 0;
	connect(&display, &GPSDisplay::mapPositionUpdated, this, [&]() { ++passed; });
	QSignalSpy log_changes(&display, &GPSDisplay::sentenceLogChanged);
	
	display.startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	QByteArray sent = rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "09");
	receiver.send(sent);
	QTRY_COMPARE(passed, 1);
	receiver.send(rmc("101501.00", "5000.00000") + gga("101501.00", "5000.00000", "09"));
	sent += rmc("101501.00", "5000.00000") + gga("101501.00", "5000.00000", "09");
	QTRY_COMPARE(passed, 2);
	QCOMPARE(logged(folder), sent);
	QCOMPARE(log_changes.count(), 1);
	QCOMPARE(QFileInfo(log_changes.at(0).at(0).toString()).absolutePath(), QFileInfo(folder).absoluteFilePath());
	QCOMPARE(log_changes.at(0).at(1).toBool(), true);
	
	// The setting is changed to a place which cannot be a folder.
	auto const blocker = settings_dir.filePath(QStringLiteral("blocker"));
	{
		QFile file(blocker);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("x");
	}
	nmea.log_folder = blocker;
	nmea.save();
	Settings::getInstance().applySettings();
	receiver.send(rmc("101502.00", "5000.00000") + gga("101502.00", "5000.00000", "09"));
	QTRY_COMPARE(passed, 3);
	QCOMPARE(log_changes.count(), 2);
	QVERIFY(log_changes.at(1).at(0).toString().startsWith(blocker));
	QCOMPARE(log_changes.at(1).at(1).toBool(), false);
	QCOMPARE(logged(folder), sent);
	QCOMPARE(contents(blocker), QByteArray("x"));
	
	display.stopUpdates();
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
	Settings::getInstance().applySettings();
}


/// Accept (5)
void GnssOffsetLogTest::displayOffsetTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	
	Map map;
	setGeoreferencing(map);
	auto const& georef = map.getGeoreferencing();
	
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = receiver.address();
	nmea.save();
	
	MapView view(&map);
	MapWidget widget(false, false);
	widget.setMapView(&view);
	widget.resize(600, 600);
	view.setZoom(32);
	view.setCenter(MapCoord(1.5, -2.0));
	
	GPSDisplay display(&widget, georef);
	QVERIFY(display.findChild<QGeoPositionInfoSource*>());
	display.setFixGate(FixGateSettings{});
	display.setAntennaHeight(0);
	display.setTailLength(30);
	display.setVisible(true);
	
	auto checked = 0;
	auto passed = 0;
	auto last_position = MapCoordF{};
	auto last_latitude = 0.0;
	auto last_longitude = 0.0;
	auto last_altitude = 0.0;
	connect(&display, &GPSDisplay::fixChecked, this, [&]() { ++checked; });
	connect(&display, &GPSDisplay::mapPositionUpdated, this, [&](const MapCoordF& position, float) {
		++passed;
		last_position = position;
	});
	connect(&display, &GPSDisplay::latLonUpdated, this, [&](double latitude, double longitude, double altitude, float) {
		last_latitude = latitude;
		last_longitude = longitude;
		last_altitude = altitude;
	});
	QSignalSpy offset_changes(&display, &GPSDisplay::positionOffsetChanged);
	
	// Before a fix there is nothing to compare the true position with.
	QVERIFY(!display.hasPositionOffset());
	QVERIFY(!display.setTruePosition(MapCoordF(3, -2)));
	QVERIFY(!display.hasPositionOffset());
	
	display.startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	// Nor with a fix which fails the gate.
	receiver.send(rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "03"));
	QTRY_VERIFY(checked > 0 && display.latestFix().satellites == 3);
	QVERIFY(!display.hasValidPosition());
	QVERIFY(!display.setTruePosition(MapCoordF(3, -2)));
	QVERIFY(!display.hasPositionOffset());
	QCOMPARE(offset_changes.count(), 0);
	
	receiver.send(rmc("101501.00", "5000.00000") + gga("101501.00", "5000.00000", "09"));
	QTRY_COMPARE(passed, 1);
	QVERIFY2(near(last_position, 0, 0, 0.02), qPrintable(shown(last_position)));
	
	// The receiver stands at (3, -2) in truth.
	QVERIFY(display.setTruePosition(MapCoordF(3, -2)));
	QVERIFY(display.hasPositionOffset());
	QCOMPARE(offset_changes.count(), 1);
	QVERIFY2(near(display.positionOffset(), 3, -2, 0.02), qPrintable(shown(display.positionOffset())));
	QVERIFY2(near(display.getLatestGPSCoord(), 3, -2), qPrintable(shown(display.getLatestGPSCoord())));
	QCOMPARE(int(display.tail().points().size()), 1);
	QVERIFY(near(display.tail().points().front(), 3, -2));
	QCoreApplication::processEvents();
	QCOMPARE(passed, 1);
	
	// 10 m to the north.
	receiver.send(rmc("101502.00", "5000.00539") + gga("101502.00", "5000.00539", "09"));
	QTRY_COMPARE(passed, 2);
	QVERIFY2(near(last_position, 3, -3, 0.02), qPrintable(shown(last_position)));
	QVERIFY(near(display.getLatestGPSCoord(), last_position.x(), last_position.y()));
	QCOMPARE(int(display.tail().points().size()), 2);
	{
		// The latitude and longitude are those of the shifted place: 30 m to
		// the east and 30 m to the north of the reference point.
		auto ok = false;
		auto const expected = georef.toGeographicCoords(last_position, &ok);
		QVERIFY(ok);
		QVERIFY(qAbs(last_latitude - expected.latitude()) < 1e-9);
		QVERIFY(qAbs(last_longitude - expected.longitude()) < 1e-9);
		QVERIFY2(qAbs(last_latitude - 50.00027) < 2e-5, qPrintable(QString::number(last_latitude, 'f', 7)));
		QVERIFY2(qAbs(last_longitude + 104.99958) < 2e-5, qPrintable(QString::number(last_longitude, 'f', 7)));
		QCOMPARE(last_altitude, 362.4);
		QCOMPARE(display.latestFix().altitude, 362.4);
	}
	{
		// The marker is painted at the shifted place and not where the receiver says.
		QImage image(widget.size(), QImage::Format_RGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		display.paint(&painter);
		painter.end();
		auto const arm = QPointF(Util::mmToPixelLogical(7.5), 0);
		auto const shifted = (widget.mapToViewport(MapCoordF(3, -3)) + arm).toPoint();
		auto const raw = (widget.mapToViewport(MapCoordF(0, -1)) + arm).toPoint();
		QVERIFY(QColor(image.pixel(shifted)).green() < 200);
		QCOMPARE(QColor(image.pixel(raw)), QColor(Qt::white));
	}
	
	// A second true position replaces the offset; the two do not add up.
	QVERIFY(display.setTruePosition(MapCoordF(1, -1)));
	QCOMPARE(offset_changes.count(), 2);
	QVERIFY2(near(display.positionOffset(), 1, 0, 0.02), qPrintable(shown(display.positionOffset())));
	QVERIFY(near(display.getLatestGPSCoord(), 1, -1));
	QCOMPARE(int(display.tail().points().size()), 1);
	
	display.clearPositionOffset();
	QVERIFY(!display.hasPositionOffset());
	QCOMPARE(offset_changes.count(), 3);
	QVERIFY2(near(display.getLatestGPSCoord(), 0, -1, 0.02), qPrintable(shown(display.getLatestGPSCoord())));
	// Removing what is not there changes nothing.
	display.clearPositionOffset();
	QCOMPARE(offset_changes.count(), 3);
	
	receiver.send(rmc("101503.00", "5000.00539") + gga("101503.00", "5000.00539", "09"));
	QTRY_COMPARE(passed, 3);
	QVERIFY2(near(last_position, 0, -1, 0.02), qPrintable(shown(last_position)));
	QVERIFY2(qAbs(last_longitude + 105.0) < 1e-9, qPrintable(QString::number(last_longitude, 'f', 9)));
	
	display.stopUpdates();
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
}


/// Accept (6)
void GnssOffsetLogTest::editorTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = receiver.address();
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
	editor->setEditTool();
	// The tool which was replaced unticks its action when it is deleted.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	
	auto* display_act = editor->getAction("gpsdisplay");
	auto* path_act = editor->getAction("drawpath");
	auto* add_act = editor->getAction("gps-add-vertex");
	auto* set_act = editor->getAction("gps-set-offset");
	auto* clear_act = editor->getAction("gps-clear-offset");
	QVERIFY(display_act && path_act && add_act && set_act && clear_act);
	auto const* display = editor->gpsDisplay();
	QVERIFY(display);
	
	auto const offsetToolChosen = [editor]() {
		return qobject_cast<GpsOffsetTool*>(editor->getTool()) != nullptr;
	};
	auto const click = [editor](const MapCoordF& position) {
		auto* tool = editor->getTool();
		auto* widget = editor->getMainWidget();
		auto const point = widget->mapToViewport(position).toPoint();
		QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		tool->mousePressEvent(&press, position, widget);
		QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		tool->mouseReleaseEvent(&release, position, widget);
	};
	auto const message = [window]() {
		return window->statusBar()->currentMessage();
	};
	auto const connectReceiver = [&receiver]() {
		QTRY_VERIFY(receiver.server.hasPendingConnections());
		QVERIFY(receiver.accept());
	};
	
	QVERIFY(!set_act->isEnabled());
	QVERIFY(!clear_act->isEnabled());
	display_act->trigger();
	QVERIFY(display_act->isChecked());
	QVERIFY(set_act->isEnabled());
	QVERIFY(!clear_act->isEnabled());
	connectReceiver();
	QVERIFY(receiver.client);
	
	receiver.send(rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "09"));
	QTRY_VERIFY(display->hasValidPosition());
	QVERIFY2(near(display->getLatestGPSCoord(), 0, 0, 0.02), qPrintable(shown(display->getLatestGPSCoord())));
	
	// The click of the tool.
	set_act->trigger();
	QVERIFY(offsetToolChosen());
	click(MapCoordF(2, 1));
	QVERIFY(!offsetToolChosen());
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!set_act->isChecked());
	QVERIFY(clear_act->isEnabled());
	QVERIFY(display->hasPositionOffset());
	QCOMPARE(message(), QStringLiteral("GPS position offset: 22.4 m"));
	
	// What is drawn from the GPS position has the offset.
	path_act->trigger();
	QVERIFY(editor->getTool() && editor->getTool()->acceptsNumericVertices());
	QVERIFY(add_act->isEnabled());
	add_act->trigger();
	receiver.send(rmc("101501.00", "5000.00539") + gga("101501.00", "5000.00539", "09"));
	QTRY_VERIFY(near(display->getLatestGPSCoord(), 2, 0, 0.02));
	add_act->trigger();
	auto const* part = map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 0);
	{
		QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
		editor->getTool()->keyPressEvent(&finish);
	}
	QCOMPARE(part->getNumObjects(), 1);
	{
		auto const& coords = part->getObject(0)->getRawCoordinateVector();
		QCOMPARE(int(coords.size()), 2);
		QVERIFY2(near(MapCoordF(coords[0]), 2, 1, 0.02), qPrintable(shown(MapCoordF(coords[0]))));
		QVERIFY2(near(MapCoordF(coords[1]), 2, 0, 0.02), qPrintable(shown(MapCoordF(coords[1]))));
	}
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	
	// The offset outlasts the display.
	display_act->trigger();
	QVERIFY(!display_act->isChecked());
	QVERIFY(!set_act->isEnabled());
	QVERIFY(clear_act->isEnabled());
	window->statusBar()->clearMessage();
	display_act->trigger();
	QVERIFY(display_act->isChecked());
	QVERIFY(display->hasPositionOffset());
	QCOMPARE(message(), QStringLiteral("GPS position offset: 22.4 m"));
	connectReceiver();
	receiver.send(rmc("101502.00", "5000.00539") + gga("101502.00", "5000.00539", "09"));
	QTRY_VERIFY(display->hasValidPosition());
	QVERIFY2(near(display->getLatestGPSCoord(), 2, 0, 0.02), qPrintable(shown(display->getLatestGPSCoord())));
	
	clear_act->trigger();
	QVERIFY(!clear_act->isEnabled());
	QVERIFY(!display->hasPositionOffset());
	QVERIFY2(near(display->getLatestGPSCoord(), 0, -1, 0.02), qPrintable(shown(display->getLatestGPSCoord())));
	
	// A fix which fails the gate gives the tool nothing to measure from.
	receiver.send(rmc("101503.00", "5000.00539") + gga("101503.00", "5000.00539", "03"));
	QTRY_VERIFY(display->latestFix().satellites == 3);
	QVERIFY(!display->hasValidPosition());
	set_act->trigger();
	QVERIFY(offsetToolChosen());
	click(MapCoordF(5, 5));
	QVERIFY(offsetToolChosen());
	QVERIFY(!clear_act->isEnabled());
	QVERIFY(!display->hasPositionOffset());
	QCOMPARE(message(), QStringLiteral("There is no GPS position which can be used."));
	
	// Switching the display off ends the tool.
	display_act->trigger();
	QVERIFY(!display_act->isChecked());
	QVERIFY(!offsetToolChosen());
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!set_act->isChecked());
	QVERIFY(!set_act->isEnabled());
	
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
	window->deleteLater();
}


QTEST_MAIN(GnssOffsetLogTest)
#include "gnss_offset_log_t.moc"  // IWYU pragma: keep
