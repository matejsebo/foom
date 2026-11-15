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
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include "test_config.h"

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gdal/geodata_import.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

constexpr double ref_e = 500000.0;
constexpr double ref_n = 5000000.0;

using Points = std::vector<QPointF>;
using Fields = std::vector<std::pair<const char*, const char*>>;

void addFeature(OGRLayerH layer, OGRwkbGeometryType type, const Points& points, const Fields& fields = {})
{
	auto* feature = OGR_F_Create(OGR_L_GetLayerDefn(layer));
	for (auto const& field : fields)
	{
		auto const index = OGR_F_GetFieldIndex(feature, field.first);
		if (index >= 0)
			OGR_F_SetFieldString(feature, index, field.second);
	}
	OGRGeometryH geometry = nullptr;
	if (type == wkbPolygon)
	{
		auto* ring = OGR_G_CreateGeometry(wkbLinearRing);
		for (auto const& p : points)
			OGR_G_AddPoint_2D(ring, p.x(), p.y());
		OGR_G_CloseRings(ring);
		geometry = OGR_G_CreateGeometry(wkbPolygon);
		OGR_G_AddGeometryDirectly(geometry, ring);
	}
	else if (type == wkbPoint)
	{
		geometry = OGR_G_CreateGeometry(wkbPoint);
		OGR_G_SetPoint_2D(geometry, 0, points.front().x(), points.front().y());
	}
	else
	{
		geometry = OGR_G_CreateGeometry(wkbLineString);
		for (auto const& p : points)
			OGR_G_AddPoint_2D(geometry, p.x(), p.y());
	}
	OGR_F_SetGeometryDirectly(feature, geometry);
	OGR_L_CreateFeature(layer, feature);
	OGR_F_Destroy(feature);
}

void addStringField(OGRLayerH layer, const char* name)
{
	auto* field = OGR_Fld_Create(name, OFTString);
	OGR_L_CreateField(layer, field, TRUE);
	OGR_Fld_Destroy(field);
}

/// Road C: 200 vertices, wiggling by 0.3 m, so simplification at 0.5 mm (5 m) removes most.
Points roadC()
{
	Points points;
	for (int i = 0; i < 200; ++i)
		points.emplace_back(499000.0 + 2.0 * i, 4999000.0 + 0.3 * std::sin(i * 0.5));
	return points;
}

void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(ref_e, ref_n), false, false);
	map.setGeoreferencing(georef);
	auto* road = new LineSymbol();
	road->setName(QStringLiteral("Road"));
	road->setNumberComponent(0, 501);
	road->setLineWidth(0.35);
	map.addSymbol(road, 0);
}

std::vector<Object*> allObjects(Map& map)
{
	std::vector<Object*> result;
	map.applyOnAllObjects([&result](Object* object) { result.push_back(object); });
	return result;
}

Object* findByTag(Map& map, const QString& key, const QString& value)
{
	for (auto* object : allObjects(map))
	{
		if (object->getTag(key) == value)
			return object;
	}
	return nullptr;
}

/// The distance from p to the nearest first vertex of any object.
double nearestFirstVertex(Map& map, const QPointF& p)
{
	auto best = 1e9;
	for (auto* object : allObjects(map))
		best = std::min(best, QLineF(QPointF(MapCoordF(object->getRawCoordinateVector().front())), p).length());
	return best;
}

int countType(Map& map, Object::Type type)
{
	auto const objects = allObjects(map);
	return int(std::count_if(begin(objects), end(objects), [type](const Object* o) { return o->getType() == type; }));
}

MapCoordF expected(const Map& map, double e, double n)
{
	return map.getGeoreferencing().toMapCoordF(QPointF(e, n));
}

double distance(const MapCoordF& a, const MapCoordF& b)
{
	return QLineF(QPointF(a), QPointF(b)).length();
}

double distanceToPolyline(const QPointF& p, const MapCoordVector& coords)
{
	auto best = 1e9;
	for (std::size_t i = 0; i + 1 < coords.size(); ++i)
	{
		auto const a = QPointF(MapCoordF(coords[i]));
		auto const b = QPointF(MapCoordF(coords[i + 1]));
		auto const ab = b - a;
		auto const len2 = QPointF::dotProduct(ab, ab);
		auto t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
		t = qBound(0.0, t, 1.0);
		best = std::min(best, QLineF(p, a + t * ab).length());
	}
	return best;
}

}  // namespace



