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
#include <memory>
#include <vector>

#include <QtTest>
#include <QAction>
#include <QObject>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/crop.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/widgets/symbol_widget.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// A map with one colour, a line, a second line and an area and a point symbol.
struct TestMap
{
	Map& map = *new Map();
	/** Set when an editor took the map, which deletes it. */
	bool taken = false;
	MapColor* black = nullptr;
	LineSymbol* line = nullptr;
	LineSymbol* other_line = nullptr;
	AreaSymbol* area = nullptr;
	PointSymbol* point = nullptr;

	TestMap()
	{
		black = new MapColor();
		black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
		black->setName(QStringLiteral("black"));
		map.addColor(black, 0);
		line = new LineSymbol();
		line->setLineWidth(0.2);
		line->setColor(black);
		map.addSymbol(line, 0);
		other_line = new LineSymbol();
		other_line->setLineWidth(0.4);
		other_line->setColor(black);
		map.addSymbol(other_line, 1);
		area = new AreaSymbol();
		area->setColor(black);
		map.addSymbol(area, 2);
		point = new PointSymbol();
		map.addSymbol(point, 3);
	}
	
	TestMap(const TestMap&) = delete;
	TestMap& operator=(const TestMap&) = delete;
	
	~TestMap()
	{
		if (!taken)
			delete &map;
	}
};

PathObject* makePath(const Symbol* symbol, const std::vector<MapCoordF>& points, bool closed)
{
	MapCoordVector coords;
	for (auto const& p : points)
		coords.push_back(MapCoord(p));
	auto* path = new PathObject(symbol, coords);
	if (closed)
		path->closeAllParts();
	path->update();
	return path;
}

PathObject* square(const Symbol* symbol, double x0, double y0, double x1, double y1)
{
	return makePath(symbol, { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} }, true);
}

PointObject* makePoint(const PointSymbol* symbol, double x, double y)
{
	auto* point = new PointObject(symbol);
	point->setPosition(MapCoordF(x, y));
	return point;
}

/// The end points of a single-part line.
std::pair<MapCoordF, MapCoordF> ends(const Object* object)
{
	auto const& coords = object->asPath()->getRawCoordinateVector();
	return { MapCoordF(coords.front()), MapCoordF(coords.back()) };
}

bool near(const MapCoordF& a, const MapCoordF& b, double tolerance = 0.01)
{
	return a.distanceTo(b) <= tolerance;
}

bool consumes(const ops::OpResult& result, const Object* object)
{
	return std::find(result.consumed.begin(), result.consumed.end(), object) != result.consumed.end();
}

/// The outputs sorted by the x of their first coordinate.
std::vector<const Object*> sortedOut(const ops::OpResult& result)
{
	std::vector<const Object*> out;
	for (auto const& object : result.out)
		out.push_back(object.get());
	std::sort(out.begin(), out.end(), [](const Object* a, const Object* b) {
		return a->getRawCoordinateVector().front().x() < b->getRawCoordinateVector().front().x();
	});
	return out;
}

}  // namespace



/**
 * @test Tests the object commands without a phase of the implementation order.
 *
 * See the design notes, "Object commands".
 */
