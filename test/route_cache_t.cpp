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


#include <cstddef>
#include <cstdio>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_search.h"
#include "course/route_settings.h"
#include "course/route_terrain.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "undo/object_undo.h"

using namespace OpenOrienteering;


namespace {

template <class S>
S* addSymbol(Map& map, int number, Symbol::CourseRole role = Symbol::NoCourseRole)
{
	auto* symbol = new S();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

PathObject* addArea(Map& map, const Symbol* symbol, const QRectF& r)
{
	auto* object = new PathObject(symbol);
	object->addCoordinate(MapCoord(r.left(), r.top()));
	object->addCoordinate(MapCoord(r.right(), r.top()));
	object->addCoordinate(MapCoord(r.right(), r.bottom()));
	object->addCoordinate(MapCoord(r.left(), r.bottom()));
	object->parts().back().setClosed(true);
	map.addObject(object);
	return object;
}

void addPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

struct Field
{
	PathObject* building = nullptr;
	PathObject* contour = nullptr;
};

/**
 * Open land of 20 by 10 mm at 1:10,000 with a building from (8, 3) to
 * (12, 7) and a contour across it; with a course from (2, 5) to (18, 5).
 */
Field fillMap(Map& map, bool with_course = false)
{
	Field field;
	map.setScaleDenominator(10000);
	addArea(map, addSymbol<AreaSymbol>(map, 401), QRectF(0.0, 0.0, 20.0, 10.0));
	field.building = addArea(map, addSymbol<AreaSymbol>(map, 521), QRectF(8.0, 3.0, 4.0, 4.0));
	field.contour = new PathObject(addSymbol<LineSymbol>(map, 101));
	field.contour->addCoordinate(MapCoord(1.0, 8.0));
	field.contour->addCoordinate(MapCoord(19.0, 9.0));
	map.addObject(field.contour);
	if (with_course)
	{
		addPoint(map, addSymbol<PointSymbol>(map, 701, Symbol::CourseStart), 2.0, 5.0);
		addPoint(map, addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish), 18.0, 5.0);
		auto data = map.courseData();
		Course course;
		course.name = QStringLiteral("A");
		CourseItem start;
		start.type = CourseItem::Start;
		start.code = QStringLiteral("S1");
		CourseItem finish;
		finish.type = CourseItem::Finish;
		finish.code = QStringLiteral("F1");
		course.items = { start, finish };
		data.courses.push_back(course);
		changeCourseData(map, data);
	}
	return field;
}

bool sameTerrain(const RouteTerrain& a, const RouteTerrain& b)
{
	return a.width == b.width && a.height == b.height && a.area == b.area && qFuzzyCompare(a.pixel, b.pixel)
	       && a.runnability == b.runnability && a.impassable == b.impassable && a.perimeter == b.perimeter;
}

QByteArray runJob(const QString& dir, const QString& map_path, QString* error)
{
	auto const job_path = QDir(dir).absoluteFilePath(QStringLiteral("job.json"));
	QJsonArray steps;
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("analyse-routes") }, { QStringLiteral("cache"), true } });
	QFile job_file(job_path);
	if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return {};
	job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	job_file.close();
	QProcess process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process.setProcessEnvironment(env);
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	process.waitForFinished(120000);
	QByteArray output = process.readAllStandardOutput();
	output += process.readAllStandardError();
	if (process.exitCode() != 0)
		*error = QString::fromUtf8(output);
	return output;
}

}  // namespace



/**
 * @test Tests the cache of the terrain rasters of the route analysis.
 *
 * See the design notes, "Route analysis: the cache of the rasters".
 */
class RouteCacheTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void keyTest();
	void fileTest();
	void memoryTest();
	void resetTest();
	void routesTest();
	void runnerTest();
};


void RouteCacheTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteCacheTest::keyTest()
{
	Map map;
	auto field = fillMap(map);
	RouteSettings settings;
	auto const key = routeTerrainKey(map, settings);
	QVERIFY(!key.isEmpty());
	QCOMPARE(routeTerrainKey(map, settings), key);

	// Another base pace, or no elevation, leave the rasters as they are.
	auto other = settings;
	other.base_pace = 400;
	other.use_elevation = false;
	other.climb_factor = 0.5;
	QCOMPARE(routeTerrainKey(map, other), key);

	// What the rasters are made of.
	other = settings;
	other.use_runnability = false;
	QVERIFY(routeTerrainKey(map, other) != key);
	other = settings;
	Runnability slow;
	slow.kind = Runnability::Vegetation;
	slow.percent = 50;
	other.symbols.push_back({ QStringLiteral("521.000"), slow });
	QVERIFY(routeTerrainKey(map, other) != key);
	other = settings;
	other.perimeter = { MapCoordF(1.0, 1.0), MapCoordF(15.0, 1.0), MapCoordF(15.0, 9.0) };
	QVERIFY(routeTerrainKey(map, other) != key);
	QVERIFY(routeTerrainKey(map, settings, QRectF(0.0, 0.0, 10.0, 10.0)) != key);

	// The contour does not count, the building does.
	field.contour->move(MapCoord(0.0, -1.0));
	QCOMPARE(routeTerrainKey(map, settings), key);
	field.building->move(MapCoord(1.0, 0.0));
	QVERIFY(routeTerrainKey(map, settings) != key);
}


/// Accept (2)
void RouteCacheTest::fileTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom.~route"));
	Map map;
	auto field = fillMap(map);
	RouteSettings settings;
	resetRouteTerrainCache({});

	RouteTerrainSource source;
	auto const made = cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
	QVERIFY(QFileInfo::exists(path));
	QVERIFY(sameTerrain(made, buildRouteTerrain(map, settings)));

	// From the file, once the memory is forgotten.
	forgetRouteTerrainMemory();
	auto const read = cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::File);
	QVERIFY(sameTerrain(read, made));

	// After a change they are made anew, and written anew.
	field.building->move(MapCoord(1.0, 0.0));
	forgetRouteTerrainMemory();
	auto const moved = cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
	QVERIFY(!sameTerrain(moved, made));
	forgetRouteTerrainMemory();
	cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::File);

	// Garbage is made anew.
	{
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write("not a cache of rasters");
	}
	forgetRouteTerrainMemory();
	auto const again = cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
	QVERIFY(sameTerrain(again, moved));
}


/// Accept (3)
void RouteCacheTest::memoryTest()
{
	Map map;
	fillMap(map);
	RouteSettings settings;
	resetRouteTerrainCache({});
	RouteTerrainSource source;
	auto const first = cachedRouteTerrain(map, settings, {}, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
	auto const second = cachedRouteTerrain(map, settings, {}, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Memory);
	QVERIFY(sameTerrain(first, second));
	// Other settings of the runnability: made anew.
	settings.use_runnability = false;
	cachedRouteTerrain(map, settings, {}, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
}


/// Accept (4)
void RouteCacheTest::resetTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom.~route"));
	Map map;
	fillMap(map);
	RouteTerrainSource source;
	cachedRouteTerrain(map, RouteSettings{}, path, {}, &source);
	QVERIFY(QFileInfo::exists(path));
	resetRouteTerrainCache(path);
	QVERIFY(!QFileInfo::exists(path));
	cachedRouteTerrain(map, RouteSettings{}, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::Made);
}


/// Accept (5)
void RouteCacheTest::routesTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom.~route"));
	Map map;
	fillMap(map);
	RouteSettings settings;
	resetRouteTerrainCache({});
	cachedRouteTerrain(map, settings, path);
	forgetRouteTerrainMemory();
	RouteTerrainSource source;
	auto const from_file = cachedRouteTerrain(map, settings, path, {}, &source);
	QCOMPARE(source, RouteTerrainSource::File);
	auto const made = buildRouteTerrain(map, settings);
	RouteSearch file_search(from_file, settings, 10000);
	RouteSearch made_search(made, settings, 10000);
	auto const a = file_search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0));
	auto const b = made_search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0));
	QVERIFY(a.found && b.found);
	QCOMPARE(a.time_s, b.time_s);
	QCOMPARE(a.length_m, b.length_m);
	QCOMPARE(a.points.size(), b.points.size());
}


/// Accept (6)
void RouteCacheTest::runnerTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	{
		Map map;
		fillMap(map, true);
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	QString error;
	auto output = runJob(dir.path(), map_path, &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QVERIFY2(output.contains("\"terrain\":\"made\""), output.constData());
	QVERIFY(QFileInfo::exists(map_path + QLatin1String(".~route")));
	output = runJob(dir.path(), map_path, &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QVERIFY2(output.contains("\"terrain\":\"file\""), output.constData());
}



QTEST_MAIN(RouteCacheTest)
#include "route_cache_t.moc"  // IWYU pragma: keep
