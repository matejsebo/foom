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
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTransform>
#include <QUrl>
#include <QUrlQuery>

#include <gdal.h>
#include <cpl_conv.h>

#include "test_config.h"

#include "global.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "fileformats/xml_file_format.h"
#include "fileformats/file_import_export.h"
#include "gdal/map_service.h"
#include "gdal/map_service_capabilities.h"
#include "gdal/tile_pyramid.h"
#include "gdal/tile_service_template.h"
#include "templates/template.h"

using namespace OpenOrienteering;


namespace {

constexpr double merc_radius = 6378137.0;
constexpr double merc_half = 20037508.342789244;
constexpr double cell_m = 20.0;    ///< Checkerboard cell size in web-mercator metres
constexpr int tile_px = 256;

QPointF toMercator(const LatLon& latlon)
{
	auto const lat = qDegreesToRadians(latlon.latitude());
	auto const lon = qDegreesToRadians(latlon.longitude());
	return { merc_radius * lon, merc_radius * std::log(std::tan(M_PI / 4 + lat / 2)) };
}

/// The checkerboard: dark where the sum of the cell indices is odd.
bool checkerDark(const QPointF& merc)
{
	auto const cx = qint64(std::floor(merc.x() / cell_m));
	auto const cy = qint64(std::floor(merc.y() / cell_m));
	return ((cx + cy) % 2 + 2) % 2 != 0;
}

/// Renders the checkerboard for a web-mercator window of minx, maxx, miny, maxy.
QByteArray checkerPng(double minx, double maxx, double miny, double maxy, int width, int height)
{
	QImage image(width, height, QImage::Format_RGB32);
	auto const dx = (maxx - minx) / width;
	auto const dy = (maxy - miny) / height;
	for (int y = 0; y < height; ++y)
	{
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		auto const my = maxy - (y + 0.5) * dy;
		for (int x = 0; x < width; ++x)
			line[x] = checkerDark({minx + (x + 0.5) * dx, my}) ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
	}
	QByteArray data;
	QBuffer buffer(&data);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return data;
}

QByteArray tilePng(int z, qint64 x, qint64 y)
{
	auto const size = 2 * merc_half / double(qint64(1) << z);
	auto const minx = -merc_half + x * size;
	auto const maxy = merc_half - y * size;
	return checkerPng(minx, minx + size, maxy - size, maxy, tile_px, tile_px);
}


/**
 * A tiny HTTP server in the test process which answers XYZ tile requests,
 * WMS GetMap and GetCapabilities, and WMTS capabilities and tile requests,
 * all with the same checkerboard. Every request path is recorded.
 */
class TileStub : public QObject
{
Q_OBJECT
public:
	TileStub()
	{
		auto const data_dir = QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data/tiles"));
		wms_capabilities = readAll(data_dir + QStringLiteral("/wms-capabilities-1.3.0.xml"));
		wmts_capabilities = readAll(data_dir + QStringLiteral("/wmts-capabilities.xml"));
		connect(&server, &QTcpServer::newConnection, this, &TileStub::newConnection);
	}

	bool listen()
	{
		return server.listen(QHostAddress::LocalHost);
	}

	QString baseUrl() const
	{
		return QStringLiteral("http://127.0.0.1:") + QString::number(server.serverPort());
	}

	QStringList requests;

private:
	static QByteArray readAll(const QString& path)
	{
		QFile file(path);
		file.open(QIODevice::ReadOnly);
		return file.readAll();
	}

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
		auto const request_line = QString::fromLatin1(buffer.left(buffer.indexOf("\r\n")));
		buffer.clear();
		buffers.remove(socket);
		auto const parts = request_line.split(QLatin1Char(' '));
		auto const target = parts.size() > 1 ? parts[1] : QString{};
		requests.push_back(target);

