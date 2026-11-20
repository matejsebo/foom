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
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QRectF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/route_search.h"
#include "course/route_terrain.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "templates/template.h"

using namespace OpenOrienteering;


namespace {

template <class S>
S* addSymbol(Map& map, int number)
{
	auto* symbol = new S();
	symbol->setNumberComponent(0, number);
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

void addStroke(Map& map, const Symbol* symbol, const std::vector<QPointF>& points)
{
	auto* object = new PathObject(symbol);
	for (auto const& point : points)
		object->addCoordinate(MapCoord(point));
	map.addObject(object);
}

QRectF fieldArea()
{
	return QRectF(0.0, 0.0, 20.0, 10.0);
}

}  // namespace



/**
 * @test Tests the search of the fastest route and the factor of the slope.
 *
 * See the design notes, "Route analysis: the search".
 */
class RouteSearchTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void slopeFactorTest();
	void straightTest();
	void aroundTest();
	void roadTest();
	void slopeTest();
};


void RouteSearchTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteSearchTest::slopeFactorTest()
{
	QCOMPARE(slopeFactor(0.0), 1.0);
	QCOMPARE(slopeFactor(0.016), 1.0);
	QCOMPARE(slopeFactor(-0.019), 1.0);
	QVERIFY(std::abs(slopeFactor(0.1) - 1.515) < 0.002);
	QVERIFY(std::abs(slopeFactor(0.31) - 11.45 * 0.31) < 1e-9);
	QVERIFY(std::abs(slopeFactor(0.4) - 4.58) < 1e-9);
	QVERIFY(std::abs(slopeFactor(-0.09) - 0.854) < 0.001);
	QCOMPARE(slopeFactor(-0.18), 1.0);
	QVERIFY(std::abs(slopeFactor(-0.23) - 1.191) < 0.001);
	QVERIFY(std::abs(slopeFactor(-0.3) - 1.29) < 1e-9);
	// Nowhere below the least factor of the search.
	for (double m = -0.5; m <= 0.5; m += 0.001)
		QVERIFY2(slopeFactor(m) >= 0.85, qPrintable(QString::number(m)));
}


/// Accept (2)
void RouteSearchTest::straightTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	addArea(map, open, QRectF(0.0, 6.0, 20.0, 4.0));
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	// 160 m through the forest: 200 s/km at 83 percent.
	auto route = search.fastest(MapCoordF(2.0, 3.0), MapCoordF(18.0, 3.0));
	QVERIFY(route.found);
	QVERIFY2(std::abs(route.length_m - 160.0) < 2.0, qPrintable(QString::number(route.length_m)));
	QVERIFY2(std::abs(route.time_s - 160.0 * 0.2 / 0.83) < 0.8, qPrintable(QString::number(route.time_s)));
	QCOMPARE(route.climb_m, 0.0);
	QVERIFY(route.points.size() >= 2);
	QVERIFY(route.points.front() == MapCoordF(2.0, 3.0));
	QVERIFY(route.points.back() == MapCoordF(18.0, 3.0));
	QVERIFY(std::abs(search.straightTime(MapCoordF(2.0, 3.0), MapCoordF(18.0, 3.0)) - 160.0 * 0.2 / 0.83) < 0.3);
	
	// A slanting line of one of the 16 headings is as long as it is.
	route = search.fastest(MapCoordF(2.0, 1.0), MapCoordF(10.0, 5.0));
	QVERIFY(route.found);
	QVERIFY2(std::abs(route.length_m - std::hypot(80.0, 40.0)) < 2.0, qPrintable(QString::number(route.length_m)));
	// Any other heading within four percent.
	route = search.fastest(MapCoordF(2.0, 1.0), MapCoordF(17.0, 4.5));
	QVERIFY(route.found);
	auto const straight = std::hypot(150.0, 35.0);
	QVERIFY2(route.length_m >= straight - 1.5 && route.length_m < straight * 1.04, qPrintable(QString::number(route.length_m)));
	
	// In the open land at 86 percent.
	route = search.fastest(MapCoordF(2.0, 8.0), MapCoordF(18.0, 8.0));
	QVERIFY2(std::abs(route.time_s - 160.0 * 0.2 / 0.86) < 0.8, qPrintable(QString::number(route.time_s)));
	
	// With another base pace the time follows.
	RouteSettings slow;
	slow.base_pace = 400;
	RouteSearch slow_search(terrain, slow, 10000);
	QVERIFY(std::abs(slow_search.fastest(MapCoordF(2.0, 3.0), MapCoordF(18.0, 3.0)).time_s - 2 * 160.0 * 0.2 / 0.83) < 1.6);
	
	// A place outside the terrain.
	QVERIFY(!search.fastest(MapCoordF(-5.0, 3.0), MapCoordF(18.0, 3.0)).found);
}


