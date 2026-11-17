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

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSettings>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QTime>
#include <QVariant>
#include <QWidget>

#include "test_config.h"

#include "sensors/fix_gate.h"
#include "sensors/fix_gate_rows.h"
#include "sensors/nmea_fix.h"

#if defined(QT_POSITIONING_LIB) && defined(MAPPER_USE_FAKE_POSITION_PLUGIN)
#  include <QGeoCoordinate>
#  include <QGeoPositionInfoSource>
#  include <QStaticPlugin>
#  include "settings.h"
#  include "core/georeferencing.h"
#  include "core/latlon.h"
#  include "core/map.h"
#  include "core/map_coord.h"
#  include "core/map_view.h"
#  include "gui/map/map_widget.h"
#  include "sensors/fake_position_plugin.h"
#  include "sensors/fake_position_source.h"
#  include "sensors/gps_display.h"
Q_IMPORT_PLUGIN(FakePositionPlugin)
#endif

using namespace OpenOrienteering;


namespace {

const char* const reference_gga = "$GNGGA,065901.00,3018.94658,S,13920.03591,E,1,11,0.79,362.4,M,10.3,M,,*50";

/// Returns the sentence with its checksum for the text between $ and *.
QByteArray sentence(const QByteArray& body)
{
	int checksum = 0;
	for (auto c : body)
		checksum ^= c;
	return QByteArray("$") + body + QByteArray("*") + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper();
}

QByteArray gst(const char* time, const char* latitude, const char* longitude)
{
	return sentence(QByteArray("GPGST,") + time + ",0.9,1.6,0.7,35.0," + latitude + "," + longitude + ",2.1");
}

QByteArray gga(const char* time, const char* quality, const char* satellites, const char* hdop)
{
	return sentence(QByteArray("GPGGA,") + time + ",5000.00000,N,10500.00000,W," + quality + "," + satellites + "," + hdop + ",362.4,M,47.0,M,,");
}

FixInfo sentenceFix(FixInfo::Class fix_class, int satellites)
{
	FixInfo fix;
	fix.from_sentences = true;
	fix.fix_class = fix_class;
	fix.satellites = satellites;
	return fix;
}

#if defined(QT_POSITIONING_LIB) && defined(MAPPER_USE_FAKE_POSITION_PLUGIN)

/// The numbers of reddish, greyish and not white pixels.
struct Pixels
{
	int red = 0;
	int grey = 0;
	int any = 0;
};

Pixels countPixels(const QImage& image)
{
	Pixels result;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			auto const c = QColor(image.pixel(x, y));
			if (c.red() - c.green() > 80)
				++result.red;
			else if (c.red() < 215 && qAbs(c.red() - c.green()) < 10 && qAbs(c.green() - c.blue()) < 10)
				++result.grey;
			if (c.red() < 250 || c.green() < 250 || c.blue() < 250)
				++result.any;
		}
	}
	return result;
}

#endif

}  // namespace



/**
 * @test Tests the fix data read from NMEA sentences, the quality gate,
 *       its settings and its effect on the position display.
 *
 * the design notes, "GNSS sources: detail".
 */
class GnssGateTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void ggaTest();
	void gstTest();
	void fixClassTest();
	void gateTest();
	void replayTest();
	void settingsTest();
	void displayTest();
	void rowsTest();
	
private:
	QTemporaryDir settings_dir;
};


void GnssGateTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	// The settings of this test go to a file in a temporary folder.
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	QDir::addSearchPath(QStringLiteral("testdata"), QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data")));
}