class ObjectCommandsTest : public QObject
{
Q_OBJECT

private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		static QTemporaryDir settings_dir;
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	}


	void cropTest()
	{
		// the design notes, "Crop", Accept.
		TestMap t;
		std::unique_ptr<PathObject> region(square(t.area, 0, 0, 10, 10));
		std::unique_ptr<PathObject> line(makePath(t.line, { {-5, 5}, {15, 5} }, false));
		ops::CropParams params;
		params.shapes = { region.get() };

		// (1)
		{
			auto result = ops::crop(t.map, { line.get() }, params);
			QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
			QVERIFY(consumes(result, line.get()));
			QCOMPARE(int(result.out.size()), 1);
			auto const e = ends(result.out.front().get());
			QVERIFY(near(e.first, {0, 5}));
			QVERIFY(near(e.second, {10, 5}));
			QCOMPARE(result.out.front()->uuid(), line->uuid());
			QCOMPARE(result.out.front()->getSymbol(), t.line);
		}
		params.hole = true;
		{
			auto result = ops::crop(t.map, { line.get() }, params);
			QCOMPARE(int(result.out.size()), 2);
			auto const out = sortedOut(result);
			QVERIFY(near(ends(out[0]).first, {-5, 5}));
			QVERIFY(near(ends(out[0]).second, {0, 5}));
			QVERIFY(near(ends(out[1]).first, {10, 5}));
			QVERIFY(near(ends(out[1]).second, {15, 5}));
		}
		params.hole = false;

		// (2)
		{
			std::unique_ptr<PathObject> area(square(t.area, 5, 5, 15, 15));
			auto result = ops::crop(t.map, { area.get() }, params);
			QCOMPARE(int(result.out.size()), 1);
			QVERIFY(std::abs(result.out.front()->asPath()->calculatePaperArea() - 25.0) < 0.01);
			params.hole = true;
			result = ops::crop(t.map, { area.get() }, params);
			QCOMPARE(int(result.out.size()), 1);
			QVERIFY(std::abs(result.out.front()->asPath()->calculatePaperArea() - 75.0) < 0.01);
			params.hole = false;
		}

		// (3)
		{
			std::unique_ptr<PointObject> inside(makePoint(t.point, 2, 2));
			std::unique_ptr<PointObject> outside(makePoint(t.point, 20, 20));
			auto result = ops::crop(t.map, { inside.get(), outside.get() }, params);
			QVERIFY(!consumes(result, inside.get()));
			QVERIFY(consumes(result, outside.get()));
			QVERIFY(result.out.empty());
			params.hole = true;
			result = ops::crop(t.map, { inside.get(), outside.get() }, params);
			QVERIFY(consumes(result, inside.get()));
			QVERIFY(!consumes(result, outside.get()));
			QVERIFY(result.out.empty());
			params.hole = false;
		}

		// (4)
		{
			std::unique_ptr<PathObject> open(makePath(t.line, { {0, 0}, {10, 0}, {10, 10}, {0, 10} }, false));
			ops::CropParams open_params;
			open_params.shapes = { open.get() };
			auto result = ops::crop(t.map, { line.get() }, open_params);
			QCOMPARE(int(result.out.size()), 1);
			QVERIFY(near(ends(result.out.front().get()).first, {0, 5}));
			QVERIFY(near(ends(result.out.front().get()).second, {10, 5}));
		}

		// (5)
		{
			std::unique_ptr<PathObject> second(square(t.area, 20, 0, 30, 10));
			std::unique_ptr<PathObject> long_line(makePath(t.line, { {-5, 5}, {35, 5} }, false));
			ops::CropParams two;
			two.shapes = { region.get(), second.get() };
			auto result = ops::crop(t.map, { long_line.get() }, two);
			QCOMPARE(int(result.out.size()), 2);
			for (auto const& piece : result.out)
				QVERIFY(std::abs(piece->asPath()->getPaperLength() - 10.0) < 0.01);
		}

		// (6)
		{
			ops::CropParams rect;
			rect.rect = QRectF(0, 0, 10, 10);
			auto result = ops::crop(t.map, { line.get() }, rect);
			QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
			QCOMPARE(int(result.out.size()), 1);
			QVERIFY(near(ends(result.out.front().get()).first, {0, 5}));
			QVERIFY(near(ends(result.out.front().get()).second, {10, 5}));
		}

		// (7)
		{
			auto result = ops::crop(t.map, { line.get(), region.get() }, params);
			QVERIFY(!consumes(result, region.get()));
			QCOMPARE(int(result.out.size()), 1);
			params.delete_shapes = true;
			result = ops::crop(t.map, { line.get() }, params);
			QVERIFY(consumes(result, region.get()));
			QCOMPARE(int(result.out.size()), 1);
			params.delete_shapes = false;
		}

		// (8)
		{
			auto result = ops::crop(t.map, { line.get() }, ops::CropParams{});
			QVERIFY(!result.error.isEmpty());
			QVERIFY(result.out.empty());
			QVERIFY(result.consumed.empty());
		}
	}


	void cropCommand()
	{
		// the design notes, "Crop", Accept (editor).
		TestMap t;
		auto* region = square(t.area, 0, 0, 10, 10);
		auto* line = makePath(t.line, { {-5, 5}, {15, 5} }, false);
		auto* other = makePath(t.other_line, { {-5, 7}, {15, 7} }, false);
		t.map.addObject(region);
		t.map.addObject(line);
		t.map.addObject(other);

		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, &t.map);
		window->setController(editor);
		t.taken = true;
		QVERIFY(editor->getAction("crop"));

		t.map.clearObjectSelection(false);
		t.map.addObjectToSelection(region, true);
		editor->getSymbolWidget()->selectSingleSymbol(t.line);
		auto const steps = t.map.undoManager().undoStepCount();
		editor->crop(false, false, {}, false, false);
		QCOMPARE(t.map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(t.map.getNumObjects(), 3);
		auto const* part = t.map.getCurrentPart();
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (object->getSymbol() == t.area)
				continue;
			QVERIFY(std::abs(object->asPath()->getPaperLength() - 10.0) < 0.01);
		}
		QVERIFY(t.map.undoManager().undo());
		QCOMPARE(t.map.getNumObjects(), 3);
		QVERIFY(std::abs(line->getPaperLength() - 20.0) < 0.01);

		// The symbol pane follows the selection, so the symbol is chosen after it.
		t.map.clearObjectSelection(false);
		t.map.addObjectToSelection(region, true);
		editor->getSymbolWidget()->selectSingleSymbol(t.line);
		editor->crop(true, false, {}, false, false);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (object->getSymbol() == t.line)
				QVERIFY(std::abs(object->asPath()->getPaperLength() - 10.0) < 0.01);
			else if (object->getSymbol() == t.other_line)
				QVERIFY(std::abs(object->asPath()->getPaperLength() - 20.0) < 0.01);
		}
		window->deleteLater();
	}
};


QTEST_MAIN(ObjectCommandsTest)
#include "object_commands_t.moc"  // IWYU pragma: keep
