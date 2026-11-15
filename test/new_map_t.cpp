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
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLineF>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>

#include "test_config.h"

#include "global.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "gdal/new_map.h"
#include "gdal/tile_service_template.h"
#include "templates/template.h"

using namespace OpenOrienteering;


namespace {

const GeoBox fixture_box { 51.2480, -102.3550, 51.2520, -102.3490 };

QString sourcePath(const QString& relative)
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("../") + relative);
}

QString fixtureOsm()
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data/osm/new-map.osm"));
}

QString symbolSet()
{
	return sourcePath(QStringLiteral("symbol sets/10000/ISOM 2017-2_10000.omap"));
}

QString osmCrt()
{
	return sourcePath(QStringLiteral("symbol sets/OSM-ISOM 2017-2.crt"));
}

/// FOOM's OSM configuration, which a build tree's data path need not reach.
QString osmConfig()
{
	return sourcePath(QStringLiteral("src/gdal/mapper-osmconf.ini"));
}

/// The distance from p to the polygon's boundary, 0 inside.
double outside(const QPolygonF& polygon, const QPointF& p)
{
	if (polygon.containsPoint(p, Qt::OddEvenFill))
		return 0;
	auto best = 1e9;
	for (int i = 0; i < polygon.size(); ++i)
	{
		auto const a = polygon[i];
		auto const b = polygon[(i + 1) % polygon.size()];
		auto const ab = b - a;
		auto const len2 = QPointF::dotProduct(ab, ab);
		auto t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
		t = qBound(0.0, t, 1.0);
		best = std::min(best, QLineF(p, a + t * ab).length());
	}
	return best;
}

NewMapParams fixtureParams(const QString& map_path)
{
	NewMapParams params;
	params.box = fixture_box;
	params.symbol_set_path = symbolSet();
	params.osm_path = fixtureOsm();
	params.crt_file = osmCrt();
	params.osm_config = osmConfig();
	params.map_path = map_path;
	return params;
}


/// Answers POST requests with a scripted list of status codes.
class OverpassStub : public QObject
{
Q_OBJECT
public:
	OverpassStub()
	{
		connect(&server, &QTcpServer::newConnection, this, &OverpassStub::newConnection);
	}

	bool listen() { return server.listen(QHostAddress::LocalHost); }

	QString url() const { return QStringLiteral("http://127.0.0.1:") + QString::number(server.serverPort()) + QStringLiteral("/api/interpreter"); }

	std::vector<int> statuses;   ///< One per request; 200 sends the body.
	QByteArray body;
	QStringList queries;         ///< The decoded "data" of each request.

private:
	void newConnection()
	{
		while (auto* socket = server.nextPendingConnection())
		{
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readRequest(socket); });
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		}
	}

	void readRequest(QTcpSocket* socket)
	{
		auto& buffer = buffers[socket];
		buffer.append(socket->readAll());
		auto const end = buffer.indexOf("\r\n\r\n");
		if (end < 0)
			return;
		auto length = 0;
		for (auto const& line : buffer.left(end).split('\n'))
		{
			if (line.toLower().startsWith("content-length:"))
				length = line.mid(15).trimmed().toInt();
		}
		if (buffer.size() < end + 4 + length)
			return;
		auto const content = buffer.mid(end + 4, length);
		buffers.remove(socket);

		QUrlQuery const form(QString::fromUtf8(content).replace(QLatin1Char('+'), QLatin1Char(' ')));
		queries.push_back(form.queryItemValue(QStringLiteral("data"), QUrl::FullyDecoded));

		auto const status = statuses.empty() ? 500 : statuses.front();
		if (!statuses.empty())
			statuses.erase(statuses.begin());
		auto const reply_body = status == 200 ? body : QByteArray("busy");
		QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\n"
		                      "Content-Type: application/osm3s+xml\r\n"
		                      "Content-Length: " + QByteArray::number(reply_body.size()) + "\r\n"
		                      "Connection: close\r\n\r\n" + reply_body;
		socket->write(response);
		socket->disconnectFromHost();
	}

	QTcpServer server;
	QHash<QTcpSocket*, QByteArray> buffers;
};

}  // namespace



/**
 * Tests the new-map flow headless.
 *
 * See the design notes, "New map wizard: detail", Accept.
 */
class NewMapTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QVERIFY2(QFileInfo::exists(symbolSet()), qPrintable(symbolSet()));
		QVERIFY2(QFileInfo::exists(osmCrt()), qPrintable(osmCrt()));
		QVERIFY(stub.listen());
	}

	/// Accept (1)
	void utmTest()
	{
		QCOMPARE(newMapUtmEpsg(LatLon(51.25, -102.33)), 32613);
		QCOMPARE(newMapUtmEpsg(LatLon(-30.0, 20.0)), 32734);
		QCOMPARE(newMapUtmEpsg(LatLon(60.0, -0.5)), 32630);
		auto const area = newMapAreaKm2(fixture_box);
		QVERIFY2(area > 0.18 && area < 0.22, qPrintable(QString::number(area)));
	}

	/// Accept (2), through the runner
	void fixtureTest()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));
		auto const out = dir.filePath(QStringLiteral("runner.foom"));
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{
		        { QStringLiteral("step"), QStringLiteral("new-map") },
		        { QStringLiteral("box"), QJsonArray{ fixture_box.south, fixture_box.west, fixture_box.north, fixture_box.east } },
		        { QStringLiteral("symbol_set"), symbolSet() },
		        { QStringLiteral("osm"), fixtureOsm() },
		        { QStringLiteral("crt"), osmCrt() },
		        { QStringLiteral("osm_config"), osmConfig() },
		        { QStringLiteral("path"), out },
		    },
		} } };
		auto const job_path = dir.filePath(QStringLiteral("new-map.json"));
		{
			QFile file(job_path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write(QJsonDocument(job).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(executable, { QStringLiteral("--run"), job_path });
		QVERIFY(process.waitForFinished(120000));
		QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

		Map map;
		QVERIFY(map.loadFrom(out));

		QFile expected_file(QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data/osm/new-map.expected.json")));
		QVERIFY(expected_file.open(QIODevice::ReadOnly));
		auto const expected = QJsonDocument::fromJson(expected_file.readAll()).object().value(QStringLiteral("counts")).toObject();

		std::map<QString, int> counts;
		auto const polygon = newMapBoxPolygon(map, fixture_box);
		auto worst = 0.0;
		map.applyOnAllObjects([&](const Object* object) {
			++counts[object->getSymbol()->getNumberAsString()];
			for (auto const& coord : object->getRawCoordinateVector())
				worst = std::max(worst, outside(polygon, QPointF(MapCoordF(coord))));
		});
		QStringList actual_text;
		for (auto const& entry : counts)
			actual_text.push_back(entry.first + QLatin1Char('=') + QString::number(entry.second));
		QVERIFY2(int(counts.size()) == expected.size(), qPrintable(actual_text.join(QLatin1String(", "))));
		for (auto const& key : expected.keys())
			QVERIFY2(counts[key] == expected.value(key).toInt(), qPrintable(key + QStringLiteral(": ") + actual_text.join(QLatin1String(", "))));
		QVERIFY2(worst < 0.001, qPrintable(QString::number(worst)));
	}

	/// Accept (3)
	void georeferencingTest()
	{
		auto params = fixtureParams({});
		params.declination = 3.5;
		auto const result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		auto const& georef = result.map->getGeoreferencing();
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		QCOMPARE(georef.getProjectedCRSId(), QStringLiteral("EPSG"));
		QCOMPARE(georef.getProjectedCRSParameters().front(), QStringLiteral("32613"));
		auto const centre = georef.getGeographicRefPoint();
		QVERIFY(std::abs(centre.latitude() - 51.25) < 1e-7);
		QVERIFY(std::abs(centre.longitude() - -102.352) < 1e-7);
		QCOMPARE(georef.getMapRefPoint(), MapCoord(0, 0));
		QVERIFY(std::abs(georef.getDeclination() - 3.5) < 0.01);

		params.rotate_to_magnetic = false;
		auto const unrotated = createNewMap(params);
		QVERIFY2(unrotated.ok, qPrintable(unrotated.error));
		QVERIFY(std::abs(unrotated.map->getGeoreferencing().getGrivation()) < 0.01);
	}

	/// Accept (4)
	void downloadTest()
	{
		QFile fixture(fixtureOsm());
		QVERIFY(fixture.open(QIODevice::ReadOnly));
		stub.body = fixture.readAll();

		auto params = fixtureParams(dir.filePath(QStringLiteral("download.foom")));
		params.osm_path.clear();
		params.endpoint = stub.url();
		params.retry_delay_ms = 200;
		stub.statuses = { 429, 200 };
		stub.queries.clear();
		auto const result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(stub.queries.size(), 2);
		QCOMPARE(stub.queries.front(), newMapOverpassQuery(fixture_box));
		QVERIFY(stub.queries.front().contains(QStringLiteral("51.248,-102.355,51.252,-102.349")));
		QCOMPARE(result.osm_path, dir.filePath(QStringLiteral("download.osm")));
		QVERIFY(QFileInfo::exists(result.osm_path));
		QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("download.foom"))));
		QCOMPARE(result.map->getNumObjects(), 10);
		auto trees = 0;
		result.map->applyOnAllObjects([&trees](const Object* object) {
			trees += object->getSymbol()->getNumberAsString() == QLatin1String("417");
		});
		QCOMPARE(trees, 1);

		stub.statuses = { 504, 504 };
		stub.queries.clear();
		params.map_path = dir.filePath(QStringLiteral("busy.foom"));
		auto const busy = createNewMap(params);
		QVERIFY(!busy.ok);
		QCOMPARE(stub.queries.size(), 2);
		QVERIFY2(busy.error.contains(QStringLiteral("downloaded")), qPrintable(busy.error));
	}

	/// Accept (5)
	void sizeLimitTest()
	{
		GeoBox const large { 51.20, -102.40, 51.30, -102.30 };
		QVERIFY(newMapAreaKm2(large) > 25.0);
		auto params = fixtureParams({});
		params.box = large;
		params.osm_path.clear();
		params.endpoint = stub.url();
		stub.queries.clear();
		auto const refused = createNewMap(params);
		QVERIFY(!refused.ok);
		QVERIFY2(refused.error.contains(QStringLiteral("25")), qPrintable(refused.error));
		QVERIFY(stub.queries.isEmpty());

		params.osm_path = fixtureOsm();
		auto const accepted = createNewMap(params);
		QVERIFY2(accepted.ok, qPrintable(accepted.error));
	}

	/// Accept (6)
	void tileServiceTest()
	{
		auto params = fixtureParams({});
		params.tile_service = QStringLiteral("OpenStreetMap");
		auto const result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.map->getNumTemplates(), 1);
		auto const* temp = result.map->getTemplate(0);
		QCOMPARE(QLatin1String(temp->getTemplateType()), QLatin1String(TileServiceTemplate::templateType()));
		QCOMPARE(static_cast<const TileServiceTemplate*>(temp)->service().name, QStringLiteral("OpenStreetMap"));
	}

private:
	QTemporaryDir dir;
	OverpassStub stub;
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(NewMapTest)
#include "new_map_t.moc"  // IWYU pragma: keep