void GnssGateTest::ggaTest()
{
	NmeaFixParser parser;
	QCOMPARE(parser.fix().fix_class, FixInfo::Unknown);
	QCOMPARE(parser.fix().satellites, -1);
	QVERIFY(std::isnan(parser.fix().hdop));
	
	QCOMPARE(parser.feed(reference_gga), NmeaFixParser::Gga);
	auto const fix = parser.fix();
	QVERIFY(fix.from_sentences);
	QCOMPARE(fix.fix_class, FixInfo::Autonomous);
	QCOMPARE(fix.satellites, 11);
	QCOMPARE(fix.hdop, 0.79);
	QCOMPARE(fix.altitude, 362.4);
	QCOMPARE(fix.geoid_separation, 10.3);
	QCOMPARE(fix.utc, QTime(6, 59, 1));
	QVERIFY(std::isnan(fix.horizontalError()));
	
	// The line end of the device is no part of the sentence.
	NmeaFixParser other;
	QCOMPARE(other.feed(QByteArray(reference_gga) + "\r\n"), NmeaFixParser::Gga);
	QCOMPARE(other.fix(), fix);
	
	// A changed sentence with the old checksum is refused.
	auto changed = QByteArray(reference_gga);
	changed.replace(",1,11,", ",1,12,");
	QCOMPARE(parser.feed(changed), NmeaFixParser::BadChecksum);
	QCOMPARE(parser.badChecksums(), 1);
	QCOMPARE(parser.fix(), fix);
	
	// So is a sentence without a checksum, and text which is no sentence.
	QCOMPARE(parser.feed("$GNGGA,065901.00,3018.94658,S,13920.03591,E,1,12,0.79,362.4,M,10.3,M,,"), NmeaFixParser::BadChecksum);
	QCOMPARE(parser.feed("hello"), NmeaFixParser::NotASentence);
	QCOMPARE(parser.feed(QByteArray()), NmeaFixParser::NotASentence);
	QCOMPARE(parser.badChecksums(), 2);
	QCOMPARE(parser.fix(), fix);
	
	// Another sentence leaves the fix alone.
	QCOMPARE(parser.feed(sentence("GNVTG,,T,,M,0.341,N,0.631,K,A")), NmeaFixParser::Other);
	QCOMPARE(parser.fix(), fix);
}


void GnssGateTest::gstTest()
{
	NmeaFixParser parser;
	QCOMPARE(parser.feed(gga("101500.00", "4", "14", "0.6")), NmeaFixParser::Gga);
	QVERIFY(std::isnan(parser.fix().horizontalError()));
	
	// After its GGA.
	QCOMPARE(parser.feed(gst("101500.00", "0.8", "1.4")), NmeaFixParser::Gst);
	QCOMPARE(parser.fix().sd_latitude, 0.8);
	QCOMPARE(parser.fix().sd_longitude, 1.4);
	QCOMPARE(parser.fix().horizontalError(), 1.4);
	QCOMPARE(parser.fix().fix_class, FixInfo::RtkFixed);
	
	// A GST of the next second does not change the fix of this one ...
	QCOMPARE(parser.feed(gst("101501.00", "0.02", "0.03")), NmeaFixParser::Gst);
	QCOMPARE(parser.fix().horizontalError(), 1.4);
	// ... and counts for the GGA which follows it.
	QCOMPARE(parser.feed(gga("101501.00", "4", "14", "0.6")), NmeaFixParser::Gga);
	QCOMPARE(parser.fix().horizontalError(), 0.03);
	QCOMPARE(parser.fix().utc, QTime(10, 15, 1));
	
	// A GGA with a later time has no error.
	QCOMPARE(parser.feed(gga("101502.00", "4", "14", "0.6")), NmeaFixParser::Gga);
	QVERIFY(std::isnan(parser.fix().horizontalError()));
	QVERIFY(std::isnan(parser.fix().sd_latitude));
}


void GnssGateTest::fixClassTest()
{
	QCOMPARE(fixClassOfGgaQuality("0"), FixInfo::Invalid);
	QCOMPARE(fixClassOfGgaQuality("1"), FixInfo::Autonomous);
	QCOMPARE(fixClassOfGgaQuality("2"), FixInfo::Differential);
	QCOMPARE(fixClassOfGgaQuality("3"), FixInfo::Autonomous);
	QCOMPARE(fixClassOfGgaQuality("4"), FixInfo::RtkFixed);
	QCOMPARE(fixClassOfGgaQuality("5"), FixInfo::RtkFloat);
	QCOMPARE(fixClassOfGgaQuality("6"), FixInfo::Invalid);
	QCOMPARE(fixClassOfGgaQuality("7"), FixInfo::Invalid);
	QCOMPARE(fixClassOfGgaQuality("8"), FixInfo::Invalid);
	QCOMPARE(fixClassOfGgaQuality("9"), FixInfo::Unknown);
	QCOMPARE(fixClassOfGgaQuality(""), FixInfo::Unknown);
	QCOMPARE(fixClassOfGgaQuality("x"), FixInfo::Unknown);
	
	// From worst to best.
	QVERIFY(FixInfo::Invalid < FixInfo::Autonomous);
	QVERIFY(FixInfo::Autonomous < FixInfo::Differential);
	QVERIFY(FixInfo::Differential < FixInfo::RtkFloat);
	QVERIFY(FixInfo::RtkFloat < FixInfo::RtkFixed);
}


