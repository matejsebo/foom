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
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QEvent>
#include <QKeyEvent>
#include <QLatin1Char>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/course_widget.h"
#include "tools/route_tool.h"
#include "tools/tool.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

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

Course courseOf(const char* name, const char* items)
{
	Course course;
	course.name = QString::fromLatin1(name);
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		CourseItem item;
		auto const first = word.at(0);
		item.type = first == QLatin1Char('S') ? CourseItem::Start
		            : first == QLatin1Char('M') ? CourseItem::MarkedRoute
		            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
		item.code = word;
		course.items.push_back(item);
	}
	return course;
}

/**
 * Open land of 20 by 10 mm with the start S1, the control 31 and the finish
 * F1 on a line, and a marked route M1 beside it; the courses A, B and M.
 */
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	auto* marked = addSymbol<LineSymbol>(map, 707, Symbol::CourseMarkedRoute);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	auto* land = new PathObject(open);
	land->addCoordinate(MapCoord(0.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 10.0));
	land->addCoordinate(MapCoord(0.0, 10.0));
	land->parts().back().setClosed(true);
	map.addObject(land);
	drawPoint(map, start, 2.0, 5.0);
	drawPoint(map, control, 10.0, 5.0);
	drawPoint(map, finish, 18.0, 5.0);
	auto* route = new PathObject(marked);
	route->addCoordinate(MapCoord(11.0, 7.0));
	route->addCoordinate(MapCoord(13.0, 7.0));
	push(map, route);
	
	auto data = map.courseData();
	data.courses.push_back(courseOf("A", "S1 31 F1"));
	data.courses.push_back(courseOf("B", "S1 31"));
	data.courses.push_back(courseOf("M", "S1 31 M1 F1"));
	changeCourseData(map, data);
}

QRectF mapArea()
{
	return QRectF(0.0, 0.0, 20.0, 10.0);
}

double distance(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// The index of the point of a route which is nearest to a place, and how far it is.
std::size_t nearestPoint(const std::vector<MapCoordF>& points, const MapCoordF& place, double* how_far = nullptr)
{
	std::size_t nearest = 0;
	for (std::size_t i = 1; i < points.size(); ++i)
	{
		if (distance(points[i], place) < distance(points[nearest], place))
			nearest = i;
	}
	if (how_far)
		*how_far = points.empty() ? 1e9 : distance(points[nearest], place);
	return nearest;
}

}  // namespace



/**
 * @test Tests the routes over via points and the tool of the route analysis.
 *
 * See the design notes, "Route analysis: via points and the
 * leg by hand".
 */
class RouteViaTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void searchTest();
	void storeTest();
	void addViaRouteTest();
	void keptTest();
	void legAtTest();
	void toolTest();
};


void RouteViaTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteViaTest::searchTest()
{
	Map map;
	fillMap(map);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, mapArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	auto const from = MapCoordF(2.0, 5.0);
	auto const to = MapCoordF(18.0, 5.0);
	
	auto const via = MapCoordF(10.0, 9.0);
	auto const route = search.fastestVia(from, { via }, to);
	QVERIFY(route.found);
	double how_far = 0.0;
	nearestPoint(route.points, via, &how_far);
	QVERIFY2(how_far <= 0.2, qPrintable(QString::number(how_far)));
	auto const straights = 2.0 * std::hypot(8.0, 4.0) * 10.0;
	QVERIFY2(std::abs(route.length_m / straights - 1.0) <= 0.03, qPrintable(QString::number(route.length_m)));
	auto const pieces = search.fastest(from, via).time_s + search.fastest(via, to).time_s;
	QVERIFY(std::abs(route.time_s - pieces) < 1e-6);
	QCOMPARE(route.points.front(), from);
	QCOMPARE(route.points.back(), to);
	QVERIFY(route.time_s > search.fastest(from, to).time_s);
	
	// Two via points, in their order.
	auto const first = MapCoordF(6.0, 2.0);
	auto const second = MapCoordF(14.0, 8.0);
	auto const over_two = search.fastestVia(from, { first, second }, to);
	QVERIFY(over_two.found);
	double first_far = 0.0;
	double second_far = 0.0;
	auto const at_first = nearestPoint(over_two.points, first, &first_far);
	auto const at_second = nearestPoint(over_two.points, second, &second_far);
	QVERIFY(first_far <= 0.2 && second_far <= 0.2);
	QVERIFY(at_first < at_second);
	
	// No way over a place outside the rasters.
	QVERIFY(!search.fastestVia(from, { MapCoordF(10.0, 30.0) }, to).found);
	QVERIFY(search.fastestVia(from, { MapCoordF(10.0, 30.0) }, to).points.empty());
	
	// Without via points, the fastest route.
	auto const plain = search.fastestVia(from, {}, to);
	auto const fastest = search.fastest(from, to);
	QVERIFY(plain.found);
	QCOMPARE(plain.time_s, fastest.time_s);
	QCOMPARE(plain.points.size(), fastest.points.size());
}


