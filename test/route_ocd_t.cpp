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
#include <cstddef>
#include <cstdio>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/ocd_course_records.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "templates/template.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

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

void push(Map& map, Object* object)
{
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	push(map, object);
}

CourseItem itemOf(const QString& word)
{
	CourseItem item;
	auto const first = word.at(0);
	item.type = first == QLatin1Char('S') ? CourseItem::Start
	            : first == QLatin1Char('M') ? CourseItem::MarkedRoute
	            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
	item.code = word;
	return item;
}

Course courseOf(const char* name, const char* items)
{
	Course course;
	course.name = QString::fromLatin1(name);
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
		course.items.push_back(itemOf(word));
	return course;
}

/**
 * Open land with a marked route M1 to the start S1, the controls 31 and 32
 * with a building between them, the finish F1, and a control 33 inside a
 * building of its own, to which no route leads; the courses A, L and N.
 */
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* purple = new MapColor(QStringLiteral("Purple"), 0);
	purple->setCmyk(MapColorCmyk(0.35f, 0.85f, 0.0f, 0.0f));
	map.addColor(purple, 0);
	auto* black = new MapColor(QStringLiteral("Black"), 1);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 1);
	
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	auto* marked = addSymbol<LineSymbol>(map, 707, Symbol::CourseMarkedRoute);
	marked->setColor(purple);
	marked->setLineWidth(0.35);
	auto* building = addSymbol<AreaSymbol>(map, 521);
	building->setColor(black);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	auto const drawBox = [&map](const Symbol* symbol, double x1, double y1, double x2, double y2) {
		auto* box = new PathObject(symbol);
		box->addCoordinate(MapCoord(x1, y1));
		box->addCoordinate(MapCoord(x2, y1));
		box->addCoordinate(MapCoord(x2, y2));
		box->addCoordinate(MapCoord(x1, y2));
		box->parts().back().setClosed(true);
		map.addObject(box);
	};
	drawBox(open, 0.0, 0.0, 24.0, 10.0);
	drawBox(building, 10.5, 3.0, 11.5, 7.0);
	drawBox(building, 20.0, 3.0, 23.0, 7.0);
	drawPoint(map, start, 4.0, 5.0);
	drawPoint(map, control, 8.0, 5.0);
	drawPoint(map, control, 14.0, 5.0);
	drawPoint(map, finish, 18.0, 5.0);
	drawPoint(map, control, 21.5, 5.0);
	auto* route = new PathObject(marked);
	route->addCoordinate(MapCoord(1.0, 5.0));
	route->addCoordinate(MapCoord(3.0, 5.0));
	push(map, route);
	
	auto data = map.courseData();
	data.courses.push_back(courseOf("A", "S1 31 32 F1"));
	data.courses.push_back(courseOf("L", "M1 S1 31 F1"));
	data.courses.push_back(courseOf("N", "S1 32 33"));
	changeCourseData(map, data);
}

QByteArray exportTo(const Map& map, const char* format_id, QStringList* warnings = nullptr)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return {};
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	if (warnings)
	{
		for (auto const& warning : exporter->warnings())
			warnings->push_back(warning);
	}
	return buffer.data();
}

bool importFrom(const QByteArray& data, const char* format_id, Map& map)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

int routeSymbols(const Map& map)
{
	auto count = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		count += symbol->getNumberComponent(0) == 10602 && symbol->getNumberComponent(1) == 10 ? 1 : 0;
	}
	return count;
}

int routeObjects(const Map& map)
{
	auto count = 0;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
			count += part->getObject(i)->getTag(QStringLiteral("ocd:object_string_type")) == QLatin1String("5") ? 1 : 0;
	}
	return count;
}

/// The routes of all courses of the map, kept in it.
void analyse(Map& map)
{
	StoredRoutes store;
	analyseAllRoutesOfMap(map, store);
	auto data = map.courseData();
	data.routes = store;
	changeCourseData(map, data);
}

/// The found legs of a store are in the other, with their times, lengths and points.
void compareLegs(const StoredRoutes& expected, const StoredRoutes& actual)
{
	auto legs = 0;
	for (auto const& route : expected.routes)
	{
		if (!route.found || route.isMarkedRoute())
		{
			QVERIFY2(!actual.find(route.from, route.to), qPrintable(route.from));
			continue;
		}
		++legs;
		auto const* other = actual.find(route.from, route.to);
		QVERIFY2(other, qPrintable(route.from + QLatin1Char('-') + route.to));
		QVERIFY(other->found);
		QVERIFY2(std::abs(other->time_s - route.time_s) <= 0.5, qPrintable(QString::number(other->time_s)));
		QVERIFY2(std::abs(other->length_m - route.length_m) <= 0.5, qPrintable(QString::number(other->length_m)));
		QCOMPARE(other->points.size(), route.points.size());
		for (std::size_t i = 0; i < route.points.size(); ++i)
		{
			QVERIFY(std::abs(other->points[i].x() - route.points[i].x()) <= 0.02);
			QVERIFY(std::abs(other->points[i].y() - route.points[i].y()) <= 0.02);
		}
	}
	QCOMPARE(int(actual.routes.size()), legs);
}

}  // namespace



/**
 * @test Tests the routes of the route analysis in OCD files.
 *
 * See the design notes, "Route analysis: the routes in OCD files".
 */
class RouteOcdTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void recordTest();
	void roundTripTest();
	void withoutRoutesTest();
	void olderVersionTest();
};


void RouteOcdTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteOcdTest::recordTest()
{
	StoredRoute route;
	route.from = QStringLiteral("S1");
	route.to = QStringLiteral("31");
	route.length_m = 307.9;
	route.time_s = 134.2;
	route.climb_m = 51.3;
	QCOMPARE(OcdCourse::formatRoute(1, route, true), QStringLiteral("1\ts1\tnLeg S1-31\tfS1\tt31\ti0\tz134\tc308 m | 51 m | 02:14"));
	QCOMPARE(OcdCourse::formatRoute(17, route, false), QStringLiteral("17\ts1\tnLeg S1-31\tfS1\tt31\ti0\tz134\tc308 m | 02:14"));
	
	StoredRoute read;
	auto has_climb = false;
	QVERIFY(OcdCourse::parseRoute(QStringLiteral("1\ts1\tnLeg S1-31\tfS1\tt31\ti0\tz134\tc308 m | 51 m | 02:14"), read, &has_climb));
	QCOMPARE(read.from, QStringLiteral("S1"));
	QCOMPARE(read.to, QStringLiteral("31"));
	QCOMPARE(read.time_s, 134.0);
	QCOMPARE(read.climb_m, 51.0);
	QCOMPARE(read.length_m, 308.0);
	QVERIFY(has_climb);
	QVERIFY(OcdCourse::parseRoute(QStringLiteral("4\ts1\tnLeg M5-S1\tfM5\ttS1\ti0\tz7\tc30 m | 00:07"), read, &has_climb));
	QCOMPARE(read.from, QStringLiteral("M5"));
	QCOMPARE(read.to, QStringLiteral("S1"));
	QCOMPARE(read.time_s, 7.0);
	QCOMPARE(read.climb_m, 0.0);
	QVERIFY(!has_climb);
	
	QVERIFY(!OcdCourse::parseRoute(QStringLiteral("1\ts1\tnLeg S1-31\tfS1\tt31\ti1\tz134\tc308 m | 02:14"), read));
	QVERIFY(!OcdCourse::parseRoute(QStringLiteral("1\ts1\tnLeg S1-31\tfS1\ti0\tz134"), read));
	QVERIFY(!OcdCourse::parseRoute(QStringLiteral("1\ts1\tnLeg S1-31\tt31\ti0\tz134"), read));
	QVERIFY(!OcdCourse::parseRoute(QStringLiteral("Easy Course\td31"), read));
	QVERIFY(!OcdCourse::parseRoute(QString{}, read));
}


/// Accept (2)
void RouteOcdTest::roundTripTest()
{
	Map map;
	fillMap(map);
	analyse(map);
	auto const& kept = map.courseData().routes;
	// S1-31, 31-32, 32-F1, M1, M1-S1, 31-F1, S1-32 and 32-33, which has no route.
	QCOMPARE(kept.routes.size(), std::size_t(8));
	QVERIFY(kept.find(QStringLiteral("M1"), QStringLiteral("M1")));
	QVERIFY(kept.find(QStringLiteral("32"), QStringLiteral("33")));
	QVERIFY(!kept.find(QStringLiteral("32"), QStringLiteral("33"))->found);
	QCOMPARE(routeSymbols(map), 0);
	auto const objects = map.getNumObjects();
	
	QStringList warnings;
	auto const written = exportTo(map, "OCD2018", &warnings);
	QVERIFY(!written.isEmpty());
	QVERIFY2(warnings.filter(QStringLiteral("route")).isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
	// Writing leaves the map as it was.
	QCOMPARE(map.getNumObjects(), objects);
	QCOMPARE(routeSymbols(map), 0);
	
	Map reloaded;
	QVERIFY(importFrom(written, "OCD", reloaded));
	compareLegs(kept, reloaded.courseData().routes);
	QVERIFY(!reloaded.courseData().routes.with_heights);
	QCOMPARE(reloaded.getNumObjects(), objects);
	QCOMPARE(routeObjects(reloaded), 0);
	QCOMPARE(routeSymbols(reloaded), 1);
	// The routes are those of their legs.
	auto const table = storedCourseRoutes(reloaded, reloaded.courseData(), QStringLiteral("A"), -1);
	QCOMPARE(table.unknown, 0);
	QCOMPARE(table.legs.size(), std::size_t(3));
	
	// Again, now with the symbol which the map has.
	auto const again = exportTo(reloaded, "OCD2018");
	Map third;
	QVERIFY(importFrom(again, "OCD", third));
	compareLegs(kept, third.courseData().routes);
	QCOMPARE(third.getNumObjects(), objects);
	QCOMPARE(routeObjects(third), 0);
	QCOMPARE(routeSymbols(third), 1);
}


/// Accept (3)
void RouteOcdTest::withoutRoutesTest()
{
	Map map;
	fillMap(map);
	auto const objects = map.getNumObjects();
	Map reloaded;
	QVERIFY(importFrom(exportTo(map, "OCD2018"), "OCD", reloaded));
	QCOMPARE(routeSymbols(reloaded), 0);
	QCOMPARE(reloaded.getNumObjects(), objects);
	QVERIFY(reloaded.courseData().routes.isEmpty());
}


/// Accept (4)
void RouteOcdTest::olderVersionTest()
{
	Map map;
	fillMap(map);
	analyse(map);
	auto const objects = map.getNumObjects();
	Map reloaded;
	QVERIFY(importFrom(exportTo(map, "OCD11"), "OCD", reloaded));
	QCOMPARE(routeSymbols(reloaded), 0);
	QCOMPARE(reloaded.getNumObjects(), objects);
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteOcdTest)
#include "route_ocd_t.moc"  // IWYU pragma: keep
