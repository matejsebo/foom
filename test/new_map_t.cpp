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
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QProcess>
#include <QRectF>
#include <QSizeF>
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
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/point_cloud.h"
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


/// foom: The plane of the elevation tests, z = 500 + 0.01 (E - E0) + 0.02 (N - N0).
struct Plane
{
	QPointF origin;
	double at(const QPointF& p) const { return 500 + 0.01 * (p.x() - origin.x()) + 0.02 * (p.y() - origin.y()); }
};

/// foom: Writes the plane as a grid over rect (left, bottom, width, height) and returns its path.
QString writePlaneGrid(const QString& path, const QRectF& rect, double cell, const QString& crs, Plane& plane)
{
	auto const width = int(std::ceil(rect.width() / cell));
	auto const height = int(std::ceil(rect.height() / cell));
	plane.origin = QPointF(rect.left(), rect.top());
	ElevationModel grid(width, height, QPointF(rect.left(), rect.top() + height * cell), cell, std::vector<float>(std::size_t(width) * std::size_t(height)), crs);
	for (int r = 0; r < height; ++r)
		for (int c = 0; c < width; ++c)
			grid.setValue(c, r, float(plane.at(grid.cellCenter(c, r))));
	return grid.save(path) ? path : QString{};
}

/// foom: True when outer contains inner and exceeds it by less than margin on every side.
bool tightlyContains(const QRectF& outer, const QRectF& inner, double margin)
{
	auto const left = inner.left() - outer.left();
	auto const right = outer.right() - inner.right();
	auto const bottom = inner.top() - outer.top();
	auto const top = outer.bottom() - inner.bottom();
	return left >= -1e-6 && right >= -1e-6 && bottom >= -1e-6 && top >= -1e-6
	       && left < margin && right < margin && bottom < margin && top < margin;
}

QString rectText(const QRectF& r)
{
	return QString::fromLatin1("%1 %2 %3 %4").arg(r.left(), 0, 'f', 2).arg(r.top(), 0, 'f', 2).arg(r.right(), 0, 'f', 2).arg(r.bottom(), 0, 'f', 2);
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

/// foom: Answers GET requests with one scripted body, as the image service of the USGS answers exportImage.
class ElevationStub : public QObject
{
Q_OBJECT
public:
	ElevationStub()
	{
		connect(&server, &QTcpServer::newConnection, this, [this]() {
			while (auto* socket = server.nextPendingConnection())
			{
				connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readRequest(socket); });
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			}
		});
	}

	bool listen() { return server.listen(QHostAddress::LocalHost); }

	QString url() const { return QStringLiteral("http://127.0.0.1:") + QString::number(server.serverPort()) + QStringLiteral("/arcgis/rest/services/3DEPElevation/ImageServer"); }

	QByteArray body;
	QByteArray content_type = "image/tiff";
	QList<QUrl> requests;   ///< The path and the query of each request.