/// Accept (2)
void RouteViaTest::storeTest()
{
	StoredRoute fastest;
	fastest.from = QStringLiteral("S1");
	fastest.to = QStringLiteral("31");
	fastest.length_m = 80.0;
	fastest.time_s = 19.0;
	fastest.points = { MapCoordF(2.0, 5.0), MapCoordF(10.0, 5.0) };
	auto over = fastest;
	over.index = 1;
	over.length_m = 120.0;
	over.time_s = 28.0;
	over.via = { MapCoordF(4.0, 8.0), MapCoordF(8.0, 2.0) };
	over.points = { MapCoordF(2.0, 5.0), MapCoordF(4.0, 8.0), MapCoordF(8.0, 2.0), MapCoordF(10.0, 5.0) };
	
	StoredRoutes store;
	store.put(fastest);
	QCOMPARE(store.nextIndex(QStringLiteral("S1"), QStringLiteral("31")), 1);
	QVERIFY(store.viaRoutes(QStringLiteral("S1"), QStringLiteral("31")).empty());
	store.put(over);
	QCOMPARE(store.routes.size(), std::size_t(2));
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"))->index, 0);
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"))->time_s, 19.0);
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"), 1)->time_s, 28.0);
	QVERIFY(!store.find(QStringLiteral("S1"), QStringLiteral("31"), 2));
	QCOMPARE(store.nextIndex(QStringLiteral("S1"), QStringLiteral("31")), 2);
	auto third = over;
	third.index = 2;
	store.put(third);
	auto const via_routes = store.viaRoutes(QStringLiteral("S1"), QStringLiteral("31"));
	QCOMPARE(via_routes.size(), std::size_t(2));
	QCOMPARE(via_routes[0]->index, 1);
	QCOMPARE(via_routes[1]->index, 2);
	QVERIFY(over != fastest);
	QVERIFY(third != over);
	
	// The file.
	CourseData data;
	data.routes = store;
	QByteArray bytes;
	{
		QXmlStreamWriter writer(&bytes);
		saveCourseData(data, writer);
	}
	QVERIFY2(bytes.contains("index=\"1\""), bytes.constData());
	QVERIFY2(bytes.contains("via=\"4.000 8.000;8.000 2.000\""), bytes.constData());
	{
		QXmlStreamReader reader(bytes);
		QVERIFY(reader.readNextStartElement());
		auto const loaded = loadCourseData(reader);
		QVERIFY(!reader.hasError());
		QVERIFY(loaded.routes == store);
		auto const* read = loaded.routes.find(QStringLiteral("S1"), QStringLiteral("31"), 1);
		QVERIFY(read);
		QCOMPARE(read->via.size(), std::size_t(2));
		QCOMPARE(read->via[1], MapCoordF(8.0, 2.0));
		QVERIFY(loaded.routes.find(QStringLiteral("S1"), QStringLiteral("31"))->via.empty());
	}
	
	// Removed, the fastest stays.
	QVERIFY(store.remove(QStringLiteral("S1"), QStringLiteral("31"), 1));
	QVERIFY(!store.remove(QStringLiteral("S1"), QStringLiteral("31"), 1));
	QCOMPARE(store.routes.size(), std::size_t(2));
	QVERIFY(store.find(QStringLiteral("S1"), QStringLiteral("31")));
	QVERIFY(store.find(QStringLiteral("S1"), QStringLiteral("31"), 2));
}


