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
#include <algorithm>
#include <cstdlib>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/route_terrain.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "templates/template.h"
#include "templates/template_map.h"

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

LineSymbol* addLine(Map& map, int number, int width)
{
	auto* symbol = addSymbol<LineSymbol>(map, number);
	symbol->setLineWidth(width / 1000.0);
	return symbol;
}

/// A rectangle, or with an inner rectangle an area with a hole.
void addArea(Map& map, const Symbol* symbol, const QRectF& rect, const QRectF& hole = {})
{
	auto* object = new PathObject(symbol);
	for (auto const& r : { rect, hole })
	{
		if (!r.isValid())
			continue;
		auto const first = object->getCoordinateCount();
		object->addCoordinate(MapCoord(r.left(), r.top()), first > 0);
		object->addCoordinate(MapCoord(r.right(), r.top()));
		object->addCoordinate(MapCoord(r.right(), r.bottom()));
		object->addCoordinate(MapCoord(r.left(), r.bottom()));
		object->parts().back().setClosed(true);
	}
	map.addObject(object);
}

void addStroke(Map& map, const Symbol* symbol, double x1, double y1, double x2, double y2)
{
	auto* object = new PathObject(symbol);
	object->addCoordinate(MapCoord(x1, y1));
	object->addCoordinate(MapCoord(x2, y2));
	map.addObject(object);
}

struct TerrainMap
{
	Map map;
	AreaSymbol* open;
	AreaSymbol* slow;
	AreaSymbol* walk;
	AreaSymbol* walk_visible;
	AreaSymbol* fight;
	AreaSymbol* rough;
	AreaSymbol* stony;
	AreaSymbol* building;
	LineSymbol* path;
	LineSymbol* fence;
	
	TerrainMap()
	{
		map.setScaleDenominator(10000);
		open = addSymbol<AreaSymbol>(map, 401);
		slow = addSymbol<AreaSymbol>(map, 406);
		walk = addSymbol<AreaSymbol>(map, 408);
		walk_visible = addSymbol<AreaSymbol>(map, 409);
		fight = addSymbol<AreaSymbol>(map, 410);
		rough = addSymbol<AreaSymbol>(map, 403);
		stony = addSymbol<AreaSymbol>(map, 211);
		building = addSymbol<AreaSymbol>(map, 521);
		path = addLine(map, 505, 250);
		fence = addLine(map, 516, 140);
		
		// Open land on the right half; slow running across the middle of both halves.
		addArea(map, open, QRectF(10.0, 0.0, 10.0, 10.0));
		addArea(map, slow, QRectF(6.0, 1.0, 8.0, 2.0));
		// Walk, with fight over a part of it and stony ground over another; undergrowth beside it in the forest.
		addArea(map, walk, QRectF(1.0, 4.0, 4.0, 2.0));
		addArea(map, fight, QRectF(1.0, 4.0, 1.0, 2.0));
		addArea(map, stony, QRectF(3.0, 4.0, 1.0, 2.0));
		addArea(map, walk_visible, QRectF(5.0, 4.0, 2.0, 2.0));
		// Rough open land in the open land, with the same undergrowth over a part of it.
		addArea(map, rough, QRectF(11.0, 4.0, 3.0, 1.5));
		addArea(map, walk_visible, QRectF(12.0, 4.0, 1.0, 1.5));
		addArea(map, building, QRectF(15.0, 6.0, 2.0, 2.0));
		// A path from the left to the right at y = 7, across the building; a fence at y = 9.
		addStroke(map, path, 0.0, 7.0, 20.0, 7.0);
		addStroke(map, fence, 0.0, 9.0, 20.0, 9.0);
	}
	
	QRectF area() const { return QRectF(0.0, 0.0, 20.0, 10.0); }
};

}  // namespace



/**
 * @test Tests the runnability of the symbols and the rasters of the terrain.
 *
 * See the design notes, "Route analysis: runnability and
 * the terrain rasters".
 */
class RouteTerrainTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void defaultsTest();
	void ruleTest();
	void withoutRunnabilityTest();
	void holeTest();
	void saveTest();
};


void RouteTerrainTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteTerrainTest::defaultsTest()
{
	Map map;
	auto const of = [](const Symbol* symbol) { return defaultRunnability(*symbol); };
	QVERIFY(of(addSymbol<AreaSymbol>(map, 401)) == (Runnability{ Runnability::Ground, 86 }));
	QVERIFY(of(addSymbol<AreaSymbol>(map, 403)) == (Runnability{ Runnability::Ground, 83 }));
	QVERIFY(of(addSymbol<AreaSymbol>(map, 406)) == (Runnability{ Runnability::Vegetation, 65 }));
	QVERIFY(of(addSymbol<AreaSymbol>(map, 410)) == (Runnability{ Runnability::Vegetation, 10 }));
	QVERIFY(of(addSymbol<AreaSymbol>(map, 409)) == (Runnability{ Runnability::Hindrance, 37 }));
	QVERIFY(of(addSymbol<AreaSymbol>(map, 211)) == (Runnability{ Runnability::Surface, 37 }));
	QVERIFY(of(addSymbol<LineSymbol>(map, 505)) == (Runnability{ Runnability::Path, 93 }));
	QVERIFY(of(addSymbol<LineSymbol>(map, 503)) == (Runnability{ Runnability::Path, 100 }));
	QVERIFY(of(addSymbol<LineSymbol>(map, 516)) == (Runnability{ Runnability::Hindrance, 37 }));
	QVERIFY(of(addSymbol<LineSymbol>(map, 202)) == (Runnability{ Runnability::Hindrance, 10 }));
	// The second part of the number tells the difficult ride from the others, and the tags of a bank from the bank.
	auto* ride = addSymbol<LineSymbol>(map, 508);
	QVERIFY(of(ride) == (Runnability{ Runnability::Path, 83 }));
	ride->setNumberComponent(1, 3);
	QVERIFY(of(ride) == (Runnability{ Runnability::Path, 37 }));
	auto* tags = addSymbol<LineSymbol>(map, 104);
	QVERIFY(of(tags) == (Runnability{ Runnability::Hindrance, 37 }));
	tags->setNumberComponent(1, 4);
	QCOMPARE(of(tags).kind, Runnability::None);
	QCOMPARE(of(addSymbol<AreaSymbol>(map, 521)).kind, Runnability::Impassable);
	QCOMPARE(of(addSymbol<LineSymbol>(map, 518)).kind, Runnability::Impassable);
	QCOMPARE(of(addSymbol<AreaSymbol>(map, 709)).kind, Runnability::Impassable);
	QCOMPARE(of(addSymbol<LineSymbol>(map, 101)).kind, Runnability::None);
	QCOMPARE(of(addSymbol<PointSymbol>(map, 401)).kind, Runnability::None);
	
	auto* slow = addSymbol<AreaSymbol>(map, 406);
	QCOMPARE(routeSymbolNumber(*slow), QStringLiteral("406.000"));
	auto* second = addSymbol<AreaSymbol>(map, 406);
	second->setNumberComponent(1, 2);
	QCOMPARE(routeSymbolNumber(*second), QStringLiteral("406.002"));
	RouteSettings settings;
	QCOMPARE(settings.base_pace, 200);
	QVERIFY(settings.use_runnability && settings.use_elevation);
	QVERIFY(runnabilityOf(settings, *slow) == (Runnability{ Runnability::Vegetation, 65 }));
	settings.symbols.push_back({ QStringLiteral("406.000"), Runnability{ Runnability::Vegetation, 50 } });
	QVERIFY(runnabilityOf(settings, *slow) == (Runnability{ Runnability::Vegetation, 50 }));
	// The other symbol of the number keeps its default.
	QVERIFY(runnabilityOf(settings, *second) == (Runnability{ Runnability::Vegetation, 65 }));
	settings.symbols.front().second = Runnability{};
	QCOMPARE(runnabilityOf(settings, *slow).kind, Runnability::None);
}


