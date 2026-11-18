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


#include <memory>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QHostAddress>
#include <QLineEdit>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStaticPlugin>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantMap>
#include <QWidget>

#include "settings.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "gui/map/map_widget.h"
#include "sensors/fix_gate.h"
#include "sensors/gps_display.h"
#include "sensors/nmea_fix.h"
#include "sensors/nmea_position_plugin.h"
#include "sensors/nmea_source_settings.h"
#include "sensors/nmea_stream_source.h"

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
	return sentence(QByteArray("GPRMC,") + time + ",A," + latitude + ",N,10500.00000,W,0.0,,171126,,,A");
}

QByteArray gga(const char* time, const char* latitude, const char* quality, const char* satellites, const char* hdop)
{
	return sentence(QByteArray("GPGGA,") + time + "," + latitude + ",N,10500.00000,W," + quality + "," + satellites + "," + hdop + ",362.4,M,47.0,M,,");
}

QByteArray gst(const char* time, const char* latitude, const char* longitude)
{
	return sentence(QByteArray("GPGST,") + time + ",0.9,1.6,0.7,35.0," + latitude + "," + longitude + ",2.1");
}

/// A server on this computer which plays the receiver.
struct Receiver
{
	QTcpServer server;
	QTcpSocket* client = nullptr;
	
	bool listen(quint16 port = 0)
	{
		return server.listen(QHostAddress::LocalHost, port);
	}
	
	QString address() const
	{
		return QStringLiteral("127.0.0.1:%1").arg(server.serverPort());
	}
	
	/// Takes the connection of the source, if there is one.
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
	
	void drop()
	{
		client->abort();
		client->deleteLater();
		client = nullptr;
	}
};

FixInfo fixOf(const QObject* source)
{
	return source->property(FixInfo::propertyName()).value<FixInfo>();
}

}  // namespace



/**
 * @test Tests the NMEA source which reads from a network receiver,
 *       its settings, and its way through the plugin to the position display.
 *
 * the design notes, "NMEA source over TCP and on Windows".
 */
class NmeaSourceTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void hostPortTest();
	void tcpTest();
	void lateServerTest();
	void nonsenseTest();
	void settingsTest();
	void rowsTest();
	void displayTest();
	
private:
	QTemporaryDir settings_dir;
};


void NmeaSourceTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	// The settings of this test go to a file in a temporary folder.
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	
	QVERIFY(QGeoPositionInfoSource::availableSources().contains(sourceName()));
}


void NmeaSourceTest::hostPortTest()
{
	QString host;
	quint16 port = 0;
	QVERIFY(NmeaStreamSource::parseHostPort(QStringLiteral("localhost:5017"), &host, &port));
	QCOMPARE(host, QStringLiteral("localhost"));
	QCOMPARE(int(port), 5017);
	QVERIFY(NmeaStreamSource::parseHostPort(QStringLiteral(" 192.168.1.5:2947 "), &host, &port));
	QCOMPARE(host, QStringLiteral("192.168.1.5"));
	QCOMPARE(int(port), 2947);
	QVERIFY(NmeaStreamSource::parseHostPort(QStringLiteral("[::1]:5017"), &host, &port));
	QCOMPARE(host, QStringLiteral("::1"));
	QCOMPARE(int(port), 5017);
	QVERIFY(NmeaStreamSource::parseHostPort(QStringLiteral("localhost:65535"), &host, &port));
	QCOMPARE(int(port), 65535);
	
	for (auto const* text : { "localhost", ":5017", "localhost:0", "localhost:70000", "localhost:x", "", "[::1]", "::1:5017", "localhost:-1" })
		QVERIFY2(!NmeaStreamSource::parseHostPort(QString::fromLatin1(text), &host, &port), text);
}