/**
 * Tests the import options for geodata.
 *
 * See the design notes, "Import options", Accept.
 */
class GeodataImportTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		GDALAllRegister();
		QVERIFY(dir.isValid());
		writeGeoPackage();
		writeShapefile(plainShp(), { { { 500010, 5000020 }, { 500030, 5000040 } }, { { 500060, 5000060 }, { 500070, 5000090 } } });
		writeShapefile(smallShp(), { { { 10, 20 }, { 30, 40 } }, { { 50, 60 }, { 70, 90 } } });
		writeDxf();
	}

	/// Accept (1)
	void layersTest()
	{
		QString error;
		auto const layers = geodataLayers(gpkg(), &error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(int(layers.size()), 2);
		QCOMPARE(layers[0].name, QStringLiteral("roads"));
		QCOMPARE(layers[0].geometry, GeodataLayer::Line);
		QCOMPARE(layers[0].features, qint64(3));
		QCOMPARE(layers[1].name, QStringLiteral("buildings"));
		QCOMPARE(layers[1].geometry, GeodataLayer::Polygon);
		QCOMPARE(layers[1].features, qint64(2));
		QVERIFY(geodataAttributes(gpkg()).contains(QStringLiteral("kind")));
	}

	/// Accept (2)
	void layerFilterTest()
	{
		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.layers = QStringList{ QStringLiteral("buildings") };
		auto const result = importGeodata(map, { gpkg() }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 2);
		for (auto* object : allObjects(map))
		{
			QCOMPARE(object->getType(), Object::Path);
			QVERIFY(object->getSymbol()->getContainedTypes() & Symbol::Area);
		}
	}

	/// Accept (3)
	void detectedTest()
	{
		Map map;
		setupMap(map);
		auto const result = importGeodata(map, { gpkg() }, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 5);
		auto* road_b = findByTag(map, QStringLiteral("name"), QStringLiteral("B"));
		QVERIFY(road_b);
		auto const& coords = road_b->getRawCoordinateVector();
		QCOMPARE(int(coords.size()), 2);
		QVERIFY(distance(MapCoordF(coords[0]), expected(map, 500020, 5000020)) < 0.01);
		QVERIFY(distance(MapCoordF(coords[1]), expected(map, 500080, 5000020)) < 0.01);
	}

	/// Accept (4)
	void overrideTest()
	{
		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.crs_mode = GeodataImportOptions::CrsOverride;
		options.crs_spec = QStringLiteral("EPSG:32613");
		auto const result = importGeodata(map, { plainShp() }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 2);
		QVERIFY(nearestFirstVertex(map, QPointF(expected(map, 500010, 5000020))) < 0.01);
		QVERIFY(nearestFirstVertex(map, QPointF(expected(map, 500060, 5000060))) < 0.01);
	}

	/// Accept (5)
	void unreferencedTest_data()
	{
		QTest::addColumn<int>("units");
		QTest::addColumn<double>("custom");
		QTest::addColumn<double>("rotation");
		QTest::addColumn<QPointF>("offset");
		QTest::addColumn<QPointF>("first");

		QTest::newRow("1 mm per unit") << int(GeodataImportOptions::MillimetresOnPaper) << 1.0 << 0.0 << QPointF() << QPointF(10, -20);
		QTest::newRow("1 m per unit") << int(GeodataImportOptions::MetresOnGround) << 1.0 << 0.0 << QPointF() << QPointF(1, -2);
		QTest::newRow("custom 2.5") << int(GeodataImportOptions::CustomMillimetres) << 2.5 << 0.0 << QPointF() << QPointF(25, -50);
		QTest::newRow("rotated, offset") << int(GeodataImportOptions::MillimetresOnPaper) << 1.0 << 90.0 << QPointF(10, 20) << QPointF(-10, -30);
	}

	void unreferencedTest()
	{
		QFETCH(int, units);
		QFETCH(double, custom);
		QFETCH(double, rotation);
		QFETCH(QPointF, offset);
		QFETCH(QPointF, first);

		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.crs_mode = GeodataImportOptions::CrsUnreferenced;
		options.units = GeodataImportOptions::Units(units);
		options.custom_mm_per_unit = custom;
		options.rotation_deg = rotation;
		options.offset_mm = offset;
		auto const result = importGeodata(map, { smallShp() }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 2);
		auto const nearest = nearestFirstVertex(map, first);
		QVERIFY2(nearest < 0.001, qPrintable(QString::number(nearest)));
	}

	/// Accept (6)
	void clipTest()
	{
		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.clip = geodataClipBox(map, QRectF(QPointF(500000, 4999990), QPointF(500100, 5000100)));
		QCOMPARE(options.clip.size(), 4);
		auto const result = importGeodata(map, { gpkg() }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 3);  // roads A (cut) and B, building P1

		auto const box = options.clip.boundingRect().adjusted(-0.001, -0.001, 0.001, 0.001);
		for (auto* object : allObjects(map))
		{
			for (auto const& coord : object->getRawCoordinateVector())
				QVERIFY(box.contains(QPointF(MapCoordF(coord))));
		}

		auto* road_a = findByTag(map, QStringLiteral("name"), QStringLiteral("A"));
		QVERIFY(road_a);
		auto const& coords = road_a->getRawCoordinateVector();
		auto const min_x = std::min_element(begin(coords), end(coords), [](auto const& a, auto const& b) { return a.x() < b.x(); })->x();
		QVERIFY(std::abs(min_x - expected(map, 500000, 5000000).x()) < 0.01);
		QVERIFY(!findByTag(map, QStringLiteral("name"), QStringLiteral("C")));
	}

	/// Accept (7)
	void layerTagTest()
	{
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.layer_tag = GeodataImportOptions::LayerNameTag;
			QVERIFY(importGeodata(map, { gpkg() }, options).ok);
			for (auto* object : allObjects(map))
			{
				auto const expected_layer = object->getSymbol()->getContainedTypes() & Symbol::Area ? QStringLiteral("buildings") : QStringLiteral("roads");
				QCOMPARE(object->getTag(QStringLiteral("layer_name")), expected_layer);
			}
		}
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.layer_tag = GeodataImportOptions::AttributeTag;
			options.layer_tag_attribute = QStringLiteral("kind");
			QVERIFY(importGeodata(map, { gpkg() }, options).ok);
			QCOMPARE(findByTag(map, QStringLiteral("name"), QStringLiteral("A"))->getTag(QStringLiteral("layer_name")), QStringLiteral("track"));
			QCOMPARE(findByTag(map, QStringLiteral("name"), QStringLiteral("B"))->getTag(QStringLiteral("layer_name")), QStringLiteral("path"));
		}
	}

	/// Accept (8)
	void textTest()
	{
		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.crs_mode = GeodataImportOptions::CrsUnreferenced;
		options.units = GeodataImportOptions::MillimetresOnPaper;
		options.blocks_as_points = true;
		options.text_attribute = QStringLiteral("BlockName");
		auto const result = importGeodata(map, { dxf() }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(countType(map, Object::Point), 3);
		QCOMPARE(countType(map, Object::Text), 3);
		auto const objects = allObjects(map);
		for (auto* object : objects)
		{
			if (object->getType() != Object::Text)
				continue;
			auto* text = static_cast<TextObject*>(object);
			QCOMPARE(text->getText(), QStringLiteral("TREE"));
			auto const anchor = MapCoordF(text->getAnchorCoordF());
			auto const at_point = std::any_of(begin(objects), end(objects), [&anchor](const Object* o) {
				return o->getType() == Object::Point && distance(MapCoordF(o->getRawCoordinateVector().front()), anchor) < 0.001;
			});
			QVERIFY(at_point);
		}
	}

	/// Accept (9)
	void blocksTest()
	{
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.crs_mode = GeodataImportOptions::CrsUnreferenced;
			options.units = GeodataImportOptions::MillimetresOnPaper;
			options.blocks_as_points = true;
			QVERIFY(importGeodata(map, { dxf() }, options).ok);
			QCOMPARE(map.getNumObjects(), 3);
			for (auto* object : allObjects(map))
			{
				QCOMPARE(object->getType(), Object::Point);
				QCOMPARE(object->getTag(QStringLiteral("BlockName")), QStringLiteral("TREE"));
			}
		}
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.crs_mode = GeodataImportOptions::CrsUnreferenced;
			options.units = GeodataImportOptions::MillimetresOnPaper;
			QVERIFY(importGeodata(map, { dxf() }, options).ok);
			QCOMPARE(countType(map, Object::Point), 0);
			QCOMPARE(countType(map, Object::Path), 3);
		}
	}

	/// Accept (10)
	void simplifyTest()
	{
		Map map;
		setupMap(map);
		GeodataImportOptions options;
		options.layers = QStringList{ QStringLiteral("roads") };
		options.simplify_mm = 0.5;
		QVERIFY(importGeodata(map, { gpkg() }, options).ok);
		auto* road_c = findByTag(map, QStringLiteral("name"), QStringLiteral("C"));
		QVERIFY(road_c);
		auto const& coords = road_c->getRawCoordinateVector();
		QVERIFY(coords.size() < 200);
		for (auto const& p : roadC())
			QVERIFY(distanceToPolyline(QPointF(expected(map, p.x(), p.y())), coords) <= 0.5 + 0.001);
	}

	/// Accept (11)
	void crtTest()
	{
		auto const crt = dir.filePath(QStringLiteral("roads.crt"));
		{
			QFile file(crt);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("501 layer_name = roads\n");
		}
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.layers = QStringList{ QStringLiteral("roads") };
			options.layer_tag = GeodataImportOptions::LayerNameTag;
			options.crt_file = crt;
			auto const result = importGeodata(map, { gpkg() }, options);
			QVERIFY2(result.ok, qPrintable(result.error));
			QCOMPARE(map.getNumObjects(), 3);
			for (auto* object : allObjects(map))
				QCOMPARE(object->getSymbol(), map.getSymbol(0));
		}
		{
			Map map;
			setupMap(map);
			GeodataImportOptions options;
			options.layers = QStringList{ QStringLiteral("roads") };
			QVERIFY(importGeodata(map, { gpkg() }, options).ok);
			for (auto* object : allObjects(map))
			{
				QVERIFY(object->getSymbol() != map.getSymbol(0));
				QCOMPARE(object->getSymbol()->getType(), Symbol::Line);
			}
		}
	}

	/// Accept (12)
	void severalFilesTest()
	{
		Map map;
		setupMap(map);
		auto const symbols_before = map.getNumSymbols();
		auto const result = importGeodata(map, { gpkg(), plainShp() }, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 7);
		QCOMPARE(result.objects, 7);
		// Default line and area symbols, once each.
		QCOMPARE(map.getNumSymbols(), symbols_before + 2);
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumObjects(), 0);
	}

	/// Accept (13)
	void runnerTest()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));

		Map map;
		setupMap(map);
		auto const map_path = dir.filePath(QStringLiteral("target.omap"));
		auto const* format = FileFormats.findFormat("XML");
		QVERIFY(format);
		QVERIFY(format->makeExporter(map_path, &map, nullptr)->doExport());

		auto run = [&](const QString& name, const QJsonObject& import_step) -> int {
			auto const out = dir.filePath(name + QStringLiteral(".foom"));
			auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
			    QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } },
			    import_step,
			    QJsonObject{ { QStringLiteral("step"), QStringLiteral("save-as") }, { QStringLiteral("path"), out } },
			} } };
			auto const job_path = dir.filePath(name + QStringLiteral(".json"));
			QFile file(job_path);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			file.write(QJsonDocument(job).toJson());
			file.close();

			QProcess process;
			auto env = QProcessEnvironment::systemEnvironment();
			env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
			process.setProcessEnvironment(env);
			process.start(executable, { QStringLiteral("--run"), job_path });
			if (!process.waitForFinished(120000) || process.exitCode() != 0)
			{
				qWarning("%s", process.readAllStandardError().constData());
				return -1;
			}
			Map result;
			if (!result.loadFrom(out))
				return -1;
			return result.getNumObjects();
		};

		QCOMPARE(run(QStringLiteral("layer"), QJsonObject{
		    { QStringLiteral("step"), QStringLiteral("import") },
		    { QStringLiteral("path"), gpkg() },
		    { QStringLiteral("layer"), QJsonArray{ QStringLiteral("buildings") } },
		}), 2);
		QCOMPARE(run(QStringLiteral("clip"), QJsonObject{
		    { QStringLiteral("step"), QStringLiteral("import") },
		    { QStringLiteral("path"), gpkg() },
		    { QStringLiteral("clip"), QJsonArray{ 500000, 4999990, 500100, 5000100 } },
		}), 3);
	}

