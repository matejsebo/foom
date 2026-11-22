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
#include <QApplication>
#include <QClipboard>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/crop.h"
#include "core/ops/flatten.h"
#include "core/ops/interpolate.h"
#include "core/ops/wkt.h"
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


	void wktTest()
	{
		// the design notes, "Well-known text", Accept.
		TestMap t;
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedRefPoint(QPointF(350000, 150000));
		t.map.setGeoreferencing(georef);
		auto const check = t.map.getGeoreferencing().toProjectedCoords(MapCoordF(10, -20));
		QVERIFY(std::abs(check.x() - 350100) < 1e-6 && std::abs(check.y() - 150200) < 1e-6);

		ops::WktParams projected;
		ops::WktParams paper;
		paper.projected = false;
		for (auto* p : { &projected, &paper })
		{
			p->point_symbol = t.point;
			p->line_symbol = t.line;
			p->area_symbol = t.area;
		}

		// (1), (2)
		std::unique_ptr<PathObject> line(makePath(t.line, { {0, 0}, {10, -20} }, false));
		std::unique_ptr<PointObject> point(makePoint(t.point, 1, -2.5));
		QCOMPARE(ops::toWkt(t.map, { line.get() }, projected), QStringLiteral("LINESTRING (350000 150000, 350100 150200)"));
		QCOMPARE(ops::toWkt(t.map, { line.get() }, paper), QStringLiteral("LINESTRING (0 0, 10 20)"));
		QCOMPARE(ops::toWkt(t.map, { point.get() }, projected), QStringLiteral("POINT (350010 150025)"));

		// (3)
		MapCoordVector holed_coords = { MapCoord(0, 0), MapCoord(10, 0), MapCoord(10, 10), MapCoord(0, 10), MapCoord(0, 0),
		                                MapCoord(2, 2), MapCoord(4, 2), MapCoord(4, 4), MapCoord(2, 2) };
		holed_coords[4].setClosePoint(true);
		holed_coords[4].setHolePoint(true);
		holed_coords[8].setClosePoint(true);
		std::unique_ptr<PathObject> holed(new PathObject(t.area, holed_coords));
		QCOMPARE(int(holed->parts().size()), 2);
		auto const polygon = ops::toWkt(t.map, { holed.get() }, paper);
		QCOMPARE(polygon, QStringLiteral("POLYGON ((0 0, 10 0, 10 -10, 0 -10, 0 0), (2 -2, 4 -2, 4 -4, 2 -2))"));
		MapCoordVector two_coords = { MapCoord(0, 0), MapCoord(5, 0), MapCoord(0, 5), MapCoord(5, 5) };
		two_coords[1].setHolePoint(true);
		std::unique_ptr<PathObject> two_parts(new PathObject(t.line, two_coords));
		QCOMPARE(ops::toWkt(t.map, { two_parts.get() }, paper), QStringLiteral("MULTILINESTRING ((0 0, 5 0), (0 -5, 5 -5))"));

		// (4)
		{
			MapCoordVector arc = { MapCoord(10.0, 0.0), MapCoord(10.0, 5.523), MapCoord(5.523, 10.0), MapCoord(0.0, 10.0) };
			arc[0].setCurveStart(true);
			std::unique_ptr<PathObject> curve(new PathObject(t.line, arc));
			auto const text = ops::toWkt(t.map, { curve.get() }, paper);
			QVERIFY(text.startsWith(QLatin1String("LINESTRING (10 0, ")));
			QVERIFY(text.endsWith(QLatin1String(", 0 -10)")));
			QVERIFY(text.count(QLatin1Char(',')) >= 4);
		}

		// (5)
		{
			auto const text = ops::toWkt(t.map, { line.get(), holed.get() }, projected);
			auto result = ops::fromWkt(t.map, text, projected);
			QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
			QCOMPARE(int(result.out.size()), 2);
			for (int i = 0; i < 2; ++i)
			{
				auto const* original = i == 0 ? line.get() : holed.get();
				auto const* pasted = result.out[std::size_t(i)]->asPath();
				pasted->update();
				QCOMPARE(pasted->parts().size(), original->parts().size());
				auto const& a = original->getRawCoordinateVector();
				auto const& b = pasted->getRawCoordinateVector();
				QCOMPARE(b.size(), a.size());
				for (std::size_t k = 0; k < a.size(); ++k)
					QVERIFY(MapCoordF(a[k]).distanceTo(MapCoordF(b[k])) <= 0.001);
			}
			QCOMPARE(result.out[0]->getSymbol(), t.line);
			QCOMPARE(result.out[1]->getSymbol(), t.area);
		}

		// (6)
		{
			auto result = ops::fromWkt(t.map, QStringLiteral("polygon z ((0 0 5, 10 0 5, 10 10 5, 0 0 5))"), paper);
			QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
			QCOMPARE(int(result.out.size()), 1);
			auto const* area = result.out.front()->asPath();
			area->update();
			QCOMPARE(int(area->parts().size()), 1);
			QVERIFY(area->parts().front().isClosed());
			QCOMPARE(int(area->parts().front().size()), 4);  // three corners and the close point

			for (auto const* text : { "MULTIPOINT ((1 2), (3 4))", "MULTIPOINT (1 2, 3 4)" })
			{
				result = ops::fromWkt(t.map, QString::fromLatin1(text), paper);
				QVERIFY2(result.error.isEmpty(), text);
				QCOMPARE(int(result.out.size()), 2);
				QCOMPARE(result.out.front()->getType(), Object::Point);
			}
			result = ops::fromWkt(t.map, QStringLiteral("SRID=26954;POINT(1 2)"), paper);
			QCOMPARE(int(result.out.size()), 1);
			QVERIFY(MapCoordF(result.out.front()->getRawCoordinateVector().front()).distanceTo(MapCoordF(1, -2)) < 0.001);
			result = ops::fromWkt(t.map, QStringLiteral("GEOMETRYCOLLECTION (POINT (1 2), LINESTRING (0 0, 1 1))"), paper);
			QCOMPARE(int(result.out.size()), 2);
			QCOMPARE(result.out[0]->getType(), Object::Point);
			QCOMPARE(result.out[1]->getType(), Object::Path);

			QVERIFY(!ops::fromWkt(t.map, QStringLiteral("POINT EMPTY"), paper).error.isEmpty());
			QVERIFY(!ops::fromWkt(t.map, QStringLiteral("hello"), paper).error.isEmpty());
			auto no_line = paper;
			no_line.line_symbol = nullptr;
			result = ops::fromWkt(t.map, QStringLiteral("LINESTRING (0 0, 1 1)"), no_line);
			QVERIFY(!result.error.isEmpty());
			QVERIFY(result.out.empty());
		}
	}


	void wktCommand()
	{
		// the design notes, "Well-known text", Accept (editor).
		TestMap t;
		auto* line = makePath(t.line, { {0, 0}, {10, -20} }, false);
		t.map.addObject(line);
		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, &t.map);
		window->setController(editor);
		t.taken = true;
		auto* copy_action = editor->getAction("copy-wkt");
		QVERIFY(copy_action);
		QVERIFY(editor->getAction("paste-wkt"));
		t.map.clearObjectSelection(true);
		QVERIFY(!copy_action->isEnabled());
		t.map.addObjectToSelection(line, true);
		QVERIFY(copy_action->isEnabled());
		auto const text = editor->copyWkt(true);
		QVERIFY(text.startsWith(QLatin1String("LINESTRING")));
		QCOMPARE(QApplication::clipboard()->text(), text);

		editor->getSymbolWidget()->selectSingleSymbol(t.line);
		auto const objects = t.map.getNumObjects();
		auto const steps = t.map.undoManager().undoStepCount();
		QVERIFY(editor->pasteWkt(text, true));
		QCOMPARE(t.map.getNumObjects(), objects + 1);
		QCOMPARE(t.map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(t.map.getNumSelectedObjects(), 1);
		QCOMPARE(t.map.getFirstSelectedObject()->getSymbol(), t.line);
		window->deleteLater();
	}


	void interpolateCurveFit()
	{
		// the design notes, "Interpolate", Accept (7).
		TestMap t;
		auto const arc = [&t](double radius) {
			std::vector<MapCoordF> points;
			for (int i = 0; i <= 128; ++i)
			{
				auto const angle = M_PI * i / 128.0;
				points.emplace_back(50.0 - radius * std::cos(angle), 50.0 - radius * std::sin(angle));
			}
			return std::unique_ptr<PathObject>(makePath(t.line, points, false));
		};
		auto a = arc(10);
		auto b = arc(20);
		ops::InterpolateParams params;
		params.curve_fit = true;
		auto result = ops::interpolate(t.map, { a.get(), b.get() }, params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 1);
		auto const& coords = result.out.front()->getRawCoordinateVector();
		QVERIFY(std::any_of(coords.begin(), coords.end(), [](const MapCoord& c) { return c.isCurveStart(); }));
		QVERIFY(near(MapCoordF(coords.front()), {35, 50}));
		QVERIFY(near(MapCoordF(coords.back()), {65, 50}));
		for (auto const& c : ops::flattenCoords(coords, 0.005))
			QVERIFY(std::abs(MapCoordF(c).distanceTo(MapCoordF(50, 50)) - 15.0) < 0.1);

		params.curve_fit = false;
		result = ops::interpolate(t.map, { a.get(), b.get() }, params);
		auto const& plain = result.out.front()->getRawCoordinateVector();
		QVERIFY(std::none_of(plain.begin(), plain.end(), [](const MapCoord& c) { return c.isCurveStart(); }));
		auto const vertices = [](const MapCoordVector& vector) {
			int count = 0;
			for (std::size_t i = 0; i < vector.size(); ++i, ++count)
			{
				if (vector[i].isCurveStart())
					i += 2;
			}
			return count;
		};
		QVERIFY(vertices(coords) < vertices(plain));
	}
};


QTEST_MAIN(ObjectCommandsTest)
#include "object_commands_t.moc"  // IWYU pragma: keep
