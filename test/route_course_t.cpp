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
#include <cstddef>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
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
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
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
	if (word == QLatin1String("g"))
		item.type = CourseItem::MapExchange;
	else if (word == QLatin1String("v"))
		item.type = CourseItem::ForkBranch;
	else
	{
		item.type = first == QLatin1Char('S') ? CourseItem::Start
		            : first == QLatin1Char('M') ? CourseItem::MarkedRoute
		            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
		item.code = word;
	}
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

/// A start, two controls with a building between them, a marked route and a finish, on a line.
struct RouteMap
{
	Map map;
	
	RouteMap()
	{
		map.setScaleDenominator(10000);
		auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
		auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
		auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
		auto* marked = addSymbol<LineSymbol>(map, 707, Symbol::CourseMarkedRoute);
		auto* building = addSymbol<AreaSymbol>(map, 521);
		drawPoint(map, start, 2.0, 5.0);
		drawPoint(map, control, 8.0, 5.0);
		drawPoint(map, control, 14.0, 5.0);
		drawPoint(map, finish, 18.0, 5.0);
		auto* route = new PathObject(marked);
		route->addCoordinate(MapCoord(15.0, 5.0));
		route->addCoordinate(MapCoord(17.0, 5.0));
		push(map, route);
		auto* wall = new PathObject(building);
		wall->addCoordinate(MapCoord(10.5, 3.0));
		wall->addCoordinate(MapCoord(11.5, 3.0));
		wall->addCoordinate(MapCoord(11.5, 7.0));
		wall->addCoordinate(MapCoord(10.5, 7.0));
		wall->parts().back().setClosed(true);
		map.addObject(wall);
		
		auto data = map.courseData();
		data.courses.push_back(courseOf("A", "S1 31 32 F1"));
		data.courses.push_back(courseOf("X", "S1 31 g 32 F1"));
		data.courses.push_back(courseOf("M", "S1 31 32 M1 F1"));
		data.courses.push_back(courseOf("V", "S1 v 31 32 F1"));
		changeCourseData(map, data);
	}
	
	QRectF area() const { return QRectF(0.0, 0.0, 20.0, 10.0); }
};

QString namesOf(const CourseRoutes& routes)
{
	QStringList names;
	for (auto const& leg : routes.legs)
		names.push_back(leg.marked ? leg.from : QString(leg.from + QLatin1Char('-') + leg.to));
	return names.join(QLatin1Char(' '));
}

}  // namespace



/**
 * @test Tests the routes of the legs of a course and their table.
 *
 * See the design notes, "Route analysis: the routes of a course".
 */
class RouteCourseTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void legsTest();
	void textTest();
	void partsTest();
	void markedRouteTest();
};


void RouteCourseTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteCourseTest::legsTest()
{
	RouteMap m;
	auto const& data = m.map.courseData();
	QVERIFY(data.findObject(QStringLiteral("S1")) && data.findObject(QStringLiteral("32")) && data.findObject(QStringLiteral("M1")));
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	auto const routes = analyseCourseRoutes(m.map, data, QStringLiteral("A"), -1, search);
	QVERIFY(routes.analysed);
	QCOMPARE(routes.course, QStringLiteral("A"));
	QCOMPARE(namesOf(routes), QStringLiteral("S1-31 31-32 32-F1"));
	QCOMPARE(routes.missing, 0);
	QVERIFY(std::abs(routes.legs[0].straight_m - 60.0) < 0.01);
	QVERIFY(routes.legs[0].route.found);
	QVERIFY2(std::abs(routes.legs[0].route.length_m - 60.0) < 1.5, qPrintable(QString::number(routes.legs[0].route.length_m)));
	// Around the building: 32 m to its corner, 10 m along it, 32 m on.
	QVERIFY(std::abs(routes.legs[1].straight_m - 60.0) < 0.01);
	QVERIFY2(routes.legs[1].route.length_m > 72.0 && routes.legs[1].route.length_m < 79.0, qPrintable(QString::number(routes.legs[1].route.length_m)));
	QVERIFY(std::abs(routes.course_length_m - 160.0) < 0.01);
	QVERIFY(routes.length_m > 172.0 && routes.length_m < 180.0);
	QVERIFY(std::abs(routes.time_s - routes.length_m * 0.2 / 0.83) < 1.5);
	QCOMPARE(routes.climb_m, 0.0);
	
	// A course with variations, and one which is not there.
	QVERIFY(!analyseCourseRoutes(m.map, data, QStringLiteral("V"), -1, search).analysed);
	QVERIFY(!analyseCourseRoutes(m.map, data, QStringLiteral("Nobody"), -1, search).analysed);
}


