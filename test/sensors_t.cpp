/*
 *    Copyright 2019-2020 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <vector>

#include <QtTest>
#include <QDir>           // IWYU pragma: keep
#include <QFileInfo>      // IWYU pragma: keep
#include <QObject>
#include <QScopedPointer> // IWYU pragma: keep
#include <QSignalSpy>     // IWYU pragma: keep
#include <QStandardPaths> // IWYU pragma: keep
#include <QStaticPlugin>  // IWYU pragma: keep
#include <QTemporaryFile> // IWYU pragma: keep

#include "test_config.h"  // IWYU pragma: keep

#include "util/backports.h"  // IWYU pragma: keep

namespace OpenOrienteering {}
using namespace OpenOrienteering;


#ifdef QT_POSITIONING_LIB
#include <QGeoCoordinate>         // IWYU pragma: keep
#include <QGeoPositionInfo>       // IWYU pragma: keep
#include <QGeoPositionInfoSource>
#endif

#ifdef MAPPER_USE_FAKE_POSITION_PLUGIN
#include <QGeoCoordinate>           // IWYU pragma: keep
#include <QNmeaPositionInfoSource>  // IWYU pragma: keep
#include "sensors/fake_position_plugin.h"
#include "sensors/fake_position_source.h"
Q_IMPORT_PLUGIN(FakePositionPlugin)
#endif

#ifdef MAPPER_USE_NMEA_POSITION_PLUGIN
#include <QNmeaPositionInfoSource>  // IWYU pragma: keep
#include "sensors/nmea_position_plugin.h"
Q_IMPORT_PLUGIN(NmeaPositionPlugin)
#endif

#ifdef MAPPER_USE_POWERSHELL_POSITION_PLUGIN
#include "sensors/powershell_position_source.h"
Q_IMPORT_PLUGIN(PowershellPositionPlugin)
#endif


class SensorsTest : public QObject
{
	Q_OBJECT
	
private slots:
	void initTestCase()
	{
		QDir::addSearchPath(QStringLiteral("testdata"), QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data")));
	}
	
	
#if defined(MAPPER_USE_FAKE_POSITION_PLUGIN)
	void fakePositionSourcePluginTest()
	{
		auto sources = QGeoPositionInfoSource::availableSources();
		QVERIFY(sources.contains(QStringLiteral("Fake position")));
	}
	
	void fakePositionSourceSimulatedTest()
	{
		auto const reference = QGeoCoordinate{10, 20, 100};
		FakePositionSource::setReferencePoint(reference);
		
		auto* source = QGeoPositionInfoSource::createSource(QStringLiteral("Fake position"), this);
		QVERIFY(source);
		QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
		
		QSignalSpy source_spy(source, &QGeoPositionInfoSource::positionUpdated);
		QVERIFY(source_spy.isValid());
		
		source->startUpdates();
		QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
		QVERIFY(source_spy.wait());
		
		auto last = source->lastKnownPosition(true);
		QVERIFY(last.isValid());
		QVERIFY(qAbs(last.coordinate().latitude() - reference.latitude()) < 0.002);
		QVERIFY(qAbs(last.coordinate().longitude() - reference.longitude()) < 0.002);
		QVERIFY(qAbs(last.coordinate().altitude() - reference.altitude()) < 20);
		
		source->stopUpdates();
		delete source;
	}
#endif  // MAPPER_USE_FAKE_POSITION_PLUGIN
	
	
#if defined(MAPPER_USE_NMEA_POSITION_PLUGIN)
	void nmeaPositionSourcePluginTest()
	{
		auto nmea_position_source = QStringLiteral("NMEA (OpenOrienteering)");
		auto sources = QGeoPositionInfoSource::availableSources();
		QVERIFY(sources.contains(nmea_position_source));
	}
	
	void nmeaPositionSourceSimulatedTest()
	{
		auto nmea_position_source = QStringLiteral("NMEA (OpenOrienteering)");
		auto* source = QGeoPositionInfoSource::createSource(nmea_position_source, this);
#if !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)
#if !defined(Q_OS_WIN)  // foom: on Windows the source reads a serial port, see nmea_source_t
		QVERIFY(!source || source->error() != QGeoPositionInfoSource::NoError);
#endif
		QSKIP("\"unsupported platform");
#endif
		QVERIFY(source);
		QVERIFY(qobject_cast<QNmeaPositionInfoSource*>(source));
		QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
		
		QSignalSpy source_spy(source, &QGeoPositionInfoSource::positionUpdated);
		QVERIFY(source_spy.isValid());
		
		auto test_file = QFileInfo(QStringLiteral("testdata:sensors/nmea.txt"));
		QVERIFY(test_file.exists());
		qputenv("QT_NMEA_SERIAL_PORT", test_file.absoluteFilePath().toUtf8());
		source->startUpdates();
		QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::NoError));
		QVERIFY(source_spy.wait());
		
		auto last = source->lastKnownPosition(true);
		QVERIFY(last.isValid());
		QCOMPARE(int(last.coordinate().latitude()), -30);
		QCOMPARE(int(last.coordinate().longitude()), 139);
		
		source->stopUpdates();
		
		QTemporaryFile unreadable_file;
		QVERIFY(unreadable_file.open());
		unreadable_file.close();
		unreadable_file.setPermissions({});
		qputenv("QT_NMEA_SERIAL_PORT", unreadable_file.fileName().toUtf8());
		source->startUpdates();
		QCOMPARE(int(source->error()), int(QGeoPositionInfoSource::AccessError));
		
		delete source;
	}
#endif  // MAPPER_USE_NMEA_POSITION_PLUGIN
	
#if defined(MAPPER_USE_POWERSHELL_POSITION_PLUGIN)
	void powershellPositionSourcePluginTest()
	{
		auto powershell_position_source = QStringLiteral("Windows");
		auto sources = QGeoPositionInfoSource::availableSources();
		QVERIFY(sources.contains(powershell_position_source));  // JSON file properties
	}
		
	void powershellPositionSourceLiveTest()
	{
		if(!qEnvironmentVariableIsEmpty("SKIP_POWERSHELL_POSITION_SOURCE_LIVE_TEST"))
			QSKIP("SKIP_POWERSHELL_POSITION_SOURCE_LIVE_TEST is set");
		
		auto powershell_path = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
#if !defined(Q_OS_WIN)
		if (powershell_path.isEmpty())
			QSKIP("Powershell not available");
#endif
		QVERIFY(!powershell_path.isEmpty());
		
		auto powershell_position_source = QStringLiteral("Windows");
		// foom: The test owns the source, so that the helper process ends with the test.
		QScopedPointer<QGeoPositionInfoSource> generic_source { QGeoPositionInfoSource::createSource(powershell_position_source, nullptr) };
		auto* source = qobject_cast<PowershellPositionSource*>(generic_source.data());
		QVERIFY(source);
		QCOMPARE(source->error(), QGeoPositionInfoSource::NoError);
		QCOMPARE(source->status(), PowershellPositionSource::Status::NotStarted);
		
		QSignalSpy source_spy(source, &QGeoPositionInfoSource::positionUpdated);
		QVERIFY(source_spy.isValid());
		source->startUpdates();
		
		// foom: Whether there is a position is up to the location service of
		// this machine, and so is the time it takes. When updates start,
		// Windows reports "NoData" and then, as soon as a provider has a
		// position, "Ready" and the position. That took between a hundredth
		// of a second and more than three seconds on one machine, and the
		// source reports an error in between. So the test waits for a position
		// or for a status which will not change by itself, and it skips when
		// the location service is what stands in the way. Anything else, such
		// as a helper process which does not start, ends or writes nonsense,
		// is a fault of the source and fails.
		using Status = PowershellPositionSource::Status;
		auto const waiting_for_service = [source]() {
			switch (source->status())
			{
			case Status::HelperStarting:
			case Status::Listening:
			case Status::Initializing:
			case Status::Ready:
			case Status::NoData:
				return true;
			default:
				return false;
			}
		};
		auto const timeout = 20000;
		auto const settled = QTest::qWaitFor([&source_spy, &waiting_for_service]() {
			return source_spy.count() > 0 || !waiting_for_service();
		}, timeout);
		Q_UNUSED(settled)  // what the source says is looked at below
		
		if (source_spy.count() > 0)
		{
			auto const last = source->lastKnownPosition(false);
			QVERIFY(last.isValid());
			QVERIFY(last.coordinate().isValid());
			QVERIFY(last.hasAttribute(QGeoPositionInfo::HorizontalAccuracy));
			source->stopUpdates();
			return;
		}
		
		auto const status_text = source->statusText();
		switch (source->status())
		{
		case Status::Disabled:
		case Status::Denied:
			QSKIP(qPrintable(status_text));
			break;
		case Status::Listening:
		case Status::Initializing:
		case Status::Ready:
		case Status::NoData:
			QSKIP(qPrintable(QStringLiteral("No position within %1 s. %2").arg(timeout / 1000).arg(status_text)));
			break;
		case Status::HelperStarting:
			QFAIL(qPrintable(QStringLiteral("Nothing from the helper process within %1 s. %2").arg(timeout / 1000).arg(status_text)));
			break;
		case Status::NotStarted:
			QFAIL(qPrintable(QStringLiteral("The helper process ended by itself. %1").arg(status_text)));
			break;
		default:
			QFAIL(qPrintable(status_text));
			break;
		}
	}
	
	static QGeoPositionInfoSource::Error powershellPositionSourceSimulationSetup(QProcess& process, QByteArray& /* unused */, QByteArray& /* unused */)
	{
		auto test_file = QFileInfo(QStringLiteral("testdata:sensors/powershell_position.txt"));
		if (!test_file.exists())
			return QGeoPositionInfoSource::UnknownSourceError;
		
#if defined(Q_OS_WIN)
		process.setProgram(QStandardPaths::findExecutable(QStringLiteral("cmd")));
		process.setArguments({QStringLiteral("/C"), QStringLiteral("type"), QDir::toNativeSeparators(test_file.absoluteFilePath())});
#else
		process.setProgram(QStringLiteral("/bin/cat"));
		process.setArguments({test_file.absoluteFilePath()});
#endif
		return QGeoPositionInfoSource::NoError;
	};
	
	void powershellPositionSourceSimulatedTest()
	{
		PowershellPositionSource source(powershellPositionSourceSimulationSetup);
		QCOMPARE(int(source.error()), int(QGeoPositionInfoSource::NoError));
		
		QSignalSpy source_spy(&source, &QGeoPositionInfoSource::positionUpdated);
		QVERIFY(source_spy.isValid());
		source.startUpdates();
		QCOMPARE(int(source.error()), int(QGeoPositionInfoSource::NoError));
		QVERIFY(source_spy.wait());
		
		auto last = source.lastKnownPosition(true);
		QVERIFY(last.isValid());
		QCOMPARE(int(last.coordinate().latitude()), 50);
		QCOMPARE(int(last.coordinate().longitude()), 8);
		
		source.stopUpdates();
	}
	
	
	// foom: What the source makes of single lines of the helper process,
	// and that status() tells apart what error() reports as the same error.
	
