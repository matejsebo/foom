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


#include <cmath>
#include <vector>

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/crs_template.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/prj_file.h"
#include "gui/georeferencing_dialog.h"
#include "gui/widgets/crs_selector.h"

using namespace OpenOrienteering;


namespace {

const char utm_wkt[] =
    "PROJCS[\"WGS_1984_UTM_Zone_13N\",GEOGCS[\"GCS_WGS_1984\",DATUM[\"D_WGS_1984\",SPHEROID[\"WGS_1984\",6378137.0,298.257223563]],"
    "PRIMEM[\"Greenwich\",0.0],UNIT[\"Degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
    "PARAMETER[\"False_Easting\",500000.0],PARAMETER[\"False_Northing\",0.0],PARAMETER[\"Central_Meridian\",-105.0],"
    "PARAMETER[\"Scale_Factor\",0.9996],PARAMETER[\"Latitude_Of_Origin\",0.0],UNIT[\"Meter\",1.0]]";

const char plane_wkt[] =
    "PROJCS[\"NAD_1983_StatePlane_Colorado_Central_FIPS_0502\",GEOGCS[\"GCS_North_American_1983\",DATUM[\"D_North_American_1983\",SPHEROID[\"GRS_1980\",6378137.0,298.257222101]],"
    "PRIMEM[\"Greenwich\",0.0],UNIT[\"Degree\",0.0174532925199433]],PROJECTION[\"Lambert_Conformal_Conic\"],"
    "PARAMETER[\"False_Easting\",914401.8289],PARAMETER[\"False_Northing\",304800.6096],PARAMETER[\"Central_Meridian\",-105.5],"
    "PARAMETER[\"Standard_Parallel_1\",39.75],PARAMETER[\"Standard_Parallel_2\",38.45],"
    "PARAMETER[\"Latitude_Of_Origin\",37.8333333333333],UNIT[\"Meter\",1.0]]";

const char custom_wkt[] =
    "PROJCS[\"Local_TM\",GEOGCS[\"GCS_WGS_1984\",DATUM[\"D_WGS_1984\",SPHEROID[\"WGS_1984\",6378137.0,298.257223563]],"
    "PRIMEM[\"Greenwich\",0.0],UNIT[\"Degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
    "PARAMETER[\"False_Easting\",123456.0],PARAMETER[\"False_Northing\",0.0],PARAMETER[\"Central_Meridian\",-103.5],"
    "PARAMETER[\"Scale_Factor\",1.0],PARAMETER[\"Latitude_Of_Origin\",0.0],UNIT[\"Meter\",1.0]]";

const char geographic_wkt[] =
    "GEOGCS[\"GCS_WGS_1984\",DATUM[\"D_WGS_1984\",SPHEROID[\"WGS_1984\",6378137.0,298.257223563]],"
    "PRIMEM[\"Greenwich\",0.0],UNIT[\"Degree\",0.0174532925199433]]";

}  // namespace



/**
 * @test Tests reading the coordinate reference system of a .prj file.
 *
 * See the design notes, "Projection file".
 */
class PrjFileTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString writeFile(const char* name, const QByteArray& content) const
	{
		auto const path = QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(content) != content.size())
			return {};
		return path;
	}
	
private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
	}
	
	/// Accept (1)
	void epsgTest()
	{
		auto crs = crsFromWkt(QByteArray(utm_wkt));
		QVERIFY2(crs.error.isEmpty(), qPrintable(crs.error));
		QCOMPARE(crs.epsg, QStringLiteral("32613"));
		crs = crsFromWkt(QByteArray(plane_wkt));
		QVERIFY2(crs.error.isEmpty(), qPrintable(crs.error));
		QCOMPARE(crs.epsg, QStringLiteral("26954"));
	}
	
	/// Accept (2)
	void customTest()
	{
		auto const crs = crsFromWkt(QByteArray(custom_wkt));
		QVERIFY2(crs.error.isEmpty(), qPrintable(crs.error));
		QVERIFY(crs.epsg.isEmpty());
		QVERIFY2(crs.proj.contains(QLatin1String("+proj=tmerc")), qPrintable(crs.proj));
		QVERIFY2(crs.proj.contains(QLatin1String("+lon_0=-103.5")), qPrintable(crs.proj));
		QVERIFY2(crs.proj.contains(QLatin1String("+x_0=123456")), qPrintable(crs.proj));
		QVERIFY2(!crs.proj.contains(QLatin1String("+type=crs")), qPrintable(crs.proj));
		
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		QVERIFY(georef.setProjectedCRS(QStringLiteral("PROJ.4"), crs.proj, { crs.proj }));
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		auto ok = false;
		auto const place = georef.toGeographicCoords(QPointF(123456.0, 5000000.0), &ok);
		QVERIFY(ok);
		QVERIFY2(std::abs(place.longitude() + 103.5) < 1e-9, qPrintable(QString::number(place.longitude(), 'f', 12)));
	}
	
	/// Accept (3)
	void refusedTest()
	{
		QVERIFY(!crsFromWkt(QByteArray(geographic_wkt)).error.isEmpty());
		QVERIFY(!crsFromWkt(QByteArray("hello")).error.isEmpty());
		QVERIFY(!readPrjFile(QDir(dir.path()).absoluteFilePath(QStringLiteral("missing.prj"))).error.isEmpty());
	}
	
	/// Accept (4)
	void fileTest()
	{
		auto const path = writeFile("utm.prj", QByteArray("\xef\xbb\xbf") + utm_wkt + '\n');
		auto const crs = readPrjFile(path);
		QVERIFY2(crs.error.isEmpty(), qPrintable(crs.error));
		QCOMPARE(crs.epsg, QStringLiteral("32613"));
	}
	
	/// Accept (5)
	void dialogTest()
	{
		Map map;
		GeoreferencingDialog dialog(nullptr, &map);
		auto* selector = dialog.findChild<CRSSelector*>();
		QVERIFY(selector);
		
		QString error;
		QVERIFY2(dialog.loadPrjFile(writeFile("d-utm.prj", utm_wkt), &error), qPrintable(error));
		QVERIFY(selector->currentCRSTemplate());
		QCOMPARE(selector->currentCRSTemplate()->id(), QStringLiteral("EPSG"));
		QCOMPARE(selector->parameters(), std::vector<QString>{ QStringLiteral("32613") });
		
		auto const custom = crsFromWkt(QByteArray(custom_wkt));
		QVERIFY(dialog.loadPrjFile(writeFile("d-custom.prj", custom_wkt), &error));
		QCOMPARE(selector->currentCRSTemplate()->id(), QStringLiteral("PROJ.4"));
		QCOMPARE(selector->parameters(), std::vector<QString>{ custom.proj });
		
		QVERIFY(!dialog.loadPrjFile(writeFile("d-geo.prj", geographic_wkt), &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(selector->currentCRSTemplate()->id(), QStringLiteral("PROJ.4"));
	}
};


QTEST_MAIN(PrjFileTest)
#include "prj_file_t.moc"  // IWYU pragma: keep
