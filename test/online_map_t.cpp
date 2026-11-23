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
		                              QStringLiteral("OpenStreetMap"), QStringLiteral("OpenTopoMap") }));
		QVERIFY(!presets[2].bulk_download);
		QCOMPARE(presets[0].kind, MapService::Xyz);
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