/// Accept (3)
void RouteViaTest::addViaRouteTest()
{
	Map map;
	fillMap(map);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, mapArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	StoredRoutes store;
	analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QCOMPARE(store.routes.size(), std::size_t(2));
	auto const fastest = *store.find(QStringLiteral("S1"), QStringLiteral("31"));
	
	auto const added = addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 8.0) });
	QCOMPARE(added.reason, ViaRouteAdded::Added);
	QCOMPARE(added.index, 1);
	QVERIFY(added.route.found);
	QCOMPARE(added.fastest_time_s, fastest.time_s);
	QVERIFY(added.route.time_s >= fastest.time_s);
	auto const* kept = store.find(QStringLiteral("S1"), QStringLiteral("31"), 1);
	QVERIFY(kept);
	QCOMPARE(kept->via.size(), std::size_t(1));
	QCOMPARE(kept->points.front(), fastest.points.front());
	QCOMPARE(kept->points.back(), fastest.points.back());
	QCOMPARE(kept->time_s, added.route.time_s);
	QVERIFY(store.find(QStringLiteral("S1"), QStringLiteral("31")));
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"))->time_s, fastest.time_s);
	
	QCOMPARE(addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 2.0), MapCoordF(8.0, 8.0) }).index, 2);
	QCOMPARE(store.routes.size(), std::size_t(4));
	
	// What is refused.
	QCOMPARE(addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("F1"), { MapCoordF(6.0, 8.0) }).reason, ViaRouteAdded::NoFastestRoute);
	QCOMPARE(addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 30.0) }).reason, ViaRouteAdded::NoRoute);
	QCOMPARE(store.routes.size(), std::size_t(4));
	store.with_heights = true;
	QCOMPARE(addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 8.0) }).reason, ViaRouteAdded::OtherHeights);
	QCOMPARE(store.routes.size(), std::size_t(4));
}


/// Accept (4)
void RouteViaTest::keptTest()
{
	Map map;
	fillMap(map);
	StoredRoutes store;
	auto const all = analyseAllRoutesOfMap(map, store);
	QVERIFY(!all.tables.empty());
	auto const without = store;
	{
		auto const settings = map.courseData().route;
		auto const terrain = buildRouteTerrain(map, settings);
		RouteSearch search(terrain, settings, 10000);
		QCOMPARE(addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 8.0) }).reason, ViaRouteAdded::Added);
		
		// The course is analysed again, and its routes are taken in.
		StoredRoutes fresh;
		analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, fresh);
		auto taken = store;
		taken.take(fresh);
		QVERIFY(taken.find(QStringLiteral("S1"), QStringLiteral("31"), 1));
		QCOMPARE(taken.routes.size(), store.routes.size());
	}
	
	// The tables and the text of the export do not know the routes over via points.
	auto with_data = map.courseData();
	with_data.routes = store;
	auto without_data = map.courseData();
	without_data.routes = without;
	QCOMPARE(allRoutesText(with_data, with_data.routes, storedAllRoutes(map, with_data)),
	         allRoutesText(without_data, without_data.routes, storedAllRoutes(map, without_data)));
	auto const table = storedCourseRoutes(map, with_data, QStringLiteral("A"), -1);
	QCOMPARE(courseRoutesText(table), courseRoutesText(storedCourseRoutes(map, without_data, QStringLiteral("A"), -1)));
	QCOMPARE(table.legs.size(), std::size_t(2));
	QCOMPARE(table.legs[0].via_routes.size(), std::size_t(1));
	QCOMPARE(table.legs[0].via_indexes[0], 1);
	QVERIFY(table.legs[0].via_routes[0].found);
	QVERIFY(table.legs[1].via_routes.empty());
	
	// All courses are analysed anew: the route over via points is brought over.
	StoredRoutes anew;
	analyseAllRoutesOfMap(map, anew);
	QVERIFY(!anew.find(QStringLiteral("S1"), QStringLiteral("31"), 1));
	QCOMPARE(anew.takeViaRoutes(store), 1);
	QVERIFY(anew.find(QStringLiteral("S1"), QStringLiteral("31"), 1));
	
	// Not from routes of a search with heights.
	{
		auto high = store;
		high.with_heights = true;
		StoredRoutes other;
		analyseAllRoutesOfMap(map, other);
		QCOMPARE(other.takeViaRoutes(high), 0);
	}
	
	// Not when the control at its end has moved.
	PointObject* control = nullptr;
	auto* part = map.getPart(0);
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto* object = part->getObject(i);
		if (object->getType() == Object::Point && std::abs(object->asPoint()->getCoordF().x() - 10.0) < 0.01)
			control = object->asPoint();
	}
	QVERIFY(control);
	control->setPosition(MapCoordF(10.0, 6.0));
	control->update();
	StoredRoutes moved;
	analyseAllRoutesOfMap(map, moved);
	QCOMPARE(moved.takeViaRoutes(store), 0);
	QVERIFY(!moved.find(QStringLiteral("S1"), QStringLiteral("31"), 1));
}