private:
	QString gpkg() const { return dir.filePath(QStringLiteral("data.gpkg")); }
	QString plainShp() const { return dir.filePath(QStringLiteral("plain.shp")); }
	QString smallShp() const { return dir.filePath(QStringLiteral("small.shp")); }
	QString dxf() const { return dir.filePath(QStringLiteral("blocks.dxf")); }

	void writeGeoPackage()
	{
		auto* driver = GDALGetDriverByName("GPKG");
		QVERIFY(driver);
		auto* dataset = GDALCreate(driver, gpkg().toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
		QVERIFY(dataset);
		auto* srs = OSRNewSpatialReference(nullptr);
		QCOMPARE(OSRImportFromEPSG(srs, 32613), OGRERR_NONE);

		auto* roads = GDALDatasetCreateLayer(dataset, "roads", srs, wkbLineString, nullptr);
		QVERIFY(roads);
		addStringField(roads, "kind");
		addStringField(roads, "name");
		addFeature(roads, wkbLineString, { { 499900, 5000000 }, { 500100, 5000000 } }, { { "kind", "track" }, { "name", "A" } });
		addFeature(roads, wkbLineString, { { 500020, 5000020 }, { 500080, 5000020 } }, { { "kind", "path" }, { "name", "B" } });
		addFeature(roads, wkbLineString, roadC(), { { "kind", "track" }, { "name", "C" } });

		auto* buildings = GDALDatasetCreateLayer(dataset, "buildings", srs, wkbPolygon, nullptr);
		QVERIFY(buildings);
		addFeature(buildings, wkbPolygon, { { 500030, 5000030 }, { 500050, 5000030 }, { 500050, 5000050 }, { 500030, 5000050 } });
		addFeature(buildings, wkbPolygon, { { 499500, 5000500 }, { 499520, 5000500 }, { 499520, 5000520 }, { 499500, 5000520 } });

		OSRDestroySpatialReference(srs);
		GDALClose(dataset);
	}

	void writeShapefile(const QString& path, const std::vector<Points>& lines)
	{
		auto* driver = GDALGetDriverByName("ESRI Shapefile");
		QVERIFY(driver);
		auto* dataset = GDALCreate(driver, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
		QVERIFY(dataset);
		auto* layer = GDALDatasetCreateLayer(dataset, QFileInfo(path).baseName().toUtf8().constData(), nullptr, wkbLineString, nullptr);
		QVERIFY(layer);
		for (auto const& line : lines)
			addFeature(layer, wkbLineString, line);
		GDALClose(dataset);
		QVERIFY(!QFileInfo::exists(QFileInfo(path).path() + QLatin1Char('/') + QFileInfo(path).baseName() + QStringLiteral(".prj")));
	}

	void writeDxf()
	{
		auto* driver = GDALGetDriverByName("DXF");
		QVERIFY(driver);
		auto* dataset = GDALCreate(driver, dxf().toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
		QVERIFY(dataset);
		auto* blocks = GDALDatasetCreateLayer(dataset, "blocks", nullptr, wkbUnknown, nullptr);
		QVERIFY(blocks);
		// The block's name: "Block" when GDAL reads it, "BlockName" in older writers.
		addFeature(blocks, wkbLineString, { { -1, 0 }, { 1, 0 } }, { { "Block", "TREE" }, { "BlockName", "TREE" } });
		auto* entities = GDALDatasetCreateLayer(dataset, "entities", nullptr, wkbUnknown, nullptr);
		QVERIFY(entities);
		for (auto x : { 10.0, 20.0, 30.0 })
			addFeature(entities, wkbPoint, { { x, 10 } }, { { "BlockName", "TREE" } });
		GDALClose(dataset);
	}

	QTemporaryDir dir;
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(GeodataImportTest)
#include "geodata_import_t.moc"  // IWYU pragma: keep