void GnssGateTest::gateTest()
{
	FixGateSettings gate;
	QCOMPARE(gate.min_class, FixInfo::Autonomous);
	QCOMPARE(gate.min_satellites, 4);
	QCOMPARE(gate.max_hdop, 0.0);
	QCOMPARE(gate.max_error, 0.0);
	QCOMPARE(gate.policy, FixGateSettings::ShowGreyed);
	
	// Satellites.
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Autonomous, 4), gate), FixGate::Accepted);
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Autonomous, 3), gate), FixGate::Satellites);
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Autonomous, -1), gate), FixGate::Accepted);
	// Nothing known, nothing fails.
	QCOMPARE(FixGate::check(FixInfo{}, gate), FixGate::Accepted);
	
	// Class.
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Invalid, 9), gate), FixGate::FixClass);
	gate.min_class = FixInfo::Differential;
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Autonomous, 9), gate), FixGate::FixClass);
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Differential, 9), gate), FixGate::Accepted);
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Unknown, 9), gate), FixGate::Accepted);
	gate.min_class = FixInfo::RtkFixed;
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::RtkFloat, 9), gate), FixGate::FixClass);
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::RtkFixed, 9), gate), FixGate::Accepted);
	// Class comes before satellites.
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::RtkFloat, 3), gate), FixGate::FixClass);
	gate.min_class = FixInfo::Invalid;
	QCOMPARE(FixGate::check(sentenceFix(FixInfo::Invalid, 9), gate), FixGate::Accepted);
	
	// HDOP.
	gate = {};
	auto fix = sentenceFix(FixInfo::Differential, 9);
	fix.hdop = 2.5;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Accepted);
	gate.max_hdop = 2.0;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Hdop);
	gate.max_hdop = 2.5;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Accepted);
	
	// Horizontal error from GST.
	gate = {};
	fix = sentenceFix(FixInfo::Differential, 9);
	fix.sd_latitude = 0.8;
	fix.sd_longitude = 1.4;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Accepted);
	gate.max_error = 1.0;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Error);
	gate.max_error = 1.5;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Accepted);
	// HDOP comes before the error.
	gate.max_error = 1.0;
	gate.max_hdop = 2.0;
	fix.hdop = 2.5;
	QCOMPARE(FixGate::check(fix, gate), FixGate::Hdop);
	
	// Without GST the accuracy which Qt derives does not count ...
	gate = {};
	gate.max_error = 1.0;
	fix = sentenceFix(FixInfo::Differential, 9);
	fix.accuracy = 10;
	QVERIFY(std::isnan(fix.horizontalError()));
	QCOMPARE(FixGate::check(fix, gate), FixGate::Accepted);
	// ... but for a system source it is all there is.
	FixInfo system;
	system.accuracy = 10;
	QCOMPARE(system.horizontalError(), 10.0);
	QCOMPARE(FixGate::check(system, gate), FixGate::Error);
	system.accuracy = 0.5;
	QCOMPARE(FixGate::check(system, gate), FixGate::Accepted);
	
	// Every refusal has a text, an accepted fix has none.
	gate = {};
	QVERIFY(FixGate::verdictText(FixGate::Accepted, system, gate).isEmpty());
	for (auto verdict : { FixGate::FixClass, FixGate::Satellites, FixGate::Hdop, FixGate::Error })
		QVERIFY(!FixGate::verdictText(verdict, fix, gate).isEmpty());
	QVERIFY(FixGate::verdictText(FixGate::Satellites, sentenceFix(FixInfo::Autonomous, 3), gate).contains(QLatin1Char('3')));
	QVERIFY(!FixGate::summary(sentenceFix(FixInfo::RtkFixed, 14)).isEmpty());
	for (auto fix_class : { FixInfo::Unknown, FixInfo::Invalid, FixInfo::Autonomous, FixInfo::Differential, FixInfo::RtkFloat, FixInfo::RtkFixed })
		QVERIFY(!FixGate::className(fix_class).isEmpty());
}