void NmeaSourceTest::tcpTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	
	QVariantMap parameters;
	parameters.insert(QStringLiteral("nmea.tcp"), receiver.address());
	parameters.insert(QStringLiteral("nmea.reconnect"), 100);
	std::unique_ptr<QGeoPositionInfoSource> source(QGeoPositionInfoSource::createSource(sourceName(), parameters, nullptr));
	QVERIFY(source);
	QVERIFY(qobject_cast<NmeaStreamSource*>(source.get()));
	QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
	
	QSignalSpy positions(source.get(), &QGeoPositionInfoSource::positionUpdated);
	auto errors = 0;
	connect(source.get(), QOverload<QGeoPositionInfoSource::Error>::of(&QGeoPositionInfoSource::error), this, [&errors]() { ++errors; });
	QVERIFY(positions.isValid());
	
	source->startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	receiver.send(rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "4", "14", "0.6") + gst("101500.00", "0.02", "0.03"));
	QTRY_VERIFY(positions.count() >= 1);
	auto last = source->lastKnownPosition();
	QVERIFY(last.isValid());
	QVERIFY(qAbs(last.coordinate().latitude() - 50.0) < 1e-6);
	QVERIFY(qAbs(last.coordinate().longitude() + 105.0) < 1e-6);
	QTRY_COMPARE(fixOf(source.get()).horizontalError(), 0.03);
	auto fix = fixOf(source.get());
	QVERIFY(fix.from_sentences);
	QCOMPARE(fix.fix_class, FixInfo::RtkFixed);
	QCOMPARE(fix.satellites, 14);
	QCOMPARE(fix.hdop, 0.6);
	QCOMPARE(errors, 0);
	
	// The receiver drops the connection. The source says so and comes back.
	receiver.drop();
	QTRY_VERIFY(errors >= 1);
	QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::ClosedError));
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	QTRY_COMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
	
	receiver.send(rmc("101510.00", "5001.00000") + gga("101510.00", "5001.00000", "2", "09", "0.9"));
	QTRY_VERIFY(source->lastKnownPosition().coordinate().latitude() > 50.01);
	QVERIFY(qAbs(source->lastKnownPosition().coordinate().latitude() - (50.0 + 1.0 / 60)) < 1e-6);
	QTRY_COMPARE(fixOf(source.get()).satellites, 9);
	QCOMPARE(fixOf(source.get()).fix_class, FixInfo::Differential);
	
	// After the stop, the source leaves the receiver alone.
	source->stopUpdates();
	QTRY_COMPARE(int(receiver.client->state()), int(QAbstractSocket::UnconnectedState));
	QTest::qWait(300);
	QVERIFY(!receiver.server.hasPendingConnections());
}


void NmeaSourceTest::lateServerTest()
{
	// A port on which nothing listens: taken and given back.
	quint16 port = 0;
	{
		QTcpServer probe;
		QVERIFY(probe.listen(QHostAddress::LocalHost));
		port = probe.serverPort();
	}
	
	QVariantMap parameters;
	parameters.insert(QStringLiteral("nmea.tcp"), QStringLiteral("127.0.0.1:%1").arg(port));
	parameters.insert(QStringLiteral("nmea.reconnect"), 100);
	std::unique_ptr<QGeoPositionInfoSource> source(QGeoPositionInfoSource::createSource(sourceName(), parameters, nullptr));
	QVERIFY(source);
	QSignalSpy positions(source.get(), &QGeoPositionInfoSource::positionUpdated);
	auto errors = 0;
	connect(source.get(), QOverload<QGeoPositionInfoSource::Error>::of(&QGeoPositionInfoSource::error), this, [&errors]() { ++errors; });
	
	source->startUpdates();
	QTRY_VERIFY(errors >= 1);
	QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::ClosedError));
	
	Receiver receiver;
	if (!receiver.listen(port))
		QSKIP("The port was taken by another program");
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	QTRY_COMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
	receiver.send(rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "1", "07", "1.1"));
	QTRY_VERIFY(positions.count() >= 1);
	QVERIFY(qAbs(source->lastKnownPosition().coordinate().latitude() - 50.0) < 1e-6);
	source->stopUpdates();
}


void NmeaSourceTest::nonsenseTest()
{
	QVariantMap parameters;
	parameters.insert(QStringLiteral("nmea.tcp"), QStringLiteral("nonsense"));
	std::unique_ptr<QGeoPositionInfoSource> source(QGeoPositionInfoSource::createSource(sourceName(), parameters, nullptr));
	QVERIFY(source);
	QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::UnknownSourceError));
	QSignalSpy positions(source.get(), &QGeoPositionInfoSource::positionUpdated);
	source->startUpdates();
	QTest::qWait(200);
	QCOMPARE(positions.count(), 0);
	QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::UnknownSourceError));
	source->stopUpdates();
}


void NmeaSourceTest::settingsTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	
	QCOMPARE(NmeaSourceSettings{}.tcp, QString{});
	QCOMPARE(NmeaSourceSettings{}.baud, 4800);
	
	NmeaSourceSettings nmea;
	nmea.tcp = QStringLiteral("192.168.1.5:2947");
	nmea.baud = 38400;
	{
		QSettings ini(dir.filePath(QStringLiteral("nmea.ini")), QSettings::IniFormat);
		nmea.save(ini);
		QVERIFY(ini.contains(QStringLiteral("Sensors/nmea_tcp")));
		QVERIFY(ini.contains(QStringLiteral("Sensors/nmea_baud")));
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("nmea.ini")), QSettings::IniFormat);
		QCOMPARE(NmeaSourceSettings::load(ini), nmea);
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("empty.ini")), QSettings::IniFormat);
		QCOMPARE(NmeaSourceSettings::load(ini), NmeaSourceSettings{});
	}
	{
		// A baud rate which is none gives the default.
		QSettings ini(dir.filePath(QStringLiteral("odd.ini")), QSettings::IniFormat);
		ini.setValue(QStringLiteral("Sensors/nmea_baud"), -5);
		QCOMPARE(NmeaSourceSettings::load(ini).baud, 4800);
	}
	
	auto const parameters = nmea.parameters();
	QCOMPARE(parameters.value(QStringLiteral("nmea.tcp")).toString(), nmea.tcp);
	QCOMPARE(parameters.value(QStringLiteral("nmea.baud")).toInt(), 38400);
	QVERIFY(!NmeaSourceSettings{}.parameters().contains(QStringLiteral("nmea.tcp")));
}