/// Accept (5)
void RouteViaTest::legAtTest()
{
	Map map;
	fillMap(map);
	StoredRoutes store;
	analyseAllRoutesOfMap(map, store);
	auto data = map.courseData();
	data.routes = store;
	auto const routes = storedCourseRoutes(map, data, QStringLiteral("M"), -1);
	QCOMPARE(routes.legs.size(), std::size_t(4));
	QVERIFY(routes.legs[2].marked);
	
	QCOMPARE(routeLegAt(routes, MapCoordF(6.0, 5.1), 0.5), 0);
	QCOMPARE(routeLegAt(routes, MapCoordF(6.0, 6.0), 0.5), -1);
	// Near the control both legs are within the reach: the nearer one.
	QCOMPARE(routeLegAt(routes, MapCoordF(9.5, 5.2), 1.0), 0);
	QCOMPARE(routeLegAt(routes, MapCoordF(10.4, 5.5), 1.0), 1);
	// The marked route is no leg.
	QCOMPARE(routeLegAt(routes, MapCoordF(12.0, 7.0), 0.5), -1);
	QCOMPARE(routeLegAt(CourseRoutes{}, MapCoordF(6.0, 5.0), 0.5), -1);
	
	// The routes over via points.
	auto leg = -1;
	auto via = -1;
	QVERIFY(!viaRouteAt(routes, MapCoordF(6.0, 8.0), 0.5, leg, via));
	{
		auto const settings = map.courseData().route;
		auto const terrain = buildRouteTerrain(map, settings);
		RouteSearch search(terrain, settings, 10000);
		QCOMPARE(addViaRoute(data.routes, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 8.0) }).reason, ViaRouteAdded::Added);
	}
	auto const with_via = storedCourseRoutes(map, data, QStringLiteral("M"), -1);
	QVERIFY(viaRouteAt(with_via, MapCoordF(6.0, 8.1), 0.5, leg, via));
	QCOMPARE(leg, 0);
	QCOMPARE(via, 0);
	QVERIFY(!viaRouteAt(with_via, MapCoordF(15.0, 2.0), 0.5, leg, via));
}