void GnssGateTest::replayTest()
{
	QFile file(QStringLiteral("testdata:sensors/gate.nmea"));
	QVERIFY(file.open(QIODevice::ReadOnly));
	
	FixGateSettings gate;
	gate.min_class = FixInfo::Differential;
	gate.min_satellites = 6;
	gate.max_hdop = 2.0;
	gate.max_error = 1.0;
	
	struct Expected
	{
		NmeaFixParser::Result result;
		FixGate::Verdict verdict;
	};
	const std::vector<Expected> expected = {
	    { NmeaFixParser::Gga, FixGate::Accepted },      // differential, 9 satellites
	    { NmeaFixParser::Gst, FixGate::Accepted },      // 0.5 m
	    { NmeaFixParser::Gga, FixGate::FixClass },      // autonomous
	    { NmeaFixParser::Gga, FixGate::Satellites },    // 5 satellites
	    { NmeaFixParser::Gga, FixGate::Hdop },          // HDOP 2.6
	    { NmeaFixParser::Gga, FixGate::Accepted },      // RTK float, no GST yet
	    { NmeaFixParser::Gst, FixGate::Error },         // 1.4 m
	    { NmeaFixParser::Gst, FixGate::Error },         // of the next second: nothing changes
	    { NmeaFixParser::Gga, FixGate::Accepted },      // RTK fixed, 0.03 m
	    { NmeaFixParser::Other, FixGate::Accepted },    // RMC
	    { NmeaFixParser::BadChecksum, FixGate::Accepted },
	    { NmeaFixParser::Gga, FixGate::FixClass },      // dead reckoning
	    { NmeaFixParser::Gga, FixGate::Accepted },      // satellites and HDOP empty
	};
	
	NmeaFixParser parser;
	auto line_number = std::size_t(0);
	while (!file.atEnd())
	{
		auto const line = file.readLine();
		QVERIFY2(line_number < expected.size(), "more lines than expected");
		auto const result = parser.feed(line);
		auto const verdict = FixGate::check(parser.fix(), gate);
		QVERIFY2(result == expected[line_number].result, line.constData());
		QVERIFY2(verdict == expected[line_number].verdict, line.constData());
		++line_number;
	}
	QCOMPARE(line_number, expected.size());
	QCOMPARE(parser.badChecksums(), 1);
	QCOMPARE(parser.fix().fix_class, FixInfo::Differential);
	QCOMPARE(parser.fix().satellites, -1);
	QVERIFY(std::isnan(parser.fix().hdop));
	QCOMPARE(parser.fix().utc, QTime(10, 0, 7));
}


