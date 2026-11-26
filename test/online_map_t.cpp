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

#include <array>
#include <cmath>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <gdal.h>
#include <ogr_srs_api.h>

#include "global.h"
#include "core/georeferencing.h"
#include "core/geoviewer.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "elevation/raster_products.h"
#include "gdal/map_service.h"
#include "gdal/online_map.h"
#include "templates/template.h"
#include "gdal/tile_service_template.h"

using namespace OpenOrienteering;


namespace {

/// The map of Accept (6): 1:10000, UTM 13, the projected point at map (0, 0).
Georeferencing utmGeoreferencing(const QPointF& reference)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setMapRefPoint(MapCoord(0.0, 0.0));
	georef.setProjectedRefPoint(reference, false, false);
	georef.setCombinedScaleFactor(1.0);
	georef.setGrivation(0.0);
	return georef;
}


/// Writes the tiles of zoom levels 0 to 2, 256 px each, in one colour.
bool writeTiles(const QString& root)
{
	for (int z = 0; z <= 2; ++z)
	{
		QImage tile(256, 256, QImage::Format_RGB32);
		tile.fill(QColor(40, 120, 200));
		for (int x = 0; x < (1 << z); ++x)
		{
			auto const dir = QStringLiteral("%1/%2/%3").arg(root).arg(z).arg(x);
			if (!QDir().mkpath(dir))
				return false;
			for (int y = 0; y < (1 << z); ++y)
			{
				if (!tile.save(QStringLiteral("%1/%2.png").arg(dir).arg(y)))
					return false;
			}
		}
	}
	return true;
}

/// The colour of a pixel of the GeoTIFF's three bands.
QColor pixelOf(GDALDatasetH dataset, int x, int y)
{
	std::array<int, 3> rgb {};
	for (int band = 0; band < 3; ++band)
	{
		unsigned char value = 0;
		if (GDALRasterIO(GDALGetRasterBand(dataset, band + 1), GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Byte, 0, 0) != CE_None)
			return {};
		rgb[std::size_t(band)] = value;
	}
	return QColor(rgb[0], rgb[1], rgb[2]);
}

double angleDifference(double a, double b)
{
	auto d = std::fmod(a - b, 360.0);
	if (d > 180)
		d -= 360;
	if (d < -180)
		d += 360;
	return std::abs(d);
}

}  // namespace


/**
 * Tests the online map and the geoviewer.
 *
 * See the design notes, "Online map and geoviewer".
 */
class OnlineMapTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
		GDALAllRegister();
		QSettings().remove(QStringLiteral("MapServices"));
		QSettings().remove(QStringLiteral("Geoviewer"));
	}

	void cleanupTestCase()
	{
		QSettings().remove(QStringLiteral("MapServices"));
		QSettings().remove(QStringLiteral("Geoviewer"));
	}


	/// Accept (1)
	void presetsTest()
	{
		auto const presets = MapService::onlinePresets();
		QStringList names;
		for (auto const& service : presets)
		{
			names << service.name;
			QVERIFY(!service.url.isEmpty());
			QVERIFY(!service.attribution.isEmpty());
		}
		QCOMPARE(names, QStringList({ QStringLiteral("USGS Imagery"), QStringLiteral("USGS Topo"),
		                              QStringLiteral("USGS NAIP imagery"), QStringLiteral("USGS 3DEP hillshade"), QStringLiteral("USGS 3DEP slope"),
		                              QStringLiteral("OpenStreetMap"), QStringLiteral("OpenTopoMap") }));
		QVERIFY(!presets[5].bulk_download);
		QCOMPARE(presets[0].kind, MapService::Xyz);
	}

	/// The program tells the servers its name, its version and where it comes from.
	void userAgentTest()
	{
		auto const agent = MapService::userAgent();
		QVERIFY2(agent.startsWith("FOOM/"), agent.constData());
		QVERIFY2(agent.endsWith(" (+https://github.com/matejsebo/foom)"), agent.constData());
		auto const presets = MapService::onlinePresets();
		QVERIFY(!presets.empty());
		QVERIFY(presets[0].gdalSource({}).contains("<UserAgent>" + agent + "</UserAgent>"));
	}

	/// The United States first, Accept (1) and (2): the National Map's WMS services.
	void usgsPresetsTest()
	{
		auto const presets = MapService::onlinePresets();
		auto const& naip = presets[2];
		QCOMPARE(naip.kind, MapService::Wms);
		QCOMPARE(naip.version, QStringLiteral("1.3.0"));
		QCOMPARE(naip.layers, QStringLiteral("USGSNAIPImagery:NaturalColor"));
		QCOMPARE(naip.format, QStringLiteral("image/jpeg"));
		QCOMPARE(naip.resolution, 0.3);
		QVERIFY(naip.bulk_download);
		auto const source = QString::fromUtf8(naip.gdalSource({}));
		QVERIFY(source.contains(QLatin1String("<Service name=\"WMS\">")));
		QVERIFY(source.contains(QLatin1String("<ServerUrl>https://imagery.nationalmap.gov/arcgis/services/USGSNAIPImagery/ImageServer/WMSServer?</ServerUrl>")));
		QVERIFY(source.contains(QLatin1String("<CRS>EPSG:3857</CRS>")));
		QVERIFY(source.contains(QLatin1String("<Layers>USGSNAIPImagery:NaturalColor</Layers>")));
		// A WMS gets a minute for a picture, a tile cache ten seconds.
		QVERIFY(source.contains(QLatin1String("<Timeout>60</Timeout>")));
		QVERIFY(QString::fromUtf8(presets[0].gdalSource({})).contains(QLatin1String("<Timeout>10</Timeout>")));
		auto const& hillshade = presets[3];
		auto const& slope = presets[4];
		QCOMPARE(hillshade.kind, MapService::Wms);
		QCOMPARE(hillshade.url, slope.url);
		QCOMPARE(hillshade.resolution, 1.0);
		QCOMPARE(hillshade.format, QStringLiteral("image/png"));
		QVERIFY(QString::fromUtf8(hillshade.gdalSource({})).contains(QLatin1String("<Layers>3DEPElevation:Hillshade Multidirectional</Layers>")));
		QVERIFY(QString::fromUtf8(slope.gdalSource({})).contains(QLatin1String("<Layers>3DEPElevation:Slope Map</Layers>")));

		// A new user's list of map services ends with the three, so that they can be templates;
		// OpenStreetMap stays the first.
		auto const shipped = MapService::presets();
		QVERIFY(shipped.size() >= 5);
		QCOMPARE(shipped.front().name, QStringLiteral("OpenStreetMap"));
		for (std::size_t i = 0; i < 3; ++i)
			QVERIFY(shipped[shipped.size() - 3 + i] == presets[2 + i]);
	}

	/// The United States first, Accept (4): a list saved before the USGS services were shipped gets them once.
	void savedListTest()
	{
		auto const osm = MapService::presets().front();
		MapService mine;
		mine.kind = MapService::Wms;
		mine.name = QStringLiteral("My WMS");
		mine.url = QStringLiteral("https://example.org/wms");
		MapService::toSettings({ osm, mine });
		QSettings().remove(QStringLiteral("MapServicePresets"));  // as a list of before 2026-11-24
		auto list = MapService::fromSettings();
		QCOMPARE(int(list.size()), 5);
		QCOMPARE(list[1].name, QStringLiteral("My WMS"));
		QCOMPARE(list[2].name, QStringLiteral("USGS NAIP imagery"));
		QCOMPARE(list[4].name, QStringLiteral("USGS 3DEP slope"));
		// The user takes one out: it stays out.
		list.erase(list.begin() + 3);
		MapService::toSettings(list);
		QCOMPARE(int(MapService::fromSettings().size()), 4);
		QCOMPARE(int(MapService::fromSettings().size()), 4);
		// A list which has one of them already keeps its own.
		auto renamed = MapService::presets().back();
		renamed.attribution = QStringLiteral("mine");
		MapService::toSettings({ osm, renamed });
		QSettings().remove(QStringLiteral("MapServicePresets"));
		list = MapService::fromSettings();
		QCOMPARE(int(list.size()), 4);
		QCOMPARE(list[1].attribution, QStringLiteral("mine"));
		QSettings().remove(QStringLiteral("MapServices"));
		QSettings().remove(QStringLiteral("MapServicePresets"));
	}

	/// Accept (2)
	void userServicesTest()
	{
		auto const osm = MapService::presets().front();
		MapService mine;
		mine.kind = MapService::Wms;
		mine.name = QStringLiteral("My WMS");
		mine.url = QStringLiteral("https://example.org/wms");
		MapService::toSettings({ osm, mine });
		auto const all = MapService::onlineMapServices();
		auto const presets = MapService::onlinePresets();
		QCOMPARE(all.size(), presets.size() + 1);
		QCOMPARE(all.back().name, QStringLiteral("My WMS"));
		QSettings().remove(QStringLiteral("MapServices"));
	}

	/// Accept (3)
	void saveTest()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QImage image(200, 100, QImage::Format_RGB32);
		image.fill(QColor(30, 140, 60));
		auto const extent = QRectF(950000, 500000, 400, 200);
		auto const plain = dir.filePath(QStringLiteral("plain.tif"));
		QString error;
		QVERIFY2(saveOnlineMapImage(image, extent, QStringLiteral("EPSG:26954"), {}, plain, &error), qPrintable(error));

		auto dataset = GDALOpen(plain.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterXSize(dataset), 200);
		QCOMPARE(GDALGetRasterYSize(dataset), 100);
		double gt[6] = {};
		QCOMPARE(GDALGetGeoTransform(dataset, gt), CE_None);
		QCOMPARE(gt[0], 950000.0);
		QCOMPARE(gt[1], 2.0);
		QCOMPARE(gt[3], 500200.0);
		QCOMPARE(gt[5], -2.0);
		auto srs = OSRNewSpatialReference(GDALGetProjectionRef(dataset));
		OSRAutoIdentifyEPSG(srs);
		QCOMPARE(QString::fromLatin1(OSRGetAuthorityCode(srs, nullptr)), QStringLiteral("26954"));
		OSRDestroySpatialReference(srs);
		QCOMPARE(pixelOf(dataset, 199, 99), QColor(30, 140, 60));
		GDALClose(dataset);

		// With an attribution the lower right differs, the rest not.
		auto const attributed = dir.filePath(QStringLiteral("attributed.tif"));
		QVERIFY(saveOnlineMapImage(image, extent, QStringLiteral("EPSG:26954"), QStringLiteral("© Test"), attributed, &error));
		dataset = GDALOpen(attributed.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QVERIFY(pixelOf(dataset, 197, 97) != QColor(30, 140, 60));
		QCOMPARE(pixelOf(dataset, 5, 5), QColor(30, 140, 60));
		GDALClose(dataset);

		// Nothing shown, nothing written.
		QVERIFY(!saveOnlineMapImage(QImage(), extent, QStringLiteral("EPSG:26954"), {}, dir.filePath(QStringLiteral("none.tif")), &error));
		QVERIFY(!error.isEmpty());
	}

	/// Accept (3), the proposed file name
	void proposedPathTest()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const map_path = dir.filePath(QStringLiteral("forest.foom"));
		QCOMPARE(proposedOnlineMapPath(map_path), dir.filePath(QStringLiteral("forest_online_1.tif")));
		QFile file(dir.filePath(QStringLiteral("forest_online_1.tif")));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.close();
		QCOMPARE(proposedOnlineMapPath(map_path), dir.filePath(QStringLiteral("forest_online_2.tif")));
		QVERIFY(proposedOnlineMapPath({}).endsWith(QLatin1String("online_1.tif")) || proposedOnlineMapPath({}).contains(QLatin1String("online_")));
	}

	/// Accept (4)
	void attachTest()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Map map;
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"));
		georef.setMapRefPoint(MapCoord(0.0, 0.0));
		georef.setProjectedRefPoint(QPointF(950200.0, 500100.0), false, false);
		georef.setCombinedScaleFactor(1.0);
		georef.setGrivation(0.0);
		map.setGeoreferencing(georef);
		QCOMPARE(map.getGeoreferencing().getState(), Georeferencing::Geospatial);

		QImage image(200, 100, QImage::Format_RGB32);
		image.fill(Qt::darkGreen);
		auto const extent = QRectF(950000, 500000, 400, 200);
		auto const path = dir.filePath(QStringLiteral("online.tif"));
		QString error;
		QVERIFY2(saveOnlineMapImage(image, extent, map.getGeoreferencing().getProjectedCRSSpec(), {}, path, &error), qPrintable(error));
		auto* temp = attachProductTemplate(map, path, &error);
		QVERIFY2(temp, qPrintable(error));
		QCOMPARE(map.getNumTemplates(), 1);
		// 400 by 200 m at 1:10000 is 40 by 20 mm about the map's origin.
		auto const box = temp->boundingRect();
		QVERIFY2(std::abs(box.left() + 20.0) < 0.2 && std::abs(box.right() - 20.0) < 0.2, qPrintable(QString::number(box.left())));
		QVERIFY2(std::abs(box.top() + 10.0) < 0.2 && std::abs(box.bottom() - 10.0) < 0.2, qPrintable(QString::number(box.top())));
	}


	/// The download of 2026-11-23: the area shown at a chosen resolution.
	void proposedResolutionTest()
	{
		QCOMPARE(proposedOnlineMapResolution(QRectF(0, 0, 12000, 9000)), 1.0);
		QCOMPARE(proposedOnlineMapResolution(QRectF(0, 0, 40000, 30000)), 2.1);
		QVERIFY(40000 / proposedOnlineMapResolution(QRectF(0, 0, 40000, 30000)) < TileServiceTemplate::max_download_pixels);
	}

	void downloadTest()
	{
		QString reason;
		if (!TileServiceTemplate::driversAvailable(&reason))
			QSKIP(qPrintable(reason));
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const tiles = dir.filePath(QStringLiteral("tiles"));
		QVERIFY(writeTiles(tiles));
		MapService service;
		service.kind = MapService::Xyz;
		service.name = QStringLiteral("Offline tiles");
		service.url = QUrl::fromLocalFile(tiles).toString() + QStringLiteral("/${z}/${x}/${y}.png");
		service.max_zoom = 2;
		service.attribution = QStringLiteral("Test tiles");

		Map map;
		map.setGeoreferencing(utmGeoreferencing(QPointF(500000.0, 5200000.0)));
		QRectF const extent(499000.0, 5199200.0, 2000.0, 1600.0);
		auto const path = dir.filePath(QStringLiteral("download.tif"));
		QString error;
		QVERIFY2(downloadOnlineMap(map, service, extent, 2.0, path, nullptr, &error), qPrintable(error));
		auto dataset = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterXSize(dataset), 1000);
		QCOMPARE(GDALGetRasterYSize(dataset), 800);
		double gt[6] = {};
		QCOMPARE(GDALGetGeoTransform(dataset, gt), CE_None);
		QVERIFY(std::abs(gt[0] - 499000.0) < 1e-6 && std::abs(gt[3] - 5200800.0) < 1e-6 && std::abs(gt[1] - 2.0) < 1e-9);
		GDALClose(dataset);
		QCOMPARE(map.getNumTemplates(), 1);
		// 2 by 1.6 km at 1:10000 is 200 by 160 mm about the map's origin.
		auto const box = map.getTemplate(0)->boundingRect();
		QVERIFY2(std::abs(box.left() + 100.0) < 0.5 && std::abs(box.right() - 100.0) < 0.5, qPrintable(QString::number(box.left())));
		QVERIFY2(std::abs(box.top() + 80.0) < 0.5 && std::abs(box.bottom() - 80.0) < 0.5, qPrintable(QString::number(box.top())));

		// An extent of no whole number of pixels gets whole pixels, square ones.
		auto const uneven = dir.filePath(QStringLiteral("uneven.tif"));
		QVERIFY2(downloadOnlineMap(map, service, QRectF(499000.0, 5199200.0, 2001.3, 1600.7), 2.0, uneven, nullptr, &error), qPrintable(error));
		dataset = GDALOpen(uneven.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterXSize(dataset), 1001);
		QCOMPARE(GDALGetRasterYSize(dataset), 801);
		QCOMPARE(GDALGetGeoTransform(dataset, gt), CE_None);
		QVERIFY(std::abs(gt[1] - 2.0) < 1e-9 && std::abs(gt[5] + 2.0) < 1e-9);
		GDALClose(dataset);
		QCOMPARE(map.getNumTemplates(), 2);

		// More than the limit on a side is refused, and nothing is attached.
		QVERIFY(!downloadOnlineMap(map, service, QRectF(450000.0, 5150000.0, 100000.0, 1000.0), 2.0,
		                           dir.filePath(QStringLiteral("refused.tif")), nullptr, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(map.getNumTemplates(), 2);
	}

	/// Accept (5)
	void urlTest()
	{
		GeoviewerTarget target;
		target.latitude = 47.5;
		target.longitude = -106.4;
		target.direction = 450;
		QCOMPARE(geoviewerUrl(geoviewerDefaultPattern(), target).toString(),
		         QStringLiteral("https://maps.google.com/maps?q=&layer=c&cbll=47.500000,-106.400000&cbp=11,90,0,0,0"));
		target.direction = -10;
		QVERIFY(geoviewerUrl(geoviewerDefaultPattern(), target).toString().contains(QLatin1String("cbp=11,350,")));
		QCOMPARE(geoviewerUrl(QStringLiteral("https://example.org/viewer"), target).toString(), QStringLiteral("https://example.org/viewer"));

		// The setting, and the default when there is none.
		QCOMPARE(geoviewerPattern(), geoviewerDefaultPattern());
		setGeoviewerPattern(QStringLiteral("https://example.org/?ll={latitude},{longitude}"));
		QCOMPARE(geoviewerPattern(), QStringLiteral("https://example.org/?ll={latitude},{longitude}"));
		setGeoviewerPattern({});
		QCOMPARE(geoviewerPattern(), geoviewerDefaultPattern());
	}

	/// Accept (6)
	void targetTest()
	{
		// Off the central meridian, where the grid convergence is not 0.
		auto const georef = utmGeoreferencing(QPointF(600000.0, 5200000.0));
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		GeoviewerTarget target;
		auto const origin = MapCoordF(0.0, 0.0);
		QVERIFY(geoviewerTarget(georef, origin, origin, target));
		auto const expected = georef.toGeographicCoords(QPointF(600000.0, 5200000.0));
		QVERIFY(std::abs(target.latitude - expected.latitude()) < 1e-6);
		QVERIFY(std::abs(target.longitude - expected.longitude()) < 1e-6);
		QCOMPARE(target.direction, 0.0);

		// 100 m grid north is 10 mm up on the paper; it lies at the convergence from true north.
		auto const convergence = georef.getConvergence();
		QVERIFY(std::abs(convergence) > 0.5);
		QVERIFY(geoviewerTarget(georef, origin, MapCoordF(0.0, -10.0), target));
		QVERIFY2(angleDifference(target.direction, convergence) < 0.5, qPrintable(QStringLiteral("%1 %2").arg(target.direction).arg(convergence)));
		QVERIFY(geoviewerTarget(georef, origin, MapCoordF(10.0, 0.0), target));
		QVERIFY2(angleDifference(target.direction, 90.0 + convergence) < 0.5, qPrintable(QString::number(target.direction)));

		Georeferencing local;
		QVERIFY(!geoviewerTarget(local, origin, origin, target));
	}
};


QTEST_MAIN(OnlineMapTest)
#include "online_map_t.moc"  // IWYU pragma: keep