void NmeaSourceTest::rowsTest()
{
	QWidget page;
	auto* layout = new QFormLayout(&page);
	NmeaSourceRows rows(layout, &page);
	
	auto* tcp_edit = page.findChild<QLineEdit*>(QStringLiteral("nmea-tcp"));
	auto* baud_box = page.findChild<QComboBox*>(QStringLiteral("nmea-baud"));
	QVERIFY(tcp_edit);
	QVERIFY(baud_box);
	QCOMPARE(baud_box->count(), 6);
	QCOMPARE(rows.settings(), NmeaSourceSettings{});
	
	NmeaSourceSettings nmea;
	nmea.tcp = QStringLiteral("192.168.1.5:2947");
	nmea.baud = 38400;
	rows.setSettings(nmea);
	QCOMPARE(tcp_edit->text(), nmea.tcp);
	QCOMPARE(baud_box->currentData().toInt(), 38400);
	QCOMPARE(rows.settings(), nmea);
	
	tcp_edit->setText(QStringLiteral("  localhost:5017 "));
	baud_box->setCurrentIndex(baud_box->findData(115200));
	nmea.tcp = QStringLiteral("localhost:5017");
	nmea.baud = 115200;
	QCOMPARE(rows.settings(), nmea);
	
	// A rate which is not in the list is kept.
	nmea.baud = 230400;
	rows.setSettings(nmea);
	QCOMPARE(rows.settings(), nmea);
}


void NmeaSourceTest::displayTest()
{
	Receiver receiver;
	QVERIFY(receiver.listen());
	
	Map map;
	{
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
		georef.setGeographicRefPoint(LatLon(50.0, -105.0));
		map.setGeoreferencing(georef);
	}
	QCOMPARE(map.getGeoreferencing().getState(), Georeferencing::Geospatial);
	
	Settings::getInstance().setPositionSource(sourceName());
	NmeaSourceSettings nmea;
	nmea.tcp = receiver.address();
	nmea.save();
	
	MapView view(&map);
	MapWidget widget(false, false);
	widget.setMapView(&view);
	widget.resize(200, 200);
	
	GPSDisplay display(&widget, map.getGeoreferencing());
	auto* source = display.findChild<QGeoPositionInfoSource*>();
	QVERIFY(source);
	QVERIFY(qobject_cast<NmeaStreamSource*>(source));
	display.setFixGate({});
	
	auto checked = 0;
	auto passed = 0;
	auto last_verdict = -1;
	FixInfo last_fix;
	connect(&display, &GPSDisplay::fixChecked, this, [&](const FixInfo& fix, int verdict) {
		++checked;
		last_fix = fix;
		last_verdict = verdict;
	});
	connect(&display, &GPSDisplay::mapPositionUpdated, this, [&]() { ++passed; });
	
	display.startUpdates();
	QTRY_VERIFY(receiver.server.hasPendingConnections());
	QVERIFY(receiver.accept());
	
	// Three satellites: checked, and held back.
	receiver.send(rmc("101500.00", "5000.00000") + gga("101500.00", "5000.00000", "1", "03", "4.2"));
	QTRY_VERIFY(checked >= 1);
	QCOMPARE(last_verdict, int(FixGate::Satellites));
	QCOMPARE(last_fix.satellites, 3);
	QCOMPARE(passed, 0);
	QVERIFY(!display.hasValidPosition());
	
	// Nine satellites a second later: passed on.
	auto const before = checked;
	receiver.send(rmc("101501.00", "5000.00000") + gga("101501.00", "5000.00000", "2", "09", "0.9"));
	QTRY_VERIFY(checked > before && last_verdict == int(FixGate::Accepted));
	QCOMPARE(last_fix.satellites, 9);
	QCOMPARE(last_fix.fix_class, FixInfo::Differential);
	QVERIFY(passed >= 1);
	QVERIFY(display.hasValidPosition());
	// The receiver stands on the reference point of the map.
	auto const position = display.getLatestGPSCoord();
	QVERIFY(qAbs(position.x()) < 0.01);
	QVERIFY(qAbs(position.y()) < 0.01);
	
	display.stopUpdates();
	Settings::getInstance().setPositionSource({});
	NmeaSourceSettings{}.save();
}


QTEST_MAIN(NmeaSourceTest)
#include "nmea_source_t.moc"  // IWYU pragma: keep