void GnssGateTest::settingsTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	
	FixGateSettings gate;
	gate.min_class = FixInfo::RtkFloat;
	gate.min_satellites = 7;
	gate.max_hdop = 1.5;
	gate.max_error = 0.25;
	gate.policy = FixGateSettings::Hide;
	{
		QSettings ini(dir.filePath(QStringLiteral("gate.ini")), QSettings::IniFormat);
		gate.save(ini);
		QVERIFY(ini.contains(QStringLiteral("Sensors/gate_min_class")));
		QVERIFY(ini.contains(QStringLiteral("Sensors/gate_policy")));
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("gate.ini")), QSettings::IniFormat);
		QCOMPARE(FixGateSettings::load(ini), gate);
	}
	{
		QSettings ini(dir.filePath(QStringLiteral("empty.ini")), QSettings::IniFormat);
		QCOMPARE(FixGateSettings::load(ini), FixGateSettings{});
	}
	{
		// Values which make no sense give the defaults.
		QSettings ini(dir.filePath(QStringLiteral("odd.ini")), QSettings::IniFormat);
		ini.setValue(QStringLiteral("Sensors/gate_min_class"), QStringLiteral("best"));
		ini.setValue(QStringLiteral("Sensors/gate_min_satellites"), -3);
		ini.setValue(QStringLiteral("Sensors/gate_max_hdop"), QStringLiteral("x"));
		ini.setValue(QStringLiteral("Sensors/gate_policy"), QStringLiteral("blink"));
		QCOMPARE(FixGateSettings::load(ini), FixGateSettings{});
	}
	
	// The application's own settings, which are a file in a temporary folder here.
	gate.save();
	QCOMPARE(FixGateSettings::load(), gate);
	FixGateSettings{}.save();
	QCOMPARE(FixGateSettings::load(), FixGateSettings{});
}


