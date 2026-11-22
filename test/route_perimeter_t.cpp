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

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QLatin1Char>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_transform.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_settings.h"
#include "course/route_terrain.h"
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

void addArea(Map& map, const Symbol* symbol, const QRectF& r)
{
	auto* object = new PathObject(symbol);
	object->addCoordinate(MapCoord(r.left(), r.top()));
	object->addCoordinate(MapCoord(r.right(), r.top()));
	object->addCoordinate(MapCoord(r.right(), r.bottom()));
	object->addCoordinate(MapCoord(r.left(), r.bottom()));
	object->parts().back().setClosed(true);
	map.addObject(object);
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

/// A map of 20 by 10 mm at 1:10,000, framed by a contour; with open land when asked.
void fillMap(Map& map, bool open_land)
{
	map.setScaleDenominator(10000);
	auto* contour = addSymbol<LineSymbol>(map, 101);
	auto* frame = new PathObject(contour);
	frame->addCoordinate(MapCoord(0.0, 0.0));
	frame->addCoordinate(MapCoord(20.0, 0.0));
	frame->addCoordinate(MapCoord(20.0, 10.0));
	frame->addCoordinate(MapCoord(0.0, 10.0));
	frame->addCoordinate(MapCoord(0.0, 0.0));
	map.addObject(frame);
	if (open_land)
		addArea(map, addSymbol<AreaSymbol>(map, 401), QRectF(0.0, 0.0, 20.0, 10.0));
}

std::vector<MapCoordF> pointsOf(std::initializer_list<std::pair<double, double>> list)
{
	std::vector<MapCoordF> points;
	for (auto const& pair : list)
		points.emplace_back(pair.first, pair.second);
	return points;
}

}  // namespace



/**
 * @test Tests the perimeter of the route analysis.
 *
 * See the design notes, "Route analysis: the perimeter".
 */
class RoutePerimeterTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void settingsTest();
	void terrainTest();
	void searchTest();
	void courseTest();
	void transformTest();
};


void RoutePerimeterTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RoutePerimeterTest::settingsTest()
{
	RouteSettings settings;
	settings.perimeter = pointsOf({ { 1.0, 1.0 }, { 15.5, 1.0 }, { 15.5, 9.25 }, { 1.0, 9.0 } });
	QVERIFY(!settings.isDefault());
	QVERIFY(settings != RouteSettings{});
	QByteArray bytes;
	{
		QXmlStreamWriter xml(&bytes);
		xml.writeStartDocument();
		saveRouteSettings(settings, xml);
		xml.writeEndDocument();
	}
	QVERIFY2(bytes.contains("<perimeter>"), bytes.constData());
	QXmlStreamReader xml(bytes);
	QVERIFY(xml.readNextStartElement());
	auto const loaded = loadRouteSettings(xml);
	QCOMPARE(loaded.perimeter.size(), std::size_t(4));
	QCOMPARE(loaded.perimeter[2].x(), 15.5);
	QCOMPARE(loaded.perimeter[2].y(), 9.25);
	QVERIFY(loaded == settings);
}


/// Accept (2)
void RoutePerimeterTest::terrainTest()
{
	Map map;
	fillMap(map, false);
	RouteSettings settings;
	settings.perimeter = pointsOf({ { 5.0, 2.0 }, { 15.0, 2.0 }, { 5.0, 8.0 } });
	auto const terrain = buildRouteTerrain(map, settings);
	QCOMPARE(terrain.width, 79);
	QCOMPARE(terrain.height, 48);
	QVERIFY(std::abs(terrain.area.left() - 5.0) < 1e-9);
	QVERIFY(std::abs(terrain.area.top() - 2.0) < 1e-9);
	QVERIFY(terrain.isWithinPerimeter(MapCoordF(6.0, 3.0)));
	QVERIFY(!terrain.isImpassableAt(MapCoordF(6.0, 3.0)));
	QCOMPARE(terrain.runnabilityAt(MapCoordF(6.0, 3.0)), forest_runnability);
	QVERIFY(!terrain.isWithinPerimeter(MapCoordF(14.0, 7.0)));
	QVERIFY(terrain.isImpassableAt(MapCoordF(14.0, 7.0)));
	QCOMPARE(terrain.runnabilityAt(MapCoordF(14.0, 7.0)), 0);
	QVERIFY(!terrain.isWithinPerimeter(MapCoordF(2.0, 5.0)));

	auto const whole = buildRouteTerrain(map, RouteSettings{});
	QVERIFY(whole.isWithinPerimeter(MapCoordF(14.0, 7.0)));
	QVERIFY(whole.isWithinPerimeter(MapCoordF(100.0, 100.0)));
}