/// Accept (3)
void RouteSearchTest::aroundTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* building = addSymbol<AreaSymbol>(map, 521);
	auto* fence = addSymbol<LineSymbol>(map, 518);
	// A wall across the way, with its end 1 mm below the line.
	addArea(map, building, QRectF(9.0, 0.0, 2.0, 6.0));
	// A ring around a place in the corner.
	addStroke(map, fence, { QPointF(16.0, 7.0), QPointF(19.0, 7.0), QPointF(19.0, 9.5), QPointF(16.0, 9.5), QPointF(16.0, 7.0) });
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	auto route = search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0));
	QVERIFY(route.found);
	// Around the corner at (9, 6) and (11, 6): 70.7 + 20 + 70.7 m.
	auto const around = 2 * std::hypot(70.0, 10.0) + 20.0;
	QVERIFY2(route.length_m > around - 3.0 && route.length_m < around * 1.04, qPrintable(QString::number(route.length_m)));
	for (auto const& point : route.points)
		QVERIFY(!terrain.isImpassableAt(point));
	
	// Into the ring no way leads; from within it to within it one does.
	QVERIFY(!search.fastest(MapCoordF(2.0, 5.0), MapCoordF(17.5, 8.0)).found);
	QVERIFY(search.fastest(MapCoordF(17.0, 8.0), MapCoordF(18.0, 8.5)).found);
	// The search can be used again.
	QVERIFY(search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0)).found);
	
	// A place on the wall is taken to its edge.
	route = search.fastest(MapCoordF(9.2, 3.0), MapCoordF(2.0, 3.0));
	QVERIFY(route.found);
	QVERIFY2(std::abs(route.length_m - 70.0) < 4.0, qPrintable(QString::number(route.length_m)));
}


/// Accept (4)
void RouteSearchTest::roadTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* fight = addSymbol<AreaSymbol>(map, 410);
	auto* road = addSymbol<LineSymbol>(map, 503);
	addArea(map, fight, fieldArea());
	// A road with a bend, from the one place to the other.
	addStroke(map, road, { QPointF(2.0, 2.0), QPointF(10.0, 8.0), QPointF(18.0, 2.0) });
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	auto const route = search.fastest(MapCoordF(2.0, 2.0), MapCoordF(18.0, 2.0));
	QVERIFY(route.found);
	// Along the road, 200 m at full speed, not 160 m through the thicket at 8 percent.
	QVERIFY2(route.length_m > 196.0 && route.length_m < 214.0, qPrintable(QString::number(route.length_m)));
	QVERIFY2(route.time_s > 39.0 && route.time_s < 47.0, qPrintable(QString::number(route.time_s)));
	QVERIFY(search.straightTime(MapCoordF(2.0, 2.0), MapCoordF(18.0, 2.0)) > 300.0);
	// It passes the bend.
	auto nearest = 1e9;
	for (auto const& point : route.points)
		nearest = std::min(nearest, std::hypot(point.x() - 10.0, point.y() - 8.0));
	QVERIFY2(nearest < 0.4, qPrintable(QString::number(nearest)));
}


/// Accept (5)
void RouteSearchTest::slopeTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	addArea(map, open, fieldArea());
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	auto const flat = 160.0 * 0.2 / 0.86;
	
	// A plane which rises to the east by 1 m in 10 m.
	RouteSearch search(terrain, RouteSettings{}, 10000);
	search.setHeights([](const MapCoordF& place) { return place.x() * 10.0 * 0.1; });
	auto route = search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0));
	QVERIFY(route.found);
	QVERIFY2(std::abs(route.time_s / flat - slopeFactor(0.1)) < 0.03, qPrintable(QString::number(route.time_s / flat)));
	QVERIFY2(std::abs(route.climb_m - 16.0) < 0.5, qPrintable(QString::number(route.climb_m)));
	route = search.fastest(MapCoordF(18.0, 5.0), MapCoordF(2.0, 5.0));
	QVERIFY2(std::abs(route.time_s / flat - slopeFactor(-0.1)) < 0.03, qPrintable(QString::number(route.time_s / flat)));
	QCOMPARE(route.climb_m, 0.0);
	// Along the contour as on flat ground.
	route = search.fastest(MapCoordF(5.0, 1.0), MapCoordF(5.0, 9.0));
	QVERIFY2(std::abs(route.time_s - 80.0 * 0.2 / 0.86) < 0.5, qPrintable(QString::number(route.time_s)));
	
	// Where the heights are not known, and when the settings do not use them: flat.
	search.setHeights([](const MapCoordF&) { return std::nan(""); });
	QVERIFY(std::abs(search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0)).time_s - flat) < 0.8);
	RouteSettings without;
	without.use_elevation = false;
	RouteSearch flat_search(terrain, without, 10000);
	flat_search.setHeights([](const MapCoordF& place) { return place.x(); });
	QVERIFY(std::abs(flat_search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0)).time_s - flat) < 0.8);
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteSearchTest)
#include "route_search_t.moc"  // IWYU pragma: keep