		QByteArray body;
		QByteArray type = "image/png";
		int status = 200;
		QUrl const url(QStringLiteral("http://x") + target);
		auto const path = url.path();
		QUrlQuery const query(url);
		if (path.startsWith(QLatin1String("/xyz/")) || path.startsWith(QLatin1String("/wmts/")))
		{
			if (path == QLatin1String("/wmts/WMTSCapabilities.xml"))
			{
				body = wmts_capabilities;
				body.replace("@BASE_URL@", baseUrl().toLatin1());
				type = "application/xml";
			}
			else
			{
				auto const items = path.section(QLatin1Char('/'), 2).split(QLatin1Char('/'));
				if (items.size() == 3)
				{
					auto const z = items[0].toInt();
					auto const second = items[1].toLongLong();
					auto const third = items[2].section(QLatin1Char('.'), 0, 0).toLongLong();
					// XYZ: z/x/y, WMTS RESTful: TileMatrix/TileRow/TileCol
					if (path.startsWith(QLatin1String("/xyz/")))
						body = tilePng(z, second, third);
					else
						body = tilePng(z, third, second);
				}
				else
				{
					status = 404;
				}
			}
		}
		else if (path == QLatin1String("/wms"))
		{
			// GDAL writes the parameter names in lower case
			QHash<QString, QString> items;
			for (auto const& item : query.queryItems(QUrl::FullyDecoded))
				items.insert(item.first.toUpper(), item.second);
			auto const request = items.value(QStringLiteral("REQUEST")).toLower();
			if (request == QLatin1String("getcapabilities"))
			{
				body = wms_capabilities;
				body.replace("@BASE_URL@", baseUrl().toLatin1());
				type = "application/xml";
			}
			else if (request == QLatin1String("getmap"))
			{
				auto const bbox = items.value(QStringLiteral("BBOX")).split(QLatin1Char(','));
				auto const width = items.value(QStringLiteral("WIDTH")).toInt();
				auto const height = items.value(QStringLiteral("HEIGHT")).toInt();
				if (bbox.size() == 4 && width > 0 && height > 0)
					body = checkerPng(bbox[0].toDouble(), bbox[2].toDouble(), bbox[1].toDouble(), bbox[3].toDouble(), width, height);
				else
					status = 400;
			}
			else
			{
				status = 400;
			}
		}
		else
		{
			status = 404;
		}

		QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\n"
		                      "Content-Type: " + type + "\r\n"
		                      "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		                      "Connection: close\r\n\r\n" + body;
		socket->write(response);
		socket->disconnectFromHost();
	}

	QTcpServer server;
	QHash<QTcpSocket*, QByteArray> buffers;
	QByteArray wms_capabilities;
	QByteArray wmts_capabilities;
};


/// A map at 1:10,000 in UTM zone 14, reference point at 60 N 99 W, with 7 degrees of grivation.
void setupMap(Map& map, bool geospatial = true)
{
	Georeferencing georef = map.getGeoreferencing();
	georef.setScaleDenominator(10000);
	if (geospatial)
	{
		georef.setProjectedCRS(QStringLiteral("UTM"), QStringLiteral("+proj=utm +zone=14 +datum=WGS84 +units=m +no_defs"));
		georef.setGeographicRefPoint(LatLon(60.0, -99.0));
		georef.setDeclination(7.0);
	}
	map.setGeoreferencing(georef);
}

/// A view of width by height pixels at zoom pixels per mm, centred on center (map mm).
struct View
{
	QPointF center_mm;
	double zoom;
	int width;
	int height;

	QTransform transform() const
	{
		QTransform t;
		t.translate(width / 2.0, height / 2.0);
		t.scale(zoom, zoom);
		t.translate(-center_mm.x(), -center_mm.y());
		return t;
	}

	QRectF mapRect() const
	{
		return transform().inverted().mapRect(QRectF(0, 0, width, height));
	}

	QPointF mapCoord(const QPointF& pixel) const
	{
		return transform().inverted().map(pixel);
	}
};