/// Accept (2)
void RouteCourseTest::textTest()
{
	QCOMPARE(formatRouteTime(58.4), QStringLiteral("00:58"));
	QCOMPARE(formatRouteTime(301.0), QStringLiteral("05:01"));
	QCOMPARE(formatRouteTime(0.0), QStringLiteral("00:00"));
	QCOMPARE(formatRouteTime(3725.0), QStringLiteral("62:05"));
	
	RouteMap m;
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	auto const routes = analyseCourseRoutes(m.map, m.map.courseData(), QStringLiteral("A"), -1, search);
	auto const lines = courseRoutesText(routes).split(QLatin1Char('\n'));
	QCOMPARE(lines.size(), 9);
	QCOMPARE(lines[0], QStringLiteral("Course A\tFastest route (leg)\tFastest route (total)\tTime along route"));
	QCOMPARE(lines[1], QStringLiteral("Leg S1-31\t60 m \t60 m \t00:14"));
	QVERIFY2(lines[2].startsWith(QLatin1String("Leg 31-32\t7")), qPrintable(lines[2]));
	QVERIFY2(lines[3].startsWith(QLatin1String("Leg 32-F1\t39 m \t17")), qPrintable(lines[3]));
	QVERIFY(lines[4].isEmpty());
	QCOMPARE(lines[5], QStringLiteral("Length = 160 m"));
	QVERIFY2(lines[6].startsWith(QLatin1String("Extra length = 1")), qPrintable(lines[6]));
	QCOMPARE(lines[7], QStringLiteral("Time along course = %1").arg(formatRouteTime(routes.time_s)));
	QVERIFY(lines[8].isEmpty());
	
	// A leg without a route says so, and counts with its straight.
	auto broken = routes;
	broken.legs[1].route = RouteResult{};
	QVERIFY(courseRoutesText(broken).contains(QLatin1String("Leg 31-32\tno route\n")));
}


/// Accept (3)
void RouteCourseTest::partsTest()
{
	RouteMap m;
	auto const& data = m.map.courseData();
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	// The whole course has the leg across the exchange, its parts have not.
	QCOMPARE(namesOf(analyseCourseRoutes(m.map, data, QStringLiteral("X"), -1, search)), QStringLiteral("S1-31 31-32 32-F1"));
	auto const first = analyseCourseRoutes(m.map, data, QStringLiteral("X"), 0, search);
	QCOMPARE(namesOf(first), QStringLiteral("S1-31"));
	QCOMPARE(first.part, 0);
	QCOMPARE(namesOf(analyseCourseRoutes(m.map, data, QStringLiteral("X"), 1, search)), QStringLiteral("32-F1"));
	QVERIFY(!analyseCourseRoutes(m.map, data, QStringLiteral("X"), 2, search).analysed);
}


/// Accept (4)
void RouteCourseTest::markedRouteTest()
{
	RouteMap m;
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	auto const routes = analyseCourseRoutes(m.map, m.map.courseData(), QStringLiteral("M"), -1, search);
	QCOMPARE(namesOf(routes), QStringLiteral("S1-31 31-32 32-M1 M1 M1-F1"));
	auto const& marked = routes.legs[3];
	QVERIFY(marked.marked);
	QVERIFY(marked.route.found);
	QVERIFY(std::abs(marked.route.length_m - 20.0) < 0.01);
	QVERIFY(std::abs(marked.route.time_s - 20.0 * 0.2 / 0.83) < 0.2);
	QVERIFY(std::abs(routes.legs[2].straight_m - 10.0) < 0.01);
	QVERIFY(std::abs(routes.legs[4].straight_m - 10.0) < 0.01);
	QVERIFY(std::abs(routes.course_length_m - 160.0) < 0.01);
	QVERIFY(courseRoutesText(routes).contains(QLatin1String("\nM1: \t20 m \t")));
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteCourseTest)
#include "route_course_t.moc"  // IWYU pragma: keep