private:
	static QString& simulatedLine()
	{
		static QString line;
		return line;
	}
	
private slots:
	static QGeoPositionInfoSource::Error powershellPositionSourceLineSetup(QProcess& process, QByteArray& /* unused */, QByteArray& /* unused */)
	{
#if defined(Q_OS_WIN)
		process.setProgram(QStandardPaths::findExecutable(QStringLiteral("cmd")));
		process.setArguments({QStringLiteral("/C"), QStringLiteral("echo"), simulatedLine()});
#else
		process.setProgram(QStringLiteral("/bin/echo"));
		process.setArguments({simulatedLine()});
#endif
		return QGeoPositionInfoSource::NoError;
	};
	
	void powershellPositionSourceStatusTest_data()
	{
		using Status = PowershellPositionSource::Status;
		QTest::addColumn<QString>("line");
		QTest::addColumn<int>("expected_error");
		QTest::addColumn<int>("status_at_error");
		QTest::addColumn<int>("status_at_end");
		
		// The helper process of the test ends after its line. A status which
		// says why there can be no position outlasts the process.
		QTest::newRow("no data")
		        << QStringLiteral("Status;NoData")
		        << int(QGeoPositionInfoSource::UnknownSourceError) << int(Status::NoData) << int(Status::NotStarted);
		QTest::newRow("location turned off")
		        << QStringLiteral("Status;Disabled")
		        << int(QGeoPositionInfoSource::AccessError) << int(Status::Disabled) << int(Status::Disabled);
		QTest::newRow("permission denied")
		        << QStringLiteral("Permission;Denied")
		        << int(QGeoPositionInfoSource::AccessError) << int(Status::Denied) << int(Status::Denied);
		QTest::newRow("unknown status")
		        << QStringLiteral("Status;Nonsense")
		        << int(QGeoPositionInfoSource::UnknownSourceError) << int(Status::BadData) << int(Status::BadData);
		QTest::newRow("unreadable position")
		        << QStringLiteral("Position;yesterday;north;east;up;near;far")
		        << int(QGeoPositionInfoSource::UnknownSourceError) << int(Status::BadData) << int(Status::BadData);
	}
	
	void powershellPositionSourceStatusTest()
	{
		using Status = PowershellPositionSource::Status;
		QFETCH(QString, line);
		QFETCH(int, expected_error);
		QFETCH(int, status_at_error);
		QFETCH(int, status_at_end);
		
		simulatedLine() = line;
		PowershellPositionSource source(powershellPositionSourceLineSetup);
		QCOMPARE(int(source.error()), int(QGeoPositionInfoSource::NoError));
		QCOMPARE(source.status(), Status::NotStarted);
		
		QSignalSpy position_spy(&source, &QGeoPositionInfoSource::positionUpdated);
		QVERIFY(position_spy.isValid());
		std::vector<int> statuses_at_error;
		connect(&source, QOverload<QGeoPositionInfoSource::Error>::of(&QGeoPositionInfoSource::error), &source, [&source, &statuses_at_error]() {
			statuses_at_error.push_back(int(source.status()));
		});
		
		source.startUpdates();
		QCOMPARE(source.status(), Status::HelperStarting);
		QTRY_VERIFY(!statuses_at_error.empty());
		QCOMPARE(statuses_at_error.front(), status_at_error);
		QCOMPARE(int(source.error()), expected_error);
		QTRY_COMPARE(int(source.status()), status_at_end);
		QVERIFY(!source.statusText().isEmpty());
		QCOMPARE(position_spy.count(), 0);
	}
	
	static QGeoPositionInfoSource::Error powershellPositionSourceMissingHelperSetup(QProcess& process, QByteArray& /* unused */, QByteArray& /* unused */)
	{
		process.setProgram(QStringLiteral("no-such-position-helper"));
		return QGeoPositionInfoSource::NoError;
	};
	
	void powershellPositionSourceMissingHelperTest()
	{
#if !defined(Q_OS_WIN)
		QSKIP("Elsewhere, QProcess learns later that a program is missing");
#endif
		using Status = PowershellPositionSource::Status;
		PowershellPositionSource source(powershellPositionSourceMissingHelperSetup);
		QCOMPARE(int(source.error()), int(QGeoPositionInfoSource::NoError));
		
		source.startUpdates();
		QCOMPARE(int(source.error()), int(QGeoPositionInfoSource::UnknownSourceError));
		QCOMPARE(source.status(), Status::HelperNotStarted);
		QVERIFY(source.statusText().startsWith(QLatin1String("The helper process could not be started")));
	}
#endif  // MAPPER_USE_POWERSHELL_POSITION_PLUGIN
	
};


/*
 * We don't need a real GUI window.
 * 
 * But we discovered QTBUG-58768 macOS: Crash when using QPrinter
 * while running with "minimal" platform plugin.
 */
#ifndef Q_OS_MACOS
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}
#endif


QTEST_MAIN(SensorsTest)
#include "sensors_t.moc"  // IWYU pragma: keep