private:
	void readRequest(QTcpSocket* socket)
	{
		auto& buffer = buffers[socket];
		buffer.append(socket->readAll());
		auto const end = buffer.indexOf("\r\n\r\n");
		if (end < 0)
			return;
		auto const line = buffer.left(buffer.indexOf("\r\n")).split(' ');
		buffers.remove(socket);
		requests.push_back(QUrl::fromEncoded(line.size() > 1 ? line[1] : QByteArray()));
		QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\n"
		                      "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		                      "Connection: close\r\n\r\n" + body;
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

		// foom: a bad gateway is a busy server too (the public server on 2026-11-24)
		stub.statuses = { 502, 200 };
		stub.queries.clear();
		params.map_path = dir.filePath(QStringLiteral("gateway.foom"));
		auto const gateway = createNewMap(params);
		QVERIFY2(gateway.ok, qPrintable(gateway.error));
		QCOMPARE(stub.queries.size(), 2);

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

	/*
	 * Elevation on the data page, Accept (1) to (7).
	 */

	/// The map's projected area and its centre, from a map made without elevation.
	void elevationSetup()
	{
		auto const result = createNewMap(fixtureParams({}));
		QVERIFY2(result.ok, qPrintable(result.error));
		auto const& georef = result.map->getGeoreferencing();
		area = newMapProjectedArea(*result.map, fixture_box);
		centre = georef.toProjectedCoords(fixture_box.centre());
		QVERIFY(area.contains(centre));
		for (auto const& corner : { LatLon(fixture_box.south, fixture_box.west), LatLon(fixture_box.south, fixture_box.east),
		                            LatLon(fixture_box.north, fixture_box.east), LatLon(fixture_box.north, fixture_box.west) })
			QVERIFY(area.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(georef.toProjectedCoords(corner)));
		QVERIFY2(area.width() > 400 && area.width() < 500 && area.height() > 400 && area.height() < 500, qPrintable(rectText(area)));
		// Accept (6)
		QVERIFY(result.map->elevationSet().isEmpty());
	}

	/// Accept (1)
	void elevationGridTest()
	{
		Plane plane;
		auto const grid_path = writePlaneGrid(dir.filePath(QStringLiteral("plane13.tif")), area.adjusted(-200, -200, 200, 200), 2.0, QStringLiteral("EPSG:32613"), plane);
		QVERIFY(!grid_path.isEmpty());
		auto params = fixtureParams(dir.filePath(QStringLiteral("grid.foom")));
		params.elevation_path = grid_path;
		auto result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		auto& set = result.map->elevationSet();
		QCOMPARE(set.href(ElevationSet::Terrain), dir.filePath(QStringLiteral("grid_terrain.tif")));
		QVERIFY(set.href(ElevationSet::Surface).isEmpty());
		auto const* terrain = set.model(ElevationSet::Terrain, result.map->getGeoreferencing().getProjectedCRSSpec());
		QVERIFY(terrain);
		QCOMPARE(terrain->cellSize(), 2.0);
		QVERIFY2(tightlyContains(terrain->extent(), area, 2.0), qPrintable(rectText(terrain->extent()) + QLatin1String(" / ") + rectText(area)));
		QVERIFY2(std::abs(terrain->at(centre) - plane.at(centre)) < 0.01, qPrintable(QString::number(terrain->at(centre) - plane.at(centre))));

		Map reloaded;
		QVERIFY(reloaded.loadFrom(params.map_path));
		QCOMPARE(reloaded.elevationSet().href(ElevationSet::Terrain), dir.filePath(QStringLiteral("grid_terrain.tif")));
		auto const reopened = ElevationModel::open(dir.filePath(QStringLiteral("grid_terrain.tif")));
		QVERIFY(reopened);
		QCOMPARE(reopened->width(), terrain->width());
	}

	/// Accept (2)
	void elevationOtherSystemTest()
	{
		Georeferencing zone14;
		QVERIFY(zone14.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("+init=epsg:32614"), { QStringLiteral("32614") }));
		auto bounds14 = QRectF();
		for (auto const& corner : { LatLon(fixture_box.south, fixture_box.west), LatLon(fixture_box.south, fixture_box.east),
		                            LatLon(fixture_box.north, fixture_box.east), LatLon(fixture_box.north, fixture_box.west) })
		{
			auto const p = zone14.toProjectedCoords(corner);
			bounds14 = bounds14.isNull() ? QRectF(p, QSizeF(0.001, 0.001)) : bounds14.united(QRectF(p, QSizeF(0.001, 0.001)));
		}
		Plane plane;
		auto const grid_path = writePlaneGrid(dir.filePath(QStringLiteral("plane14.tif")), bounds14.adjusted(-200, -200, 200, 200), 2.0, QStringLiteral("EPSG:32614"), plane);
		QVERIFY(!grid_path.isEmpty());
		auto params = fixtureParams(dir.filePath(QStringLiteral("zone14.foom")));
		params.elevation_path = grid_path;
		auto result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		auto const map_crs = result.map->getGeoreferencing().getProjectedCRSSpec();
		auto const* terrain = result.map->elevationSet().model(ElevationSet::Terrain, map_crs);
		QVERIFY(terrain);
		QVERIFY(terrain->crsSpec().isEmpty() || ElevationModel::sameCrs(terrain->crsSpec(), QStringLiteral("EPSG:32613")));
		QVERIFY2(terrain->extent().contains(area), qPrintable(rectText(terrain->extent()) + QLatin1String(" / ") + rectText(area)));
		auto const expected = plane.at(zone14.toProjectedCoords(fixture_box.centre()));
		QVERIFY2(std::abs(terrain->at(centre) - expected) < 0.05, qPrintable(QString::number(terrain->at(centre) - expected)));
	}

	/// Accept (3)
	void elevationPointCloudTest()
	{
		if (!PointCloud::supported())
			QSKIP("Built without LASzip");
		Plane plane;
		plane.origin = area.topLeft();  // left, bottom
		auto const cloud_rect = area.adjusted(-50, -50, 50, 50);
		std::vector<CloudPoint> points;
		for (auto y = std::floor(cloud_rect.top()) + 0.3; y < cloud_rect.bottom(); y += 1.0)
		{
			for (auto x = std::floor(cloud_rect.left()) + 0.6; x < cloud_rect.right(); x += 1.0)
			{
				CloudPoint p;
				p.x = x;
				p.y = y;
				p.z = plane.at(QPointF(x, y));
				p.classification = 2;
				points.push_back(p);
			}
		}
		auto const cloud_path = dir.filePath(QStringLiteral("plane.laz"));
		QString error;
		QVERIFY2(PointCloud::write(cloud_path, points, {}, 0, &error), qPrintable(error));

		auto params = fixtureParams(dir.filePath(QStringLiteral("cloud.foom")));
		params.elevation_path = cloud_path;
		params.elevation_cell = 1.0;
		auto result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		auto& set = result.map->elevationSet();
		QCOMPARE(set.href(ElevationSet::Terrain), dir.filePath(QStringLiteral("cloud_DTM.tif")));
		QCOMPARE(set.href(ElevationSet::Surface), dir.filePath(QStringLiteral("cloud_DSM.tif")));
		auto const map_crs = result.map->getGeoreferencing().getProjectedCRSSpec();
		for (auto kind : { ElevationSet::Terrain, ElevationSet::Surface })
		{
			auto const* grid = set.model(kind, map_crs);
			QVERIFY(grid);
			QCOMPARE(grid->cellSize(), 1.0);
			QVERIFY2(tightlyContains(grid->extent(), area, 1.0), qPrintable(rectText(grid->extent()) + QLatin1String(" / ") + rectText(area)));
		}
		auto const* terrain = set.model(ElevationSet::Terrain, map_crs);
		QVERIFY2(std::abs(terrain->at(centre) - plane.at(centre)) < 0.05, qPrintable(QString::number(terrain->at(centre) - plane.at(centre))));
	}

	/// Accept (4)
	void elevationFailureTest()
	{
		Plane plane;
		auto const far_path = writePlaneGrid(dir.filePath(QStringLiteral("far.tif")), area.translated(5000, 0), 2.0, QStringLiteral("EPSG:32613"), plane);
		QVERIFY(!far_path.isEmpty());
		auto const text_path = dir.filePath(QStringLiteral("not-a-grid.tif"));
		{
			QFile file(text_path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("This is not a grid.\n");
		}
		for (auto const& path : { far_path, text_path })
		{
			auto params = fixtureParams(dir.filePath(QStringLiteral("failed.foom")));
			params.osm_path.clear();
			params.endpoint = stub.url();
			params.elevation_path = path;
			stub.statuses = { 200 };
			stub.queries.clear();
			auto const result = createNewMap(params);
			QVERIFY(!result.ok);
			QVERIFY(stub.queries.isEmpty());
			QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("failed.foom"))));
			QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("failed_terrain.tif"))));
			if (path == far_path)
				QVERIFY2(result.error.contains(QStringLiteral("far.tif")), qPrintable(result.error));
		}

		auto params = fixtureParams({});
		params.elevation_path = far_path;
		auto const unsaved = createNewMap(params);
		QVERIFY(!unsaved.ok);
	}

	/// Accept (5)
	void elevationPartialTest()
	{
		Plane plane;
		auto const half = QRectF(area.left() - 200, area.top() - 200, area.width() / 2 + 200, area.height() + 400);
		auto const path = writePlaneGrid(dir.filePath(QStringLiteral("west.tif")), half, 2.0, QStringLiteral("EPSG:32613"), plane);
		QVERIFY(!path.isEmpty());
		auto params = fixtureParams(dir.filePath(QStringLiteral("west.foom")));
		params.elevation_path = path;
		auto result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.map->elevationSet().href(ElevationSet::Terrain), dir.filePath(QStringLiteral("west_terrain.tif")));
		QVERIFY2(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("part")), qPrintable(result.warnings.join(QLatin1Char('\n'))));
	}

	/// Accept (7)
	void elevationRunnerTest()
	{
		Plane plane;
		auto const grid_path = writePlaneGrid(dir.filePath(QStringLiteral("runner-plane.tif")), area.adjusted(-200, -200, 200, 200), 2.0, QStringLiteral("EPSG:32613"), plane);
		QVERIFY(!grid_path.isEmpty());
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		auto const out = dir.filePath(QStringLiteral("runner-elevation.foom"));
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{
		        { QStringLiteral("step"), QStringLiteral("new-map") },
		        { QStringLiteral("box"), QJsonArray{ fixture_box.south, fixture_box.west, fixture_box.north, fixture_box.east } },
		        { QStringLiteral("symbol_set"), symbolSet() },
		        { QStringLiteral("osm"), fixtureOsm() },
		        { QStringLiteral("crt"), osmCrt() },
		        { QStringLiteral("osm_config"), osmConfig() },
		        { QStringLiteral("elevation"), grid_path },
		        { QStringLiteral("elevation_cell"), 1.0 },
		        { QStringLiteral("path"), out },
		    },
		} } };
		auto const job_path = dir.filePath(QStringLiteral("new-map-elevation.json"));
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
		QCOMPARE(map.elevationSet().href(ElevationSet::Terrain), dir.filePath(QStringLiteral("runner-elevation_terrain.tif")));
	}

	/// foom: The box of the tests of the download from the USGS: 1 km by 0.8 km in UTM zone 13.
	static GeoBox usgsBox() { return { 37.9900, -104.6300, 37.9972, -104.6186 }; }

	static QByteArray fileBytes(const QString& path)
	{
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}

	/// Elevation from USGS 3DEP, Accept (1): the area and the request.
	void usgsRequestTest()
	{
		auto epsg = 0;
		auto const usgs_area = newMapUsgsElevationArea(usgsBox(), 2.0, &epsg);
		QCOMPARE(epsg, 32613);
		for (auto value : { usgs_area.left(), usgs_area.top(), usgs_area.right(), usgs_area.bottom() })
			QVERIFY2(std::abs(value / 2.0 - std::round(value / 2.0)) < 1e-9, qPrintable(QString::number(value, 'f', 4)));
		QVERIFY2(usgs_area.width() > 990 && usgs_area.width() < 1030 && usgs_area.height() > 790 && usgs_area.height() < 830, qPrintable(rectText(usgs_area)));
		// It holds the box of a map in that zone, with less than a cell and the rounding to spare.
		auto params = fixtureParams({});
		params.box = usgsBox();
		auto const plain = createNewMap(params);
		QVERIFY2(plain.ok, qPrintable(plain.error));
		QCOMPARE(plain.objects, 0);
		QVERIFY(tightlyContains(usgs_area, newMapProjectedArea(*plain.map, usgsBox()), 2.0 + 1e-6));

		auto const url = newMapUsgsElevationRequest(QStringLiteral("http://example.org/ImageServer/"), usgs_area, epsg, 2.0);
		QCOMPARE(url.path(), QStringLiteral("/ImageServer/exportImage"));
		QUrlQuery const query(url);
		auto const bbox = query.queryItemValue(QStringLiteral("bbox")).split(QLatin1Char(','));
		QCOMPARE(bbox.size(), 4);
		QCOMPARE(bbox[0].toDouble(), usgs_area.left());
		QCOMPARE(bbox[1].toDouble(), usgs_area.top());
		QCOMPARE(bbox[2].toDouble(), usgs_area.right());
		QCOMPARE(bbox[3].toDouble(), usgs_area.bottom());
		QCOMPARE(query.queryItemValue(QStringLiteral("bboxSR")), QStringLiteral("32613"));
		QCOMPARE(query.queryItemValue(QStringLiteral("imageSR")), QStringLiteral("32613"));
		QCOMPARE(query.queryItemValue(QStringLiteral("size")),
		         QString::number(qRound(usgs_area.width() / 2.0)) + QLatin1Char(',') + QString::number(qRound(usgs_area.height() / 2.0)));
		QCOMPARE(query.queryItemValue(QStringLiteral("format")), QStringLiteral("tiff"));
		QCOMPARE(query.queryItemValue(QStringLiteral("pixelType")), QStringLiteral("F32"));
		QCOMPARE(query.queryItemValue(QStringLiteral("f")), QStringLiteral("image"));

		// The cell size which fits a request of 8000 cells a side.
		QCOMPARE(newMapUsgsCellFor(QRectF(0, 0, 8000, 3000)), 1.0);
		QCOMPARE(newMapUsgsCellFor(QRectF(0, 0, 9000, 3000)), 2.0);
		QCOMPARE(newMapUsgsCellFor(QRectF(0, 0, 30000, 50000)), 10.0);
		QCOMPARE(newMapUsgsCellFor(QRectF(0, 0, 90000, 1000)), 0.0);
		QVERIFY(newMapUsgsCovers(usgsBox().centre()));
		QVERIFY(newMapUsgsCovers(LatLon(61.2, -149.9)));
		QVERIFY(!newMapUsgsCovers(fixture_box.centre()));
	}

	/// Elevation from USGS 3DEP, Accept (1): the grid of the service becomes the map's terrain.
	void usgsDownloadTest()
	{
		QVERIFY(usgs_stub.listen());
		auto epsg = 0;
		auto const usgs_area = newMapUsgsElevationArea(usgsBox(), 2.0, &epsg);
		Plane plane;
		auto const served = writePlaneGrid(dir.filePath(QStringLiteral("served.tif")), usgs_area, 2.0, QStringLiteral("EPSG:32613"), plane);
		QVERIFY(!served.isEmpty());
		usgs_stub.body = fileBytes(served);
		QVERIFY(usgs_stub.body.startsWith("II*"));
		usgs_stub.requests.clear();

		auto params = fixtureParams(dir.filePath(QStringLiteral("usgs.foom")));
		params.box = usgsBox();
		params.usgs_elevation = true;
		params.usgs_elevation_url = usgs_stub.url();
		params.elevation_cell = 2.0;
		auto result = createNewMap(params);
		QVERIFY2(result.ok, qPrintable(result.error));
		QVERIFY2(result.warnings.filter(QStringLiteral("elevation")).isEmpty(), qPrintable(result.warnings.join(QLatin1Char('|'))));
		QCOMPARE(usgs_stub.requests.size(), 1);
		QCOMPARE(usgs_stub.requests.front().path(), QStringLiteral("/arcgis/rest/services/3DEPElevation/ImageServer/exportImage"));
		QCOMPARE(QUrlQuery(usgs_stub.requests.front()).queryItemValue(QStringLiteral("bbox")),
		         QUrlQuery(newMapUsgsElevationRequest(usgs_stub.url(), usgs_area, epsg, 2.0)).queryItemValue(QStringLiteral("bbox")));
		QCOMPARE(QUrlQuery(usgs_stub.requests.front()).queryItemValue(QStringLiteral("imageSR")), QStringLiteral("32613"));

		auto& set = result.map->elevationSet();
		QCOMPARE(set.href(ElevationSet::Terrain), dir.filePath(QStringLiteral("usgs_terrain.tif")));
		QCOMPARE(result.elevation_path, dir.filePath(QStringLiteral("usgs_terrain.tif")));
		QVERIFY(QFileInfo::exists(result.elevation_path));
		auto const* terrain = set.model(ElevationSet::Terrain, result.map->getGeoreferencing().getProjectedCRSSpec());
		QVERIFY(terrain);
		QCOMPARE(terrain->cellSize(), 2.0);
		auto const map_area = newMapProjectedArea(*result.map, usgsBox());
		QVERIFY2(tightlyContains(terrain->extent(), map_area, 2.0 + 1e-6), qPrintable(rectText(terrain->extent()) + QLatin1String(" / ") + rectText(map_area)));
		auto const middle = map_area.center();
		QVERIFY2(std::abs(terrain->at(middle) - plane.at(middle)) < 0.01, qPrintable(QString::number(terrain->at(middle) - plane.at(middle))));
	}

	/// Elevation from USGS 3DEP, Accept (2): an area too large for one request is refused before any request.
	void usgsLimitTest()
	{
		usgs_stub.requests.clear();
		auto params = fixtureParams(dir.filePath(QStringLiteral("usgs-large.foom")));
		params.box = { 37.9900, -104.6300, 38.0170, -104.5274 };  // about 9 km by 3 km
		params.usgs_elevation = true;
		params.usgs_elevation_url = usgs_stub.url();
		params.elevation_cell = 1.0;
		auto const result = createNewMap(params);
		QVERIFY(!result.ok);
		QVERIFY2(result.error.contains(QStringLiteral("8000")) && result.error.contains(QStringLiteral("2 m")), qPrintable(result.error));
		QCOMPARE(usgs_stub.requests.size(), 0);
		QVERIFY(!QFileInfo::exists(params.map_path));

		params.elevation_cell = 2.0;
		usgs_stub.body = "<html>busy</html>";
		auto const coarser = createNewMap(params);
		QVERIFY2(coarser.ok, qPrintable(coarser.error));
		QCOMPARE(usgs_stub.requests.size(), 1);
	}

	/// Elevation from USGS 3DEP, Accept (3): an answer which is no grid, and a grid without data.
	void usgsFailureTest()
	{
		auto params = fixtureParams(dir.filePath(QStringLiteral("usgs-html.foom")));
		params.usgs_elevation = true;
		params.usgs_elevation_url = usgs_stub.url();
		params.elevation_cell = 2.0;
		usgs_stub.requests.clear();
		usgs_stub.content_type = "text/html";
		usgs_stub.body = "<html><body>Error: unable to complete operation.</body></html>";
		auto const html = createNewMap(params);
		QVERIFY2(html.ok, qPrintable(html.error));
		QCOMPARE(usgs_stub.requests.size(), 1);
		QCOMPARE(html.map->getNumObjects(), 10);
		QVERIFY(html.map->elevationSet().isEmpty());
		QVERIFY(html.elevation_path.isEmpty());
		QCOMPARE(html.warnings.filter(QStringLiteral("did not answer with a grid")).size(), 1);
		QVERIFY(QFileInfo::exists(params.map_path));
		QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("usgs-html_terrain.tif"))));

		// A grid of zeros, which is what the service answers where it has no model (seen 2026-11-24).
		{
			auto zero_epsg = 0;
			auto const zero_area = newMapUsgsElevationArea(fixture_box, 2.0, &zero_epsg);
			auto const zero_columns = qRound(zero_area.width() / 2.0);
			auto const zero_rows = qRound(zero_area.height() / 2.0);
			ElevationModel zeros(zero_columns, zero_rows, QPointF(zero_area.left(), zero_area.bottom()), 2.0,
			                     std::vector<float>(std::size_t(zero_columns) * std::size_t(zero_rows), 0.0f), QStringLiteral("EPSG:32613"));
			QVERIFY(zeros.save(dir.filePath(QStringLiteral("zeros.tif"))));
			usgs_stub.content_type = "image/tiff";
			usgs_stub.body = fileBytes(dir.filePath(QStringLiteral("zeros.tif")));
			params.map_path = dir.filePath(QStringLiteral("usgs-zeros.foom"));
			auto const flat = createNewMap(params);
			QVERIFY2(flat.ok, qPrintable(flat.error));
			QVERIFY(flat.map->elevationSet().isEmpty());
			QCOMPARE(flat.warnings.filter(QStringLiteral("no elevation data for this area")).size(), 1);
			QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("usgs-zeros_terrain.tif"))));
		}

		// A grid of no data only.
		auto epsg = 0;
		auto const usgs_area = newMapUsgsElevationArea(fixture_box, 2.0, &epsg);
		QCOMPARE(epsg, 32613);
		auto const columns = qRound(usgs_area.width() / 2.0);
		auto const rows = qRound(usgs_area.height() / 2.0);
		ElevationModel empty(columns, rows, QPointF(usgs_area.left(), usgs_area.bottom()), 2.0, {}, QStringLiteral("EPSG:32613"));
		QVERIFY(empty.save(dir.filePath(QStringLiteral("void.tif"))));
		usgs_stub.content_type = "image/tiff";
		usgs_stub.body = fileBytes(dir.filePath(QStringLiteral("void.tif")));
		params.map_path = dir.filePath(QStringLiteral("usgs-void.foom"));
		auto const nothing = createNewMap(params);
		QVERIFY2(nothing.ok, qPrintable(nothing.error));
		QCOMPARE(nothing.map->getNumObjects(), 10);
		QVERIFY(nothing.map->elevationSet().isEmpty());
		QCOMPARE(nothing.warnings.filter(QStringLiteral("no elevation data for this area")).size(), 1);

		// No answer at all: the service is not there.
		params.usgs_elevation_url = QStringLiteral("http://127.0.0.1:9/ImageServer");
		params.map_path = dir.filePath(QStringLiteral("usgs-down.foom"));
		auto const down = createNewMap(params);
		QVERIFY2(down.ok, qPrintable(down.error));
		QVERIFY(down.map->elevationSet().isEmpty());
		QCOMPARE(down.warnings.filter(QStringLiteral("Cannot download the elevation data")).size(), 1);
	}

	/// Elevation from USGS 3DEP, Accept (4): the runner's step.
	void usgsRunnerTest()
	{
		auto epsg = 0;
		auto const usgs_area = newMapUsgsElevationArea(usgsBox(), 2.0, &epsg);
		Plane plane;
		auto const served = writePlaneGrid(dir.filePath(QStringLiteral("served-runner.tif")), usgs_area, 2.0, QStringLiteral("EPSG:32613"), plane);
		usgs_stub.content_type = "image/tiff";
		usgs_stub.body = fileBytes(served);
		usgs_stub.requests.clear();
		auto const out = dir.filePath(QStringLiteral("runner-usgs.foom"));
		auto const box = usgsBox();
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{
		        { QStringLiteral("step"), QStringLiteral("new-map") },
		        { QStringLiteral("box"), QJsonArray{ box.south, box.west, box.north, box.east } },
		        { QStringLiteral("symbol_set"), symbolSet() },
		        { QStringLiteral("osm"), fixtureOsm() },
		        { QStringLiteral("crt"), osmCrt() },
		        { QStringLiteral("osm_config"), osmConfig() },
		        { QStringLiteral("elevation"), QStringLiteral("usgs") },
		        { QStringLiteral("elevation_url"), usgs_stub.url() },
		        { QStringLiteral("elevation_cell"), 2.0 },
		        { QStringLiteral("path"), out },
		    },
		} } };
		auto const job_path = dir.filePath(QStringLiteral("new-map-usgs.json"));
		{
			QFile file(job_path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write(QJsonDocument(job).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		// The stub answers from this process, so its events must run while the runner works.
		QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 120000);
		auto const output = process.readAllStandardOutput();
		QVERIFY2(process.exitCode() == 0, QByteArray(output + process.readAllStandardError()).constData());
		QCOMPARE(usgs_stub.requests.size(), 1);
		QVERIFY2(output.contains("runner-usgs_terrain.tif"), output.constData());
		Map map;
		QVERIFY(map.loadFrom(out));
		QCOMPARE(map.elevationSet().href(ElevationSet::Terrain), dir.filePath(QStringLiteral("runner-usgs_terrain.tif")));
	}

private:
	QTemporaryDir dir;
	OverpassStub stub;
	ElevationStub usgs_stub;  // foom
	QRectF area;      ///< foom: the box's projected area in the map's system
	QPointF centre;   ///< foom: the box centre, projected
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(NewMapTest)
#include "new_map_t.moc"  // IWYU pragma: keep