/// Accept (6)
void RouteViaTest::toolTest()
{
	auto* map = new Map();
	fillMap(*map);
	{
		StoredRoutes store;
		analyseAllRoutesOfMap(*map, store);
		auto data = map->courseData();
		data.routes = store;
		changeCourseData(*map, data);
	}
	
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	auto* widget = editor->getMainWidget();
	for (auto const* id : { "course-routes-via", "course-routes-leg" })
		QVERIFY2(editor->getAction(id), id);
	
	editor->getAction("course-dock")->trigger();
	auto* dock = window->findChild<CourseWidget*>();
	QVERIFY(dock);
	dock->setCurrentCourse(QStringLiteral("A"));
	QCOMPARE(dock->currentCourse(), QStringLiteral("A"));
	
	editor->getAction("course-routes-via")->trigger();
	auto* tool = dynamic_cast<RouteTool*>(editor->getTool());
	QVERIFY(tool);
	QCOMPARE(tool->mode(), RouteTool::ViaPoints);
	
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const drag = [widget, &viewport](double x1, double y1, double x2, double y2, Qt::KeyboardModifiers at_release = Qt::NoModifier) {
		auto const begin = viewport(x1, y1);
		auto const end = viewport(x2, y2);
		QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, begin);
		for (auto const& at : { (begin + end) / 2, end })
		{
			QMouseEvent move(QEvent::MouseMove, at, widget->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(widget, &move);
		}
		QTest::mouseRelease(widget, Qt::LeftButton, at_release, end);
	};
	auto const viaRoute = [map](int index) { return map->courseData().routes.find(QStringLiteral("S1"), QStringLiteral("31"), index); };
	auto const steps = map->undoManager().undoStepCount();
	auto const kept = map->courseData().routes.routes.size();
	
	// From the route of the leg S1-31 to a place beside it.
	drag(6.0, 5.0, 6.0, 9.0);
	QVERIFY(viaRoute(1));
	QCOMPARE(viaRoute(1)->via.size(), std::size_t(1));
	QVERIFY2(distance(viaRoute(1)->via[0], MapCoordF(6.0, 9.0)) < 0.5, qPrintable(QString::number(viaRoute(1)->via[0].y())));
	QVERIFY(tool->lastRoute().found);
	QCOMPARE(map->undoManager().undoStepCount(), steps + 1);
	QVERIFY(map->undoManager().undo());
	QVERIFY(!viaRoute(1));
	QCOMPARE(map->courseData().routes.routes.size(), kept);
	
	// A drag which begins beside every route does nothing.
	drag(6.0, 1.0, 9.0, 1.0);
	QVERIFY(!viaRoute(1));
	QVERIFY(tool->pendingViaPoints().empty());
	
	// With Shift, more via points first.
	drag(6.0, 5.0, 5.0, 9.0, Qt::ShiftModifier);
	QVERIFY(!viaRoute(1));
	QCOMPARE(tool->pendingViaPoints().size(), std::size_t(1));
	drag(8.0, 3.0, 9.0, 1.0);
	QVERIFY(viaRoute(1));
	QCOMPARE(viaRoute(1)->via.size(), std::size_t(2));
	QVERIFY(distance(viaRoute(1)->via[1], MapCoordF(9.0, 1.0)) < 0.5);
	QVERIFY(tool->pendingViaPoints().empty());
	
	// Escape drops the via point which waits.
	drag(6.0, 5.0, 7.0, 2.0, Qt::ShiftModifier);
	QCOMPARE(tool->pendingViaPoints().size(), std::size_t(1));
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QVERIFY(tool->keyPressEvent(&escape));
	QVERIFY(tool->pendingViaPoints().empty());
	drag(4.0, 5.0, 4.0, 8.5);
	QVERIFY(viaRoute(2));
	QCOMPARE(viaRoute(2)->via.size(), std::size_t(1));
	
	// Ctrl and a click on a route over via points removes it.
	QTest::mouseClick(widget, Qt::LeftButton, Qt::ControlModifier, viewport(4.0, 8.5));
	QVERIFY(!viaRoute(2));
	QVERIFY(viaRoute(1));
	
	// The leg by hand.
	editor->getAction("course-routes-leg")->trigger();
	tool = dynamic_cast<RouteTool*>(editor->getTool());
	QVERIFY(tool);
	QCOMPARE(tool->mode(), RouteTool::FreeLeg);
	auto const before = map->courseData().routes;
	auto const steps_before = map->undoManager().undoStepCount();
	drag(3.0, 2.0, 17.0, 2.0);
	QVERIFY(tool->lastRoute().found);
	QVERIFY2(std::abs(tool->lastRoute().length_m / 140.0 - 1.0) < 0.05, qPrintable(QString::number(tool->lastRoute().length_m)));
	QVERIFY(map->courseData().routes == before);
	QCOMPARE(map->undoManager().undoStepCount(), steps_before);
	
	editor->setTool(nullptr);
	window->deleteLater();
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteViaTest)
#include "route_via_t.moc"  // IWYU pragma: keep