void GnssGateTest::displayTest()
{
#if defined(QT_POSITIONING_LIB) && defined(MAPPER_USE_FAKE_POSITION_PLUGIN)
	Map map;
	{
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
		georef.setGeographicRefPoint(LatLon(50.0, -105.0));
		map.setGeoreferencing(georef);
	}
	QCOMPARE(map.getGeoreferencing().getState(), Georeferencing::Geospatial);
	
	FakePositionSource::setReferencePoint({50.0, -105.0, 400.0});
	Settings::getInstance().setPositionSource(QStringLiteral("Fake position"));
	
	MapView view(&map);
	MapWidget widget(false, false);
	widget.setMapView(&view);
	widget.resize(200, 200);
	
	GPSDisplay display(&widget, map.getGeoreferencing());
	auto* source = display.findChild<QGeoPositionInfoSource*>();
	QVERIFY(source);
	
	auto gate = FixGateSettings{};
	display.setFixGate(gate);
	QCOMPARE(display.fixGate(), gate);
	display.setVisible(true);
	
	auto checked = 0;
	auto passed_map = 0;
	auto passed_latlon = 0;
	auto last_verdict = -1;
	FixInfo last_fix;
	connect(&display, &GPSDisplay::fixChecked, this, [&](const FixInfo& fix, int verdict) {
		++checked;
		last_fix = fix;
		last_verdict = verdict;
	});
	connect(&display, &GPSDisplay::mapPositionUpdated, this, [&]() { ++passed_map; });
	connect(&display, &GPSDisplay::latLonUpdated, this, [&]() { ++passed_latlon; });
	
	auto paint = [&]() {
		QImage image(widget.size(), QImage::Format_RGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		display.paint(&painter);
		painter.end();
		return countPixels(image);
	};
	
	// Before the first fix there is nothing to draw.
	QVERIFY(!display.hasValidPosition());
	QCOMPARE(paint().any, 0);
	
	// A fix of the system kind: only the accuracy is known, and nothing fails.
	source->requestUpdate(0);
	QTRY_COMPARE(checked, 1);
	QCOMPARE(last_verdict, int(FixGate::Accepted));
	QCOMPARE(last_fix.fix_class, FixInfo::Unknown);
	QVERIFY(last_fix.accuracy > 0);
	QCOMPARE(passed_map, 1);
	QCOMPARE(passed_latlon, 1);
	QVERIFY(display.hasValidPosition());
	view.setCenter(MapCoord(display.getLatestGPSCoord()));
	auto pixels = paint();
	QVERIFY(pixels.red > 0);
	
	// A fix with three satellites: shown greyed, not passed on.
	source->setProperty(FixInfo::propertyName(), QVariant::fromValue(sentenceFix(FixInfo::Autonomous, 3)));
	source->requestUpdate(0);
	QTRY_COMPARE(checked, 2);
	QCOMPARE(last_verdict, int(FixGate::Satellites));
	QCOMPARE(last_fix.satellites, 3);
	QCOMPARE(passed_map, 1);
	QCOMPARE(passed_latlon, 1);
	QVERIFY(!display.hasValidPosition());
	pixels = paint();
	QCOMPARE(pixels.red, 0);
	QVERIFY(pixels.grey > 0);
	
	// The same fix with the other policy: not shown at all.
	gate.policy = FixGateSettings::Hide;
	display.setFixGate(gate);
	source->requestUpdate(0);
	QTRY_COMPARE(checked, 3);
	QCOMPARE(last_verdict, int(FixGate::Satellites));
	QCOMPARE(passed_map, 1);
	QVERIFY(!display.hasValidPosition());
	QCOMPARE(paint().any, 0);
	
	// A good fix is passed on again.
	source->setProperty(FixInfo::propertyName(), QVariant::fromValue(sentenceFix(FixInfo::Differential, 9)));
	source->requestUpdate(0);
	QTRY_COMPARE(checked, 4);
	QCOMPARE(last_verdict, int(FixGate::Accepted));
	QCOMPARE(last_fix.fix_class, FixInfo::Differential);
	QCOMPARE(passed_map, 2);
	QCOMPARE(passed_latlon, 2);
	QVERIFY(display.hasValidPosition());
	pixels = paint();
	QVERIFY(pixels.red > 0);
	
	// The limit of the error applies to the accuracy of a system source.
	source->setProperty(FixInfo::propertyName(), QVariant());
	gate.max_error = 1.0;
	display.setFixGate(gate);
	source->requestUpdate(0);
	QTRY_COMPARE(checked, 5);
	QCOMPARE(last_verdict, int(FixGate::Error));
	QCOMPARE(passed_map, 2);
	
	Settings::getInstance().setPositionSource({});
#else
	QSKIP("No position source to test with");
#endif
}


void GnssGateTest::rowsTest()
{
	QWidget page;
	auto* layout = new QFormLayout(&page);
	FixGateRows rows(layout, &page);
	
	auto* class_box = page.findChild<QComboBox*>(QStringLiteral("gate-min-class"));
	auto* satellites_box = page.findChild<QSpinBox*>(QStringLiteral("gate-min-satellites"));
	auto* hdop_box = page.findChild<QDoubleSpinBox*>(QStringLiteral("gate-max-hdop"));
	auto* error_box = page.findChild<QDoubleSpinBox*>(QStringLiteral("gate-max-error"));
	auto* policy_box = page.findChild<QComboBox*>(QStringLiteral("gate-policy"));
	QVERIFY(class_box);
	QVERIFY(satellites_box);
	QVERIFY(hdop_box);
	QVERIFY(error_box);
	QVERIFY(policy_box);
	QCOMPARE(class_box->count(), 5);
	QCOMPARE(policy_box->count(), 2);
	
	// The rows start with the defaults.
	QCOMPARE(rows.settings(), FixGateSettings{});
	
	FixGateSettings gate;
	gate.min_class = FixInfo::RtkFloat;
	gate.min_satellites = 7;
	gate.max_hdop = 1.5;
	gate.max_error = 0.25;
	gate.policy = FixGateSettings::Hide;
	rows.setSettings(gate);
	QCOMPARE(class_box->currentData().toInt(), int(FixInfo::RtkFloat));
	QCOMPARE(satellites_box->value(), 7);
	QCOMPARE(hdop_box->value(), 1.5);
	QCOMPARE(error_box->value(), 0.25);
	QCOMPARE(policy_box->currentData().toInt(), int(FixGateSettings::Hide));
	QCOMPARE(rows.settings(), gate);
	
	class_box->setCurrentIndex(class_box->findData(int(FixInfo::Differential)));
	satellites_box->setValue(5);
	hdop_box->setValue(0);
	error_box->setValue(2);
	policy_box->setCurrentIndex(policy_box->findData(int(FixGateSettings::ShowGreyed)));
	gate.min_class = FixInfo::Differential;
	gate.min_satellites = 5;
	gate.max_hdop = 0;
	gate.max_error = 2;
	gate.policy = FixGateSettings::ShowGreyed;
	QCOMPARE(rows.settings(), gate);
}


QTEST_MAIN(GnssGateTest)
#include "gnss_gate_t.moc"  // IWYU pragma: keep