QImage paintTemplates(Map& map, const View& view, bool on_screen = true)
{
	QImage image(view.width, view.height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setWorldTransform(view.transform());
	map.drawTemplates(&painter, view.mapRect(), 0, map.getNumTemplates() - 1, nullptr, on_screen);
	return image;
}

/// The checkerboard colour expected at a screen pixel.
bool expectedDark(const Map& map, const View& view, const QPointF& pixel)
{
	auto const map_coord = MapCoordF(view.mapCoord(pixel));
	auto const latlon = map.getGeoreferencing().toGeographicCoords(map_coord);
	return checkerDark(toMercator(latlon));
}

/// True when the four pixels at distance d around pixel share its expected colour.
bool awayFromEdge(const Map& map, const View& view, const QPointF& pixel, double d)
{
	auto const expected = expectedDark(map, view, pixel);
	for (auto const& offset : { QPointF(d, 0), QPointF(-d, 0), QPointF(0, d), QPointF(0, -d) })
	{
		if (expectedDark(map, view, pixel + offset) != expected)
			return false;
	}
	return true;
}

int grey(const QImage& image, int x, int y)
{
	return qGray(image.pixel(x, y));
}

MapService xyzService(const TileStub& stub)
{
	MapService service;
	service.kind = MapService::Xyz;
	service.name = QStringLiteral("Checkerboard XYZ");
	service.url = stub.baseUrl() + QStringLiteral("/xyz/${z}/${x}/${y}.png");
	service.min_zoom = 0;
	service.max_zoom = 16;
	service.attribution = QStringLiteral("Test data");
	return service;
}

}  // namespace


/**
 * Tests the tile service templates.
 *
 * See the design notes, "Tile service templates: detailed design".
 */
class TileTemplateTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
		QDir::addSearchPath(QStringLiteral("testdata"), QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data")));

		QVERIFY(cache_dir.isValid());
		TileServiceTemplate::setCacheDirectory(cache_dir.path());
		QVERIFY(stub.listen());

		QString reason;
		if (!TileServiceTemplate::driversAvailable(&reason))
			QSKIP(qPrintable(reason));
	}

	void pyramidTest()
	{
		// Accept (1)
		auto const r0 = 0.5;
		QCOMPARE(TilePyramid::levelForResolution(r0, 1.0), 1);
		QCOMPARE(TilePyramid::levelForResolution(r0, 0.9), 0);
		QCOMPARE(TilePyramid::levelForResolution(r0, 0.1), 0);
		QCOMPARE(TilePyramid::levelForResolution(r0, 4.0), 3);
		QCOMPARE(TilePyramid::levelForResolution(r0, 3.9), 2);
		QCOMPARE(TilePyramid::levelResolution(r0, 3), 4.0);
		QCOMPARE(TilePyramid::tileExtent(r0, 1), 512.0);

		auto const key = TileKey{2, -3, 5};
		auto const rect = TilePyramid::tileRect(r0, key);
		QCOMPARE(rect.left(), -3 * 1024.0);
		QCOMPARE(rect.top(), 5 * 1024.0);
		QCOMPARE(rect.width(), 1024.0);
		QCOMPARE(TilePyramid::tileAt(r0, 2, rect.center()), key);
		QCOMPARE(TilePyramid::tileAt(r0, 2, QPointF(rect.left(), rect.top())), key);
		QCOMPARE(TilePyramid::ancestor(key, 4), (TileKey{4, -1, 1}));
		QCOMPARE(TilePyramid::ancestor(TileKey{0, -1, 0}, 1), (TileKey{1, -1, 0}));

		// Brute force comparison
		QRectF const area(-1500.0, 300.0, 3100.0, 2000.0);
		auto const tiles = TilePyramid::tilesIntersecting(r0, 1, area);
		std::vector<TileKey> expected;
		for (qint64 row = -10; row < 10; ++row)
			for (qint64 column = -10; column < 10; ++column)
			{
				auto const candidate = TileKey{1, column, row};
				auto const r = TilePyramid::tileRect(r0, candidate);
				if (r.left() < area.right() && r.right() > area.left() && r.top() < area.bottom() && r.bottom() > area.top())
					expected.push_back(candidate);
			}
		QCOMPARE(tiles.size(), expected.size());
		for (auto const& k : expected)
			QVERIFY(std::find(tiles.begin(), tiles.end(), k) != tiles.end());
	}

	void requestsAndPlacementTest()
	{
		// Accept (2), (3), (8)
		Map map;
		setupMap(map);
		auto temp = std::make_unique<TileServiceTemplate>(xyzService(stub), &map);
		auto* raw = temp.get();
		map.addTemplate(0, std::move(temp));
		QVERIFY2(raw->setupAndLoad(nullptr, nullptr), qPrintable(raw->errorString()));
		QCOMPARE(raw->getTemplateState(), Template::Loaded);
		QVERIFY(raw->isTemplateGeoreferenced());

		// The base resolution is the web-mercator pixel at zoom 16 on the ground at 60 N
		auto const r0 = raw->baseResolution();
		QVERIFY(std::abs(r0 - 2 * merc_half / (65536 * 256) * std::cos(qDegreesToRadians(60.0))) < 0.02);

		// 1,000 by 800 pixels at 1 m per pixel: 10 px per mm at 1:10,000
		View view{ QPointF(0, 0), 10.0, 1000, 800 };
		stub.requests.clear();
		paintTemplates(map, view);
		auto const requested = raw->lastRequests();
		QVERIFY(!requested.empty());
		auto const level = requested.front().level;
		QCOMPARE(level, TilePyramid::levelForResolution(r0, 1.0 / 1.0));
		auto const bounds = raw->projectedBounds(view.mapRect());
		auto const expected = TilePyramid::tilesIntersecting(r0, level, bounds);
		QCOMPARE(requested.size(), expected.size());
		for (auto const& key : expected)
			QVERIFY(std::find(requested.begin(), requested.end(), key) != requested.end());

		QTRY_VERIFY_WITH_TIMEOUT(raw->pendingCount() == 0, 60000);
		QVERIFY2(raw->lastTileError().isEmpty(), qPrintable(raw->lastTileError()));
		QCOMPARE(raw->cachedTileCount(), int(expected.size()));
		QVERIFY(!stub.requests.isEmpty());

		// The same view again requests nothing
		auto const image = paintTemplates(map, view);
		QVERIFY(raw->lastRequests().empty());

		// Panning by one tile width in projected x requests only the new column
		auto const tile_m = TilePyramid::tileExtent(r0, level);
		auto const& georef = map.getGeoreferencing();
		auto const shift = georef.projectedToMap().map(QPointF(tile_m, 0)) - georef.projectedToMap().map(QPointF(0, 0));
		View panned{ view.center_mm + shift, view.zoom, view.width, view.height };
		paintTemplates(map, panned);
		auto const new_requests = raw->lastRequests();
		auto const panned_bounds = raw->projectedBounds(panned.mapRect());
		auto const panned_tiles = TilePyramid::tilesIntersecting(r0, level, panned_bounds);
		std::vector<TileKey> new_column;
		for (auto const& key : panned_tiles)
			if (std::find(expected.begin(), expected.end(), key) == expected.end())
				new_column.push_back(key);
		QVERIFY(!new_column.empty());
		QCOMPARE(new_requests.size(), new_column.size());
		for (auto const& key : new_column)
		{
			QVERIFY(std::find(new_requests.begin(), new_requests.end(), key) != new_requests.end());
			QCOMPARE(key.column, new_column.front().column);
		}
		QTRY_VERIFY_WITH_TIMEOUT(raw->pendingCount() == 0, 60000);

		// Placement: colour at points away from cell edges
		int checked = 0;
		for (int y = 10; y < view.height - 10; y += 23)
			for (int x = 10; x < view.width - 10; x += 29)
			{
				auto const pixel = QPointF(x + 0.5, y + 0.5);
				if (!awayFromEdge(map, view, pixel, 2.0))
					continue;
				auto const dark = expectedDark(map, view, pixel);
				auto const g = grey(image, x, y);
				QVERIFY2(dark == (g < 128), qPrintable(QStringLiteral("pixel %1,%2: grey %3, expected %4").arg(x).arg(y).arg(g).arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))));
				++checked;
			}
		QVERIFY(checked > 300);

		// Placement: the 50 percent crossing of a vertical edge within one pixel of the truth
		int edges = 0;
		for (int y = 100; y < view.height - 100; y += 150)
		{
			for (int x = 100; x < view.width - 100; ++x)
			{
				auto const row = y + 0.5;
				auto const left_dark = expectedDark(map, view, QPointF(x, row));
				if (expectedDark(map, view, QPointF(x + 1.0, row)) == left_dark)
					continue;
				// A cell edge crosses between x and x+1: bisect for its position
				double lo = x, hi = x + 1.0;
				for (int i = 0; i < 20; ++i)
				{
					auto const mid = 0.5 * (lo + hi);
					(expectedDark(map, view, QPointF(mid, row)) == left_dark ? lo : hi) = mid;
				}
				auto const true_edge = 0.5 * (lo + hi);
				// The edge must be isolated: the same colours 4 px to either side,
				// and no horizontal cell edge within 2.5 px of the scan line
				if (expectedDark(map, view, QPointF(x - 4.0, row)) != left_dark
				    || expectedDark(map, view, QPointF(x + 5.0, row)) == left_dark
				    || expectedDark(map, view, QPointF(x - 2.0, row - 2.5)) != left_dark
				    || expectedDark(map, view, QPointF(x - 2.0, row + 2.5)) != left_dark
				    || expectedDark(map, view, QPointF(x + 3.0, row - 2.5)) == left_dark
				    || expectedDark(map, view, QPointF(x + 3.0, row + 2.5)) == left_dark)
					continue;
				// Rendered crossing: linear interpolation of grey across 128
				double rendered = -1;
				for (int px = x - 3; px <= x + 3; ++px)
				{
					auto const g0 = grey(image, px, y);
					auto const g1 = grey(image, px + 1, y);
					if ((g0 < 128) != (g1 < 128))
					{
						rendered = px + 0.5 + (128.0 - g0) / double(g1 - g0);
						break;
					}
				}
				QVERIFY2(rendered >= 0, qPrintable(QStringLiteral("no crossing rendered near %1,%2").arg(x).arg(y)));
				QVERIFY2(std::abs(rendered - true_edge) < 1.0, qPrintable(QStringLiteral("edge at %1 rendered at %2 in row %3").arg(true_edge).arg(rendered).arg(y)));
				++edges;
				x += 5;
			}
		}
		QVERIFY(edges > 20);

		// Export paints cached tiles and requests nothing
		auto const exported = paintTemplates(map, view, false);
		QVERIFY(raw->lastRequests().empty());
		QCOMPARE(grey(exported, 500, 400) < 128, grey(image, 500, 400) < 128);

		// Accept (8): closing the map stops the worker within one second
		QElapsedTimer timer;
		timer.start();
		map.deleteTemplate(0);
		QVERIFY(timer.elapsed() < 1000);
	}

	void colourCheck(Map& map, TileServiceTemplate* raw, const View& view)
	{
		paintTemplates(map, view);
		QTRY_VERIFY_WITH_TIMEOUT(raw->pendingCount() == 0, 60000);
		QVERIFY2(raw->lastTileError().isEmpty(), qPrintable(raw->lastTileError() + QStringLiteral(" Requests: ") + stub.requests.mid(0, 3).join(QStringLiteral(" | "))));
		auto const image = paintTemplates(map, view);
		int checked = 0;
		for (int y = 10; y < view.height - 10; y += 23)
			for (int x = 10; x < view.width - 10; x += 29)
			{
				auto const pixel = QPointF(x + 0.5, y + 0.5);
				if (!awayFromEdge(map, view, pixel, 2.0))
					continue;
				auto const dark = expectedDark(map, view, pixel);
				auto const g = grey(image, x, y);
				QVERIFY2(dark == (g < 128), qPrintable(QStringLiteral("pixel %1,%2: grey %3").arg(x).arg(y).arg(g)));
				++checked;
			}
		QVERIFY(checked > 100);
	}

	void wmsTest()
	{
		// Accept (4)
		Map map;
		setupMap(map);
		MapService service;
		service.kind = MapService::Wms;
		service.name = QStringLiteral("Checkerboard WMS");
		service.url = stub.baseUrl() + QStringLiteral("/wms?");
		service.version = QStringLiteral("1.3.0");
		service.layers = QStringLiteral("checker");
		service.crs = QStringLiteral("EPSG:3857");
		service.bbox = QRectF(QPointF(-merc_half, -merc_half), QPointF(merc_half, merc_half));
		service.resolution = 1.0;
		auto temp = std::make_unique<TileServiceTemplate>(service, &map);
		auto* raw = temp.get();
		map.addTemplate(0, std::move(temp));
		QVERIFY2(raw->setupAndLoad(nullptr, nullptr), qPrintable(raw->errorString()));
		stub.requests.clear();
		colourCheck(map, raw, View{ QPointF(0, 0), 10.0, 600, 400 });
		QVERIFY(std::any_of(stub.requests.begin(), stub.requests.end(), [](const QString& r) { return r.contains(QLatin1String("request=GetMap"), Qt::CaseInsensitive); }));
	}

	void wmtsTest()
	{
		// Accept (4)
		Map map;
		setupMap(map);
		MapService service;
		service.kind = MapService::Wmts;
		service.name = QStringLiteral("Checkerboard WMTS");
		service.url = stub.baseUrl() + QStringLiteral("/wmts/WMTSCapabilities.xml");
		service.layers = QStringLiteral("checker");
		service.matrix_set = QStringLiteral("GoogleMapsCompatible");
		service.crs = QStringLiteral("EPSG:3857");
		auto temp = std::make_unique<TileServiceTemplate>(service, &map);
		auto* raw = temp.get();
		map.addTemplate(0, std::move(temp));
		QVERIFY2(raw->setupAndLoad(nullptr, nullptr), qPrintable(raw->errorString()));
		stub.requests.clear();
		colourCheck(map, raw, View{ QPointF(0, 0), 10.0, 600, 400 });
		QVERIFY(std::any_of(stub.requests.begin(), stub.requests.end(), [](const QString& r) { return r.startsWith(QLatin1String("/wmts/")) && r.endsWith(QLatin1String(".png")); }));
	}

	void xmlRoundTripTest()
	{
		// Accept (5)
		Map map;
		setupMap(map);
		MapService service;
		service.kind = MapService::Wms;
		service.name = QStringLiteral("Orthophoto");
		service.url = QStringLiteral("https://example.test/wms?");
		service.version = QStringLiteral("1.1.1");
		service.layers = QStringLiteral("ortho,roads");
		service.styles = QStringLiteral("default,");
		service.format = QStringLiteral("image/jpeg");
		service.crs = QStringLiteral("EPSG:32614");
		service.bbox = QRectF(QPointF(166021.44, 0.0), QPointF(833978.56, 9329005.18));
		service.resolution = 0.25;
		service.matrix_set = QStringLiteral("");
		service.min_zoom = 2;
		service.max_zoom = 18;
		service.attribution = QStringLiteral("© Example agency");
		service.bulk_download = false;
		service.user = QStringLiteral("alice");
		service.password = QStringLiteral("s3cret");
		map.addTemplate(0, std::make_unique<TileServiceTemplate>(service, &map));

		XMLFileFormat format;
		QBuffer buffer;
		QVERIFY(buffer.open(QIODevice::ReadWrite));
		auto exporter = format.makeExporter({}, &map, nullptr);
		exporter->setDevice(&buffer);
		QVERIFY(exporter->doExport());
		auto const xml = QString::fromUtf8(buffer.data());
		QVERIFY(xml.contains(QLatin1String("type=\"TileServiceTemplate\"")));
		QVERIFY(xml.contains(QLatin1String("<service ")));
		QVERIFY(!xml.contains(QLatin1String("alice")));
		QVERIFY(!xml.contains(QLatin1String("s3cret")));

		QVERIFY(buffer.seek(0));
		Map reloaded;
		auto importer = format.makeImporter({}, &reloaded, nullptr);
		importer->setDevice(&buffer);
		QVERIFY(importer->doImport());
		QCOMPARE(reloaded.getNumTemplates(), 1);
		auto const* loaded = qobject_cast<const TileServiceTemplate*>(reloaded.getTemplate(0));
		QVERIFY(loaded);
		QVERIFY(loaded->isTemplateGeoreferenced());
		QCOMPARE(loaded->getTemplateFilename(), service.name);
		auto const& s = loaded->service();
		QCOMPARE(s.kind, service.kind);
		QCOMPARE(s.name, service.name);
		QCOMPARE(s.url, service.url);
		QCOMPARE(s.version, service.version);
		QCOMPARE(s.layers, service.layers);
		QCOMPARE(s.styles, service.styles);
		QCOMPARE(s.format, service.format);
		QCOMPARE(s.crs, service.crs);
		QCOMPARE(s.bbox, service.bbox);
		QCOMPARE(s.resolution, service.resolution);
		QCOMPARE(s.matrix_set, service.matrix_set);
		QCOMPARE(s.min_zoom, service.min_zoom);
		QCOMPARE(s.max_zoom, service.max_zoom);
		QCOMPARE(s.attribution, service.attribution);
		QCOMPARE(s.bulk_download, service.bulk_download);
		QVERIFY(s.user.isEmpty());
		QVERIFY(s.password.isEmpty());
		QVERIFY(s == service);
	}

	void repaintExtentTest()
	{
		// Accept (10)
		Map map;
		setupMap(map);
		auto* temp = new TileServiceTemplate(MapService::presets().front(), &map);
		map.addTemplate(0, std::unique_ptr<Template>(temp));
		auto const box = temp->calculateTemplateBoundingBox();
		QVERIFY(box.isValid());
		QVERIFY(QRectF(-1.0e6, -1.0e6, 2.0e6, 2.0e6).contains(box));
		QVERIFY(box.contains(QRectF(-1.0e4, -1.0e4, 2.0e4, 2.0e4)));
	}

	void capabilitiesTest()
	{
		// Accept (6)
		QFile wms_file(QStringLiteral("testdata:tiles/wms-capabilities-1.3.0.xml"));
		QVERIFY(wms_file.open(QIODevice::ReadOnly));
		auto const wms = ServiceCapabilities::parse(wms_file.readAll());
		QVERIFY2(wms.error.isEmpty(), qPrintable(wms.error));
		QCOMPARE(wms.kind, MapService::Wms);
		QCOMPARE(wms.version, QStringLiteral("1.3.0"));
		QCOMPARE(wms.title, QStringLiteral("Checkerboard test service"));
		QCOMPARE(wms.formats, QStringList({ QStringLiteral("image/png"), QStringLiteral("image/jpeg") }));
		QCOMPARE(wms.layers.size(), std::size_t(2));
		auto const* checker = wms.layer(QStringLiteral("checker"));
		QVERIFY(checker);
		QCOMPARE(checker->title, QStringLiteral("Checkerboard"));
		QCOMPARE(checker->systems, QStringList({ QStringLiteral("EPSG:3857"), QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:32614") }));
		QCOMPARE(checker->min_scale, 1000.0);
		QCOMPARE(checker->max_scale, 100000.0);
		QVERIFY(checker->bboxes.contains(QStringLiteral("EPSG:3857")));
		QVERIFY(checker->bboxes.contains(QStringLiteral("EPSG:32614")));
		QCOMPARE(checker->bboxes.value(QStringLiteral("EPSG:32614")).left(), 166021.44);
		QCOMPARE(checker->bboxes.value(QStringLiteral("EPSG:32614")).bottom(), 9329005.18);
		auto const* plain = wms.layer(QStringLiteral("plain"));
		QVERIFY(plain);
		QCOMPARE(plain->systems, QStringList({ QStringLiteral("EPSG:3857"), QStringLiteral("EPSG:4326") }));
		QCOMPARE(plain->min_scale, 0.0);

		QFile wmts_file(QStringLiteral("testdata:tiles/wmts-capabilities.xml"));
		QVERIFY(wmts_file.open(QIODevice::ReadOnly));
		auto const wmts = ServiceCapabilities::parse(wmts_file.readAll());
		QVERIFY2(wmts.error.isEmpty(), qPrintable(wmts.error));
		QCOMPARE(wmts.kind, MapService::Wmts);
		QCOMPARE(wmts.version, QStringLiteral("1.0.0"));
		QCOMPARE(wmts.layers.size(), std::size_t(1));
		auto const* layer = wmts.layer(QStringLiteral("checker"));
		QVERIFY(layer);
		QCOMPARE(layer->title, QStringLiteral("Checkerboard"));
		QCOMPARE(layer->formats, QStringList({ QStringLiteral("image/png") }));
		QCOMPARE(layer->matrix_sets, QStringList({ QStringLiteral("GoogleMapsCompatible") }));
		QCOMPARE(wmts.matrix_set_systems.value(QStringLiteral("GoogleMapsCompatible")), QStringLiteral("EPSG:3857"));
		QCOMPARE(layer->systems, QStringList({ QStringLiteral("EPSG:3857") }));

		auto const broken = ServiceCapabilities::parse("<html>not a service</html>");
		QVERIFY(!broken.error.isEmpty());
	}

	void downloadTest()
	{
		// Accept (7)
		Map map;
		setupMap(map);
		auto temp = std::make_unique<TileServiceTemplate>(xyzService(stub), &map);
		auto* raw = temp.get();
		map.addTemplate(0, std::move(temp));
		QVERIFY2(raw->setupAndLoad(nullptr, nullptr), qPrintable(raw->errorString()));

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const& georef = map.getGeoreferencing();
		auto const center = georef.toProjectedCoords(MapCoordF(0, 0));
		QRectF const window(center.x() - 1000.0, center.y() - 800.0, 2000.0, 1600.0);

		// Refused: 50 km at 2 m per pixel is 25,000 pixels
		auto const refused_path = dir.filePath(QStringLiteral("refused.tif"));
		QString error;
		QVERIFY(!raw->downloadExtent(QRectF(center.x() - 25000.0, center.y() - 25000.0, 50000.0, 50000.0), 2.0, refused_path, &error));
		QVERIFY(!error.isEmpty());
		QVERIFY(!QFileInfo::exists(refused_path));

		auto const path = dir.filePath(QStringLiteral("offline.tif"));
		QSignalSpy finished(raw, &TileServiceTemplate::downloadFinished);
		QVERIFY2(raw->downloadExtent(window, 2.0, path, &error), qPrintable(error));
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 120000);
		QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
		QVERIFY(QFileInfo::exists(path));

		auto dataset = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterXSize(dataset), 1000);
		QCOMPARE(GDALGetRasterYSize(dataset), 800);
		double gt[6];
		QCOMPARE(GDALGetGeoTransform(dataset, gt), CE_None);
		QVERIFY(std::abs(gt[0] - window.left()) < 1e-6);
		QVERIFY(std::abs(gt[3] - window.bottom()) < 1e-6);
		QVERIFY(std::abs(gt[1] - 2.0) < 1e-9);
		QVERIFY(std::abs(gt[5] + 2.0) < 1e-9);
		auto const* wkt = GDALGetProjectionRef(dataset);
		QVERIFY(wkt && QString::fromUtf8(wkt).contains(QLatin1String("UTM")));
		int block_x = 0, block_y = 0;
		GDALGetBlockSize(GDALGetRasterBand(dataset, 1), &block_x, &block_y);
		QVERIFY(block_y > 1);  // tiled, not striped

		// Colours at sample points of the GeoTIFF
		int checked = 0;
		for (int row = 5; row < 800; row += 37)
			for (int col = 5; col < 1000; col += 41)
			{
				auto const px = gt[0] + (col + 0.5) * gt[1];
				auto const py = gt[3] + (row + 0.5) * gt[5];
				auto const latlon = georef.toGeographicCoords(QPointF(px, py));
				auto const merc = toMercator(latlon);
				// Skip points near a cell edge (4 m in mercator metres is 2 pixels on the ground here)
				bool near_edge = false;
				for (auto const& offset : { QPointF(4, 0), QPointF(-4, 0), QPointF(0, 4), QPointF(0, -4) })
					near_edge |= checkerDark(merc + offset) != checkerDark(merc);
				if (near_edge)
					continue;
				quint8 value = 0;
				QCOMPARE(GDALRasterIO(GDALGetRasterBand(dataset, 1), GF_Read, col, row, 1, 1, &value, 1, 1, GDT_Byte, 0, 0), CE_None);
				QVERIFY2(checkerDark(merc) == (value < 128), qPrintable(QStringLiteral("pixel %1,%2: %3").arg(col).arg(row).arg(value)));
				++checked;
			}
		QVERIFY(checked > 150);
		GDALClose(dataset);
	}

	void localMapTest()
	{
		// Accept (9)
		Map map;
		setupMap(map, false);
		auto temp = std::make_unique<TileServiceTemplate>(xyzService(stub), &map);
		auto* raw = temp.get();
		map.addTemplate(0, std::move(temp));
		QVERIFY(!raw->setupAndLoad(nullptr, nullptr));
		QVERIFY(!raw->errorString().isEmpty());
		QCOMPARE(raw->getTemplateState(), Template::Invalid);
	}

private:
	QTemporaryDir cache_dir;
	TileStub stub;
};


QTEST_MAIN(TileTemplateTest)

#include "tile_template_t.moc"