/// Accept (2)
void RouteTerrainTest::ruleTest()
{
	TerrainMap m;
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	QVERIFY(!terrain.isEmpty());
	QCOMPARE(terrain.width, 158);
	QCOMPARE(terrain.height, 79);
	QCOMPARE(terrain.runnability.size(), std::size_t(158 * 79));
	QCOMPARE(terrain.impassable.size(), std::size_t(158 * 79));
	
	auto const at = [&terrain](double x, double y) { return terrain.runnabilityAt(MapCoordF(x, y)); };
	QCOMPARE(at(3.0, 0.5), 83);     // bare paper
	QCOMPARE(at(15.0, 0.5), 86);    // open land
	QCOMPARE(at(8.0, 2.0), 54);     // slow running over forest
	QCOMPARE(at(12.0, 2.0), 54);    // slow running covers the open land: forest again
	QCOMPARE(at(2.5, 5.0), 31);     // walk
	QCOMPARE(at(1.5, 5.0), 8);      // fight over walk: the slower one, not their product
	QCOMPARE(at(3.5, 5.0), 11);     // stony ground multiplies the walk
	QCOMPARE(at(6.0, 5.0), 31);     // undergrowth in the forest
	QCOMPARE(at(11.5, 5.0), 83);    // rough open land, which lies in the open land: the slower of the two
	QCOMPARE(at(12.5, 5.0), 37);    // undergrowth on rough open land: its own value
	QCOMPARE(at(3.0, 7.0), 93);     // the path in the forest
	QCOMPARE(at(12.0, 7.0), 93);    // and in the open land
	QCOMPARE(at(3.0, 9.0), 31);     // the fence in the forest
	QCOMPARE(at(12.0, 9.0), 37);    // and in the open land
	QCOMPARE(at(16.0, 6.3), 0);     // the building
	QVERIFY(terrain.isImpassableAt(MapCoordF(16.0, 6.3)));
	QCOMPARE(at(16.0, 7.0), 93);    // the path across it, which can be run
	QVERIFY(!terrain.isImpassableAt(MapCoordF(16.0, 7.0)));
	QVERIFY(!terrain.isImpassableAt(MapCoordF(3.0, 7.0)));
	// Outside the raster.
	QCOMPARE(at(-5.0, 3.0), 83);
	QVERIFY(!terrain.isImpassableAt(MapCoordF(25.0, 3.0)));
	
	// Without an area the extent of the objects.
	auto const whole = buildRouteTerrain(m.map, RouteSettings{});
	QVERIFY(!whole.isEmpty());
	QVERIFY(whole.area.contains(QRectF(0.5, 0.5, 19.0, 9.0)));
	QCOMPARE(whole.runnabilityAt(MapCoordF(8.0, 2.0)), 54);
	
	// A symbol which is set to count for nothing.
	RouteSettings settings;
	settings.symbols.push_back({ QStringLiteral("406.000"), Runnability{} });
	QCOMPARE(buildRouteTerrain(m.map, settings, m.area()).runnabilityAt(MapCoordF(8.0, 2.0)), 83);
	
	// A map without anything has no terrain.
	Map empty;
	QVERIFY(buildRouteTerrain(empty, RouteSettings{}).isEmpty());
}


/// Accept (3)
void RouteTerrainTest::withoutRunnabilityTest()
{
	TerrainMap m;
	RouteSettings settings;
	settings.use_runnability = false;
	auto const terrain = buildRouteTerrain(m.map, settings, m.area());
	QCOMPARE(terrain.runnabilityAt(MapCoordF(3.0, 0.5)), 100);
	QCOMPARE(terrain.runnabilityAt(MapCoordF(8.0, 2.0)), 100);
	QCOMPARE(terrain.runnabilityAt(MapCoordF(3.0, 9.0)), 100);
	QCOMPARE(terrain.runnabilityAt(MapCoordF(16.0, 6.3)), 0);
	QVERIFY(terrain.isImpassableAt(MapCoordF(16.0, 6.3)));
}


/// Accept (4)
void RouteTerrainTest::holeTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	auto* fight = addSymbol<AreaSymbol>(map, 410);
	addArea(map, open, QRectF(0.0, 0.0, 10.0, 10.0));
	addArea(map, fight, QRectF(2.0, 2.0, 6.0, 6.0), QRectF(4.0, 4.0, 2.0, 2.0));
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, QRectF(0.0, 0.0, 10.0, 10.0));
	QCOMPARE(terrain.runnabilityAt(MapCoordF(1.0, 1.0)), 86);
	QCOMPARE(terrain.runnabilityAt(MapCoordF(3.0, 5.0)), 8);   // forest times 0.10
	QCOMPARE(terrain.runnabilityAt(MapCoordF(5.0, 5.0)), 86);  // the hole
}


/// Accept (5)
void RouteTerrainTest::saveTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TerrainMap m;
	auto const terrain = buildRouteTerrain(m.map, RouteSettings{}, m.area());
	auto const base = QDir(dir.path()).absoluteFilePath(QStringLiteral("event"));
	QVERIFY(saveRouteTerrain(terrain, base));
	for (auto const* suffix : { ".~run", ".~imp" })
	{
		QImage image(base + QLatin1String(suffix) + QLatin1String(".png"));
		QVERIFY2(!image.isNull(), suffix);
		QCOMPARE(image.width(), 158);
		QCOMPARE(image.height(), 79);
		QFile world(base + QLatin1String(suffix) + QLatin1String(".pgw"));
		QVERIFY(world.open(QIODevice::ReadOnly));
		QCOMPARE(world.readAll().trimmed().split('\n').size(), 6);
	}
	QImage run(base + QLatin1String(".~run.png"));
	QCOMPARE(qGray(run.pixel(63, 15)), 54);  // (8.0, 2.0)
	QVERIFY(!saveRouteTerrain(RouteTerrain{}, base));
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteTerrainTest)
#include "route_terrain_t.moc"  // IWYU pragma: keep