/// Accept (3)
void RoutePerimeterTest::searchTest()
{
	Map map;
	fillMap(map, true);
	RouteSettings settings;
	settings.perimeter = pointsOf({ { 1.0, 1.0 }, { 19.0, 1.0 }, { 19.0, 9.0 }, { 11.0, 9.0 }, { 11.0, 4.0 }, { 9.0, 4.0 }, { 9.0, 9.0 }, { 1.0, 9.0 } });
	auto const terrain = buildRouteTerrain(map, settings);
	RouteSearch search(terrain, settings, 10000);
	auto const route = search.fastest(MapCoordF(2.0, 7.0), MapCoordF(18.0, 7.0));
	QVERIFY(route.found);
	QVERIFY2(route.length_m >= 165.0, qPrintable(QString::number(route.length_m)));
	for (std::size_t i = 1; i < route.points.size(); ++i)
	{
		for (int p = 0; p <= 50; ++p)
		{
			auto const t = p / 50.0;
			auto const x = route.points[i - 1].x() + t * (route.points[i].x() - route.points[i - 1].x());
			auto const y = route.points[i - 1].y() + t * (route.points[i].y() - route.points[i - 1].y());
			if (x > 9.0 && x < 11.0)
				QVERIFY2(y < 4.0, qPrintable(QString::number(x) + QLatin1Char(' ') + QString::number(y)));
		}
	}
}


/// Accept (4)
void RoutePerimeterTest::courseTest()
{
	Map map;
	fillMap(map, true);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	addPoint(map, start, 2.0, 5.0);
	addPoint(map, control, 10.0, 5.0);
	addPoint(map, finish, 18.0, 5.0);
	auto data = map.courseData();
	Course course;
	course.name = QStringLiteral("A");
	for (auto const* code : { "S1", "31", "F1" })
	{
		CourseItem item;
		auto const text = QString::fromLatin1(code);
		item.type = text.startsWith(QLatin1Char('S')) ? CourseItem::Start
		            : text.startsWith(QLatin1Char('F')) ? CourseItem::Finish : CourseItem::Control;
		item.code = text;
		course.items.push_back(item);
	}
	data.courses.push_back(course);
	data.route.perimeter = pointsOf({ { 1.0, 1.0 }, { 15.0, 1.0 }, { 15.0, 9.0 }, { 1.0, 9.0 } });
	changeCourseData(map, data);

	auto const terrain = buildRouteTerrain(map, map.courseData().route);
	RouteSearch search(terrain, map.courseData().route, 10000);
	StoredRoutes store;
	auto const routes = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QCOMPARE(int(routes.legs.size()), 2);
	QVERIFY(routes.legs[0].route.found);
	QVERIFY(!routes.legs[0].outside);
	QVERIFY(!routes.legs[1].route.found);
	QVERIFY(routes.legs[1].outside);
	QCOMPARE(routes.outside, 1);
	QCOMPARE(routes.missing, 0);
	auto const* kept = store.find(QStringLiteral("31"), QStringLiteral("F1"));
	QVERIFY(kept && !kept->found);
	// The message says so itself (seen on 2026-11-22: only under its details).
	auto const message = courseRoutesMessage(routes, QStringLiteral("A"));
	QVERIFY(message.startsWith(QLatin1String("The fastest routes of the course A take ")));
	QVERIFY2(message.contains(QLatin1String("\n1 leg(s) lie out of the perimeter.")), qPrintable(message));
	QVERIFY(!message.contains(QLatin1String("have no route")));

	// The whole map: no leg is out.
	auto plain = map.courseData();
	plain.route.perimeter.clear();
	auto const whole = buildRouteTerrain(map, plain.route);
	RouteSearch whole_search(whole, plain.route, 10000);
	StoredRoutes whole_store;
	auto const all = analyseCourseRoutes(map, plain, QStringLiteral("A"), -1, whole_search, whole_store);
	QCOMPARE(all.outside, 0);
	QVERIFY(all.legs[1].route.found);
	QVERIFY(!courseRoutesMessage(all, QStringLiteral("A")).contains(QLatin1String("perimeter")));
}


/// Accept (5)
void RoutePerimeterTest::transformTest()
{
	Map map;
	fillMap(map, true);
	auto data = map.courseData();
	data.route.perimeter = pointsOf({ { 1.0, 1.0 }, { 15.0, 1.0 }, { 15.0, 9.0 } });
	map.setCourseData(data);
	QVERIFY(transformMap(map, QTransform::fromTranslate(10.0, 0.0), MapTransformOptions{}));
	auto const& moved = map.courseData().route.perimeter;
	QCOMPARE(moved.size(), std::size_t(3));
	QVERIFY(std::abs(moved[0].x() - 11.0) < 1e-6 && std::abs(moved[0].y() - 1.0) < 1e-6);
	QVERIFY(std::abs(moved[2].x() - 25.0) < 1e-6 && std::abs(moved[2].y() - 9.0) < 1e-6);
}


QTEST_MAIN(RoutePerimeterTest)
#include "route_perimeter_t.moc"  // IWYU pragma: keep
