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
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/object_information.h"
#include "core/selection_queries.h"
#include "core/status_sets.h"
#include "core/symbol_compare.h"
#include "core/symbol_descriptions.h"
#include "core/symbol_tools.h"
#include "core/symbol_tree.h"
#include "core/symbol_update.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/text_object.h"
#include "core/objects/object.h"
#include "core/ops/align.h"
#include "core/ops/apply_op.h"
#include "core/ops/flatten.h"
#include "core/ops/group.h"
#include "core/ops/bounds.h"
#include "core/ops/convert.h"
#include "core/ops/interpolate.h"
#include "core/ops/mirror.h"
#include "core/ops/offset.h"
#include "core/ops/transform.h"
#include "core/ops/vertex_type.h"
#include "core/ops/op.h"
#include "core/ops/replace_text.h"
#include "core/ops/reshape.h"
#include "core/ops/buildings.h"
#include "core/ops/smooth.h"
#include "core/ops/frame.h"
#include "core/ops/stairway.h"
#include "core/ops/to_curve.h"
#include "core/ops/rectangle_box.h"
#include "core/ops/topology.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/symbols/text_symbol.h"
#include "undo/undo_manager.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include <QDir>
#include <QFileInfo>

using namespace OpenOrienteering;


namespace {

/// An operation which replaces the first input by two moved copies and consumes the second.
ops::OpResult replaceFirstConsumeSecond(const Map& /*map*/, const std::vector<const Object*>& in, const ops::OpParams& /*params*/)
{
	ops::OpResult result;
	if (in.size() != 2)
	{
		result.error = QStringLiteral("Two objects required.");
		return result;
	}
	for (auto dx : { 1000, 2000 })
	{
		auto copy = std::unique_ptr<Object>(in[0]->duplicate());
		copy->move(dx, 0);
		result.out.push_back(std::move(copy));
	}
	result.consumed = in;
	return result;
}

/// An operation which always fails.
ops::OpResult failingOp(const Map& /*map*/, const std::vector<const Object*>& /*in*/, const ops::OpParams& /*params*/)
{
	ops::OpResult result;
	result.error = QStringLiteral("This operation always fails.");
	return result;
}

// The convention: the design notes, "Geometry operations".
static_assert(std::is_same<decltype(&replaceFirstConsumeSecond), ops::Op>::value, "Op signature");
static_assert(std::is_same<decltype(&failingOp), ops::Op>::value, "Op signature");

}  // namespace



/**
 * Tests the geometry-operation convention, applyOp and the flatten operation.
 */
class OpsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	
	void applyOpTest();
	void applyOpErrorTest();
	void flattenTest();
	void mirrorTest();
	void transformTest();
	void alignTest();
	void distributeTest();
	void reshapeTest();
	void offsetTest();
	void vertexTypeTest();
	void groupTest();
	void replaceTextTest();
	void topologyTest();
	void mergeContoursTest();
	void selectionQueriesTest();
	void interpolateTest();
	void objectInformationTest();
	void symbolToolsTest();
	void statusSetsTest();
	void symbolCompareTest();
	void convertToPointsTest();
	void symbolDescriptionsTest();
	void favouritesTest();
	void symbolTreeTest();
	void symbolUpdateTest();
	void buildingsTest();
	void smoothTest();
	void toCurveTest();
	void stairwayTest();
	void frameTest();
	void rectangleBoxTest();
};


void OpsTest::initTestCase()
{
	doStaticInitializations();
}

void OpsTest::applyOpTest()
{
	// the design notes, "Geometry operations", Accept (2).
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* first = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	first->setTag(QStringLiteral("key"), QStringLiteral("value"));
	first->setIdentity(first->uuid(), 1700000000, 1700000000);
	auto* second = new PathObject(symbol, { MapCoord(0.0, 5.0), MapCoord(10.0, 5.0) });
	map.addObject(first);
	map.addObject(second);
	map.addObjectToSelection(first, false);
	map.addObjectToSelection(second, false);
	auto const first_uuid = first->uuid();
	auto const second_uuid = second->uuid();
	auto* part = map.getCurrentPart();
	auto const undo_steps = map.undoManager().undoStepCount();
	
	// Two outputs replace the first object; the second is consumed.
	auto const before = Object::currentTime();
	auto result = replaceFirstConsumeSecond(map, { first, second }, ops::OpParams{});
	QVERIFY(result.error.isEmpty());
	QVERIFY(ops::applyOp(map, std::move(result), first));
	QCOMPARE(part->getNumObjects(), 2);
	auto* out0 = part->getObject(0);
	auto* out1 = part->getObject(1);
	QVERIFY(out0 != first && out1 != first && out0 != second && out1 != second);
	QCOMPARE(out0->getSymbol(), symbol);
	QCOMPARE(out1->getSymbol(), symbol);
	QCOMPARE(out0->tags(), first->tags());
	QCOMPARE(out1->tags(), first->tags());
	QCOMPARE(out0->uuid(), first_uuid);
	QVERIFY(out1->uuid() != first_uuid);
	QCOMPARE(out1->derivedFrom(), first_uuid);
	QCOMPARE(out0->createdTime(), qint64(1700000000));
	QVERIFY(out0->modifiedTime() >= before);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ out0, out1 }));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 1);
	QCOMPARE(map.findObject(first_uuid), out0);
	
	// One undo restores both inputs with their uuids and the previous selection.
	QVERIFY(map.undoManager().undo());
	QCOMPARE(part->getNumObjects(), 2);
	QCOMPARE(part->getObject(0), first);
	QCOMPARE(part->getObject(1), second);
	QCOMPARE(first->uuid(), first_uuid);
	QCOMPARE(second->uuid(), second_uuid);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ first, second }));
	QCOMPARE(map.findObject(first_uuid), first);
	
	// Redo reproduces the outputs.
	QVERIFY(map.undoManager().redo());
	QCOMPARE(part->getNumObjects(), 2);
	QCOMPARE(part->getObject(0), out0);
	QCOMPARE(part->getObject(1), out1);
	QCOMPARE(out0->uuid(), first_uuid);
	QCOMPARE(out1->derivedFrom(), first_uuid);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ out0, out1 }));
}

void OpsTest::applyOpErrorTest()
{
	// the design notes, "Geometry operations", Accept (3).
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* object = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.addObject(object);
	map.addObjectToSelection(object, false);
	auto* part = map.getCurrentPart();
	auto const undo_steps = map.undoManager().undoStepCount();
	
	auto result = failingOp(map, { object }, ops::OpParams{});
	QVERIFY(!result.error.isEmpty());
	auto const message = result.error;
	QVERIFY(!ops::applyOp(map, std::move(result), object));
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0), object);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ object }));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps);
	QCOMPARE(message, QStringLiteral("This operation always fails."));
}

void OpsTest::flattenTest()
{
	// the design notes, "To polyline".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 1);
	
	// A quarter circle of radius 10 mm as one cubic Bezier, then a straight segment.
	auto* curved = new PathObject(symbol, {
		MapCoord(10.0, 0.0, MapCoord::CurveStart), MapCoord(10.0, 5.5228), MapCoord(5.5228, 10.0),
		MapCoord(0.0, 10.0, MapCoord::DashPoint), MapCoord(-10.0, 10.0) });
	curved->setTag(QStringLiteral("key"), QStringLiteral("value"));
	auto const uuid = curved->uuid();
	map.addObject(curved);
	auto* straight = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.addObject(straight);
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	
	ops::FlattenParams params;
	params.tolerance = 0.05;
	auto result = ops::flatten(map, { curved, straight, point }, params);
	QVERIFY(result.error.isEmpty());
	QCOMPARE(result.out.size(), std::size_t(1));
	QCOMPARE(result.consumed.size(), std::size_t(1));
	QCOMPARE(result.consumed.front(), static_cast<const Object*>(curved));
	auto const* out = result.out.front()->asPath();
	QVERIFY(out);
	auto const& out_coords = out->getRawCoordinateVector();
	QVERIFY(out_coords.size() > 3);
	for (auto const& coord : out_coords)
		QVERIFY(!coord.isCurveStart());
	QCOMPARE(out_coords.front(), MapCoord(10.0, 0.0));
	QCOMPARE(out_coords.back(), MapCoord(-10.0, 10.0));
	QCOMPARE(out_coords[out_coords.size() - 2], MapCoord(0.0, 10.0, MapCoord::DashPoint));
	// The midpoints of the segments along the arc lie within the tolerance of
	// the circle (the Bezier itself deviates from it by less than 0.003 mm).
	for (std::size_t i = 1; i + 1 < out_coords.size(); ++i)
	{
		auto const mid = (QPointF(MapCoordF(out_coords[i-1])) + QPointF(MapCoordF(out_coords[i]))) / 2;
		QVERIFY(std::abs(std::hypot(mid.x(), mid.y()) - 10.0) <= params.tolerance + 0.003);
	}
	// The output carries the input's symbol, tags and uuid.
	QCOMPARE(out->getSymbol(), symbol);
	QCOMPARE(out->tags(), curved->tags());
	QCOMPARE(out->uuid(), uuid);
	
	// A coarser tolerance gives fewer vertices.
	ops::FlattenParams coarse_params;
	coarse_params.tolerance = 0.5;
	auto coarse = ops::flatten(map, { curved }, coarse_params);
	QCOMPARE(coarse.out.size(), std::size_t(1));
	QVERIFY(coarse.out.front()->asPath()->getRawCoordinateVector().size() < out_coords.size());
	QVERIFY(coarse.out.front()->asPath()->getRawCoordinateVector().size() >= 3);
	
	// A closed input stays closed.
	auto* closed = new PathObject(symbol, {
		MapCoord(0.0, 0.0, MapCoord::CurveStart), MapCoord(0.0, 10.0), MapCoord(10.0, 10.0),
		MapCoord(10.0, 0.0), MapCoord(0.0, 0.0, MapCoord::ClosePoint) });
	map.addObject(closed);
	auto closed_result = ops::flatten(map, { closed }, params);
	QCOMPARE(closed_result.out.size(), std::size_t(1));
	auto const* closed_out = closed_result.out.front()->asPath();
	QCOMPARE(closed_out->parts().size(), std::size_t(1));
	QVERIFY(closed_out->parts().front().isClosed());
	QVERIFY(closed_out->getRawCoordinateVector().back().isClosePoint());
	
	// A result with nothing to do leaves the selection alone.
	map.addObjectToSelection(straight, false);
	QVERIFY(ops::applyOp(map, ops::flatten(map, { straight }, params)));
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ straight }));
	map.clearObjectSelection(false);
	
	// Through applyOp, the output takes the input's place.
	map.addObjectToSelection(curved, false);
	QVERIFY(ops::applyOp(map, std::move(result)));
	QCOMPARE(map.findObject(uuid), static_cast<const Object*>(out));
	QVERIFY(map.getCurrentPart()->findObjectIndex(out) >= 0);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ const_cast<Object*>(static_cast<const Object*>(out)) }));
}


void OpsTest::mirrorTest()
{
	// the design notes, "Mirror".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	point_symbol->setRotatable(true);
	map.addSymbol(point_symbol, 1);
	auto const line_coords = MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 4.0) };
	auto* line = new PathObject(symbol, line_coords);
	auto const uuid = line->uuid();
	map.addObject(line);
	
	auto const mirrored = [&map, line](double axis_angle) {
		ops::MirrorParams params;
		params.axis_angle = axis_angle;
		auto result = ops::mirror(map, { line }, params);
		Q_ASSERT(result.error.isEmpty() && result.out.size() == 1);
		return result.out.front()->asPath()->getRawCoordinateVector();
	};
	QCOMPARE(mirrored(90.0), (MapCoordVector{ MapCoord(0.0, 4.0), MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
	QCOMPARE(mirrored(0.0), (MapCoordVector{ MapCoord(10.0, 0.0), MapCoord(10.0, 4.0), MapCoord(0.0, 4.0) }));
	QCOMPARE(mirrored(45.0), (MapCoordVector{ MapCoord(3.0, -3.0), MapCoord(7.0, -3.0), MapCoord(7.0, 7.0) }));
	
	// A rotatable point: position and rotation are mirrored.
	auto* point = new PointObject(point_symbol);
	point->setPosition(MapCoord(10.0, 0.0));
	point->setRotation(0.3);
	map.addObject(point);
	ops::MirrorParams vertical;
	vertical.axis_angle = 90.0;
	auto point_result = ops::mirror(map, { point }, vertical);
	QVERIFY(point_result.error.isEmpty());
	QCOMPARE(point_result.out.size(), std::size_t(1));
	auto const* mirrored_point = point_result.out.front()->asPoint();
	QVERIFY(mirrored_point);
	QCOMPARE(mirrored_point->getCoord(), MapCoord(10.0, 0.0));  // alone, the point is its own centre
	QVERIFY(std::abs(mirrored_point->getRotation() - (M_PI - 0.3)) < 1e-9);
	auto both_result = ops::mirror(map, { line, point }, vertical);
	QCOMPARE(both_result.out.size(), std::size_t(2));
	QCOMPARE(both_result.out.back()->asPoint()->getCoord(), MapCoord(0.0, 0.0));
	
	// A closed square maps onto itself and stays closed (setCoordinate() moves the close point along).
	auto* area_symbol_m = new AreaSymbol();
	map.addSymbol(area_symbol_m, 2);
	auto* square = new PathObject(area_symbol_m);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) })
		square->addCoordinate(coord);
	square->parts().front().setClosed(true);
	map.addObject(square);
	auto square_result = ops::mirror(map, { square }, vertical);
	QVERIFY(square_result.error.isEmpty());
	auto const* mirrored_square = square_result.out.front()->asPath();
	QVERIFY(mirrored_square->parts().front().isClosed());
	QCOMPARE(mirrored_square->getCoordinateCount(), MapCoordVector::size_type(5));
	for (auto const& coord : mirrored_square->getRawCoordinateVector())
		QVERIFY((coord.x() == 0.0 || coord.x() == 10.0) && (coord.y() == 0.0 || coord.y() == 10.0));
	QVERIFY(mirrored_square->getCoordinate(4).isPositionEqualTo(mirrored_square->getCoordinate(0)));
	QVERIFY(!mirrored_square->getCoordinate(1).isPositionEqualTo(mirrored_square->getCoordinate(0)));
	QVERIFY(!mirrored_square->getCoordinate(2).isPositionEqualTo(mirrored_square->getCoordinate(1)));
	
	// An empty input is an error.
	QVERIFY(!ops::mirror(map, {}, vertical).error.isEmpty());
	
	// Without the duplicate flag the output replaces the input and keeps its uuid.
	auto* part = map.getCurrentPart();
	map.addObjectToSelection(line, false);
	auto replace_result = ops::mirror(map, { line }, vertical);
	QCOMPARE(replace_result.consumed.size(), std::size_t(1));
	QVERIFY(ops::applyOp(map, std::move(replace_result)));
	QCOMPARE(part->getNumObjects(), 3);  // line, point, square
	auto const* replaced = map.findObject(uuid);
	QVERIFY(replaced && replaced != line);
	QCOMPARE(replaced->asPath()->getRawCoordinateVector().size(), std::size_t(3));
	QVERIFY(map.undoManager().undo());
	QCOMPARE(map.findObject(uuid), line);
	QCOMPARE(line->getRawCoordinateVector(), line_coords);
	
	// With the duplicate flag the input stays and the output carries derived_from.
	vertical.duplicate = true;
	auto duplicate_result = ops::mirror(map, { line }, vertical);
	QVERIFY(duplicate_result.consumed.empty());
	QVERIFY(ops::applyOp(map, std::move(duplicate_result)));
	QCOMPARE(part->getNumObjects(), 4);
	QCOMPARE(map.findObject(uuid), line);
	auto const* copy = part->getObject(3);
	QCOMPARE(copy->derivedFrom(), uuid);
	QVERIFY(map.selectedObjects() == (Map::ObjectSelection{ const_cast<Object*>(copy) }));
}


void OpsTest::transformTest()
{
	// the design notes, "Numeric transforms".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	point_symbol->setRotatable(true);
	map.addSymbol(point_symbol, 1);
	auto* line = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 4.0) });
	map.addObject(line);
	auto* point = new PointObject(point_symbol);
	point->setPosition(MapCoord(10.0, 0.0));
	point->setRotation(0.3);
	map.addObject(point);
	auto const path_coords = [](const ops::OpResult& result) { return result.out.front()->asPath()->getRawCoordinateVector(); };
	auto const point_coord = [](const ops::OpResult& result) { return result.out.back()->asPoint()->getCoord(); };
	
	// move-by
	ops::MoveParams move;
	move.dx = 1.0;
	move.dy = 2.0;
	auto moved = ops::moveBy(map, { line, point }, move);
	QVERIFY(moved.error.isEmpty());
	QCOMPARE(moved.out.size(), std::size_t(2));
	QCOMPARE(moved.consumed.size(), std::size_t(2));
	QCOMPARE(path_coords(moved), (MapCoordVector{ MapCoord(1.0, 2.0), MapCoord(11.0, 2.0), MapCoord(11.0, 6.0) }));
	QCOMPARE(point_coord(moved), MapCoord(11.0, 2.0));
	QCOMPARE(moved.out.back()->getRotation(), 0.3);
	
	// rotate-by, about the common centre (5, 2)
	ops::RotateParams rotate;
	rotate.angle = 90.0;
	auto rotated = ops::rotateBy(map, { line, point }, rotate);
	QVERIFY(rotated.error.isEmpty());
	QCOMPARE(path_coords(rotated), (MapCoordVector{ MapCoord(3.0, 7.0), MapCoord(3.0, -3.0), MapCoord(7.0, -3.0) }));
	QCOMPARE(point_coord(rotated), MapCoord(3.0, -3.0));
	QVERIFY(std::abs(rotated.out.back()->getRotation() - (0.3 + M_PI / 2)) < 1e-9);
	// ... and about each object's own centre: the point stays.
	rotate.each_object = true;
	auto rotated_each = ops::rotateBy(map, { line, point }, rotate);
	QCOMPARE(path_coords(rotated_each), path_coords(rotated));
	QCOMPARE(point_coord(rotated_each), MapCoord(10.0, 0.0));
	
	// stretch, about the common centre (5, 2)
	ops::StretchParams stretch;
	stretch.factor_x = 2.0;
	stretch.factor_y = 0.5;
	auto stretched = ops::stretch(map, { line, point }, stretch);
	QVERIFY(stretched.error.isEmpty());
	QCOMPARE(path_coords(stretched), (MapCoordVector{ MapCoord(-5.0, 1.0), MapCoord(15.0, 1.0), MapCoord(15.0, 3.0) }));
	QCOMPARE(point_coord(stretched), MapCoord(15.0, 1.0));
	QCOMPARE(stretched.out.back()->getRotation(), 0.3);
	
	// A closed square stretched by 2 and 1 about (5, 5) stays closed with its corners at x -5 and 15.
	auto* area_symbol_s = new AreaSymbol();
	map.addSymbol(area_symbol_s, 2);
	auto* square = new PathObject(area_symbol_s);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) })
		square->addCoordinate(coord);
	square->parts().front().setClosed(true);
	map.addObject(square);
	ops::StretchParams wide;
	wide.factor_x = 2.0;
	auto wide_result = ops::stretch(map, { square }, wide);
	QVERIFY(wide_result.error.isEmpty());
	auto const* wide_square = wide_result.out.front()->asPath();
	QVERIFY(wide_square->parts().front().isClosed());
	QCOMPARE(wide_square->getCoordinateCount(), MapCoordVector::size_type(5));
	for (auto const& coord : wide_square->getRawCoordinateVector())
		QVERIFY((coord.x() == -5.0 || coord.x() == 15.0) && (coord.y() == 0.0 || coord.y() == 10.0));
	QVERIFY(wide_square->getCoordinate(4).isPositionEqualTo(wide_square->getCoordinate(0)));
	QVERIFY(!wide_square->getCoordinate(1).isPositionEqualTo(wide_square->getCoordinate(0)));
	
	// Errors, and the duplicate flag.
	QVERIFY(!ops::moveBy(map, {}, move).error.isEmpty());
	QVERIFY(!ops::rotateBy(map, {}, rotate).error.isEmpty());
	stretch.factor_x = 0.0;
	QVERIFY(!ops::stretch(map, { line }, stretch).error.isEmpty());
	move.duplicate = true;
	auto duplicated = ops::moveBy(map, { line }, move);
	QCOMPARE(duplicated.out.size(), std::size_t(1));
	QVERIFY(duplicated.consumed.empty());
}


void OpsTest::alignTest()
{
	// the design notes, "Align and distribute".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 1);
	auto* a = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 4.0) });
	map.addObject(a);
	auto* b = new PointObject(point_symbol);
	b->setPosition(MapCoord(20.0, 10.0));
	map.addObject(b);
	auto* c = new PathObject(symbol, { MapCoord(30.0, 20.0), MapCoord(34.0, 22.0) });
	map.addObject(c);
	
	auto const check = [&map, a, b, c](ops::AlignParams::Edge edge, const MapCoord& b_expected, const MapCoordVector& c_expected) {
		ops::AlignParams params;
		params.edge = edge;
		auto result = ops::align(map, { a, b, c }, params);
		QVERIFY(result.error.isEmpty());
		QCOMPARE(result.out.size(), std::size_t(2));
		QCOMPARE(result.consumed, (std::vector<const Object*>{ b, c }));
		QCOMPARE(result.out.front()->asPoint()->getCoord(), b_expected);
		QCOMPARE(result.out.back()->asPath()->getRawCoordinateVector(), c_expected);
	};
	check(ops::AlignParams::Left, MapCoord(0.0, 10.0), { MapCoord(0.0, 20.0), MapCoord(4.0, 22.0) });
	check(ops::AlignParams::Right, MapCoord(10.0, 10.0), { MapCoord(6.0, 20.0), MapCoord(10.0, 22.0) });
	check(ops::AlignParams::HCentre, MapCoord(5.0, 10.0), { MapCoord(3.0, 20.0), MapCoord(7.0, 22.0) });
	check(ops::AlignParams::Top, MapCoord(20.0, 0.0), { MapCoord(30.0, 0.0), MapCoord(34.0, 2.0) });
	check(ops::AlignParams::Bottom, MapCoord(20.0, 4.0), { MapCoord(30.0, 2.0), MapCoord(34.0, 4.0) });
	check(ops::AlignParams::VCentre, MapCoord(20.0, 2.0), { MapCoord(30.0, 1.0), MapCoord(34.0, 3.0) });
	
	// Objects already in place are left alone; fewer than two objects is an error.
	auto* d = new PathObject(symbol, { MapCoord(0.0, 30.0), MapCoord(5.0, 30.0) });
	map.addObject(d);
	ops::AlignParams left;
	left.edge = ops::AlignParams::Left;
	auto in_place = ops::align(map, { a, d }, left);
	QVERIFY(in_place.error.isEmpty());
	QVERIFY(in_place.out.empty() && in_place.consumed.empty());
	QVERIFY(!ops::align(map, { a }, left).error.isEmpty());
	QVERIFY(!ops::align(map, {}, left).error.isEmpty());
	
	// Through applyOp, the moved object keeps its uuid and the first object stays.
	auto const a_uuid = a->uuid();
	auto const b_uuid = b->uuid();
	QVERIFY(ops::applyOp(map, ops::align(map, { a, b }, left)));
	QCOMPARE(map.findObject(a_uuid), a);
	auto const* moved_b = map.findObject(b_uuid);
	QVERIFY(moved_b && moved_b != b);
	QCOMPARE(moved_b->asPoint()->getCoord(), MapCoord(0.0, 10.0));
	QVERIFY(map.undoManager().undo());
	QCOMPARE(map.findObject(b_uuid), b);
	
	// The extent of a boxed text is its box.
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 2);
	auto* text = new TextObject(text_symbol);
	text->setBox(MapCoord(50.0, 50.0).nativeX(), MapCoord(50.0, 50.0).nativeY(), 20.0, 10.0);
	map.addObject(text);
	auto const box = ops::boundingRect({ text });
	QCOMPARE(box, QRectF(40.0, 45.0, 20.0, 10.0));
	QCOMPARE(ops::boundingRect({ a, b }), QRectF(0.0, 0.0, 20.0, 10.0));
	QVERIFY(ops::boundingRect({}).isNull());
	auto text_left = ops::align(map, { a, text }, left);
	QCOMPARE(text_left.out.size(), std::size_t(1));
	QCOMPARE(text_left.out.front()->asText()->getAnchorCoordF(), MapCoordF(10.0, 50.0));
}

void OpsTest::distributeTest()
{
	// the design notes, "Align and distribute".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 1);
	auto const addPoint = [&map, point_symbol](double x, double y) {
		auto* point = new PointObject(point_symbol);
		point->setPosition(MapCoord(x, y));
		map.addObject(point);
		return point;
	};
	auto const point_coord = [](const ops::OpResult& result, std::size_t i) { return result.out[i]->asPoint()->getCoord(); };
	
	// Three points, given out of order: only the middle one moves.
	auto* p40 = addPoint(40.0, 9.0);
	auto* p0 = addPoint(0.0, 0.0);
	auto* p10 = addPoint(10.0, 5.0);
	ops::DistributeParams horizontal;
	QCOMPARE(horizontal.direction, ops::DistributeParams::Horizontal);
	auto three = ops::distribute(map, { p40, p0, p10 }, horizontal);
	QVERIFY(three.error.isEmpty());
	QCOMPARE(three.out.size(), std::size_t(1));
	QCOMPARE(three.consumed, (std::vector<const Object*>{ p10 }));
	QCOMPARE(point_coord(three, 0), MapCoord(20.0, 5.0));
	
	// Four points: the middle two move to equal centre spacing.
	auto* q0 = addPoint(0.0, 0.0);
	auto* q1 = addPoint(1.0, 0.0);
	auto* q2 = addPoint(2.0, 0.0);
	auto* q30 = addPoint(30.0, 0.0);
	auto four = ops::distribute(map, { q0, q1, q2, q30 }, horizontal);
	QVERIFY(four.error.isEmpty());
	QCOMPARE(four.out.size(), std::size_t(2));
	QCOMPARE(four.consumed, (std::vector<const Object*>{ q1, q2 }));
	QCOMPARE(point_coord(four, 0), MapCoord(10.0, 0.0));
	QCOMPARE(point_coord(four, 1), MapCoord(20.0, 0.0));
	
	// A line moves by the centre of its extent.
	auto* line = new PathObject(symbol, { MapCoord(10.0, 0.0), MapCoord(14.0, 0.0) });
	map.addObject(line);
	auto with_line = ops::distribute(map, { p0, line, p40 }, horizontal);
	QCOMPARE(with_line.out.size(), std::size_t(1));
	QCOMPARE(with_line.out.front()->asPath()->getRawCoordinateVector(), (MapCoordVector{ MapCoord(18.0, 0.0), MapCoord(22.0, 0.0) }));
	
	// Vertically.
	auto* v0 = addPoint(3.0, 0.0);
	auto* v10 = addPoint(7.0, 10.0);
	auto* v40 = addPoint(1.0, 40.0);
	ops::DistributeParams vertical;
	vertical.direction = ops::DistributeParams::Vertical;
	auto down = ops::distribute(map, { v10, v40, v0 }, vertical);
	QCOMPARE(down.out.size(), std::size_t(1));
	QCOMPARE(down.consumed, (std::vector<const Object*>{ v10 }));
	QCOMPARE(point_coord(down, 0), MapCoord(7.0, 20.0));
	
	// Fewer than three objects is an error.
	QVERIFY(!ops::distribute(map, { p0, p40 }, horizontal).error.isEmpty());
	QVERIFY(!ops::distribute(map, {}, horizontal).error.isEmpty());
}


void OpsTest::reshapeTest()
{
	// the design notes, "Reshape".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 1);
	auto const near = [](const MapCoordVector& actual, const MapCoordVector& expected, double tolerance) {
		if (actual.size() != expected.size())
			return false;
		for (std::size_t i = 0; i < actual.size(); ++i)
		{
			if (actual[i].isCurveStart() != expected[i].isCurveStart()
			    || std::abs(actual[i].x() - expected[i].x()) > tolerance
			    || std::abs(actual[i].y() - expected[i].y()) > tolerance)
				return false;
		}
		return true;
	};
	auto const area = [](const MapCoordVector& coords) {
		auto sum = 0.0;
		for (std::size_t i = 0; i + 1 < coords.size(); ++i)
			sum += coords[i].x() * coords[i+1].y() - coords[i+1].x() * coords[i].y();
		return std::abs(sum) / 2;
	};
	auto const curve = [](double x, double y) { auto c = MapCoord(x, y); c.setCurveStart(true); return c; };
	
	// An open line: the stretch between the snapped ends is replaced.
	auto* line = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) });
	map.addObject(line);
	auto const uuid = line->uuid();
	ops::ReshapeParams params;
	params.stroke = { MapCoord(10.0, 0.1), MapCoord(15.0, 5.0), MapCoord(20.0, -0.1) };
	params.snap_distance = 1.0;
	auto const expected = MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(15.0, 5.0), MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) };
	auto result = ops::reshape(map, { line }, params);
	QVERIFY2(result.error.isEmpty(), result.error.toLatin1());
	QCOMPARE(result.out.size(), std::size_t(1));
	QCOMPARE(result.consumed, (std::vector<const Object*>{ line }));
	QCOMPARE(result.out.front()->uuid(), uuid);
	QCOMPARE(result.out.front()->asPath()->getRawCoordinateVector(), expected);
	// ... the reversed stroke gives the same.
	std::reverse(begin(params.stroke), end(params.stroke));
	auto reversed = ops::reshape(map, { line }, params);
	QVERIFY(reversed.error.isEmpty());
	QCOMPARE(reversed.out.front()->asPath()->getRawCoordinateVector(), expected);
	// ... and a curved stroke keeps its curve.
	params.stroke = { curve(10.0, 0.0), MapCoord(12.0, 3.0), MapCoord(18.0, 3.0), MapCoord(20.0, 0.0) };
	auto curved = ops::reshape(map, { line }, params);
	QVERIFY(curved.error.isEmpty());
	QVERIFY(near(curved.out.front()->asPath()->getRawCoordinateVector(), { MapCoord(0.0, 0.0), curve(10.0, 0.0), MapCoord(12.0, 3.0), MapCoord(18.0, 3.0), MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) }, 0.001));
	
	// A cut inside a curve keeps the curve's shape.
	auto* bent = new PathObject(symbol, { curve(0.0, 0.0), MapCoord(0.0, 10.0), MapCoord(20.0, 10.0), MapCoord(20.0, 0.0), MapCoord(40.0, 0.0) });
	map.addObject(bent);
	params.stroke = { MapCoord(10.0, 7.5), MapCoord(30.0, 0.0) };
	auto split = ops::reshape(map, { bent }, params);
	QVERIFY2(split.error.isEmpty(), split.error.toLatin1());
	QVERIFY(near(split.out.front()->asPath()->getRawCoordinateVector(), { curve(0.0, 0.0), MapCoord(0.0, 5.0), MapCoord(5.0, 7.5), MapCoord(10.0, 7.5), MapCoord(30.0, 0.0), MapCoord(40.0, 0.0) }, 0.01));
	
	// A closed path: the shorter arc is replaced, or the longer one on request.
	auto* square = new PathObject(area_symbol);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) })
		square->addCoordinate(coord);
	square->parts().front().setClosed(true);
	map.addObject(square);
	params.stroke = { MapCoord(5.0, 0.0), MapCoord(10.0, 5.0) };
	auto corner = ops::reshape(map, { square }, params);
	QVERIFY2(corner.error.isEmpty(), corner.error.toLatin1());
	auto const* corner_path = corner.out.front()->asPath();
	QVERIFY(corner_path->parts().front().isClosed());
	QCOMPARE(corner_path->getCoordinateCount(), MapCoordVector::size_type(6));
	QCOMPARE(area(corner_path->getRawCoordinateVector()), 87.5);
	params.other_arc = true;
	auto other = ops::reshape(map, { square }, params);
	QVERIFY(other.error.isEmpty());
	auto const* other_path = other.out.front()->asPath();
	QVERIFY(other_path->parts().front().isClosed());
	QCOMPARE(other_path->getCoordinateCount(), MapCoordVector::size_type(4));
	QCOMPARE(area(other_path->getRawCoordinateVector()), 12.5);
	params.other_arc = false;
	
	// Errors.
	params.stroke = { MapCoord(10.0, 5.0), MapCoord(20.0, 0.0) };
	QVERIFY(!ops::reshape(map, { line }, params).error.isEmpty());  // starts 5 mm away
	params.stroke = { MapCoord(10.0, 0.0) };
	QVERIFY(!ops::reshape(map, { line }, params).error.isEmpty());  // one coordinate
	params.stroke = { MapCoord(10.0, 0.0), MapCoord(20.0, 0.0) };
	QVERIFY(!ops::reshape(map, {}, params).error.isEmpty());
	QVERIFY(!ops::reshape(map, { line, bent }, params).error.isEmpty());
	auto* holed = new PathObject(area_symbol);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(40.0, 0.0), MapCoord(40.0, 40.0), MapCoord(0.0, 40.0) })
		holed->addCoordinate(coord);
	holed->parts().front().setClosed(true);
	holed->addCoordinate(MapCoord(10.0, 10.0), true);
	for (auto const& coord : { MapCoord(20.0, 10.0), MapCoord(20.0, 20.0), MapCoord(10.0, 20.0) })
		holed->addCoordinate(coord);
	holed->parts().back().setClosed(true);
	map.addObject(holed);
	QCOMPARE(holed->parts().size(), std::size_t(2));
	params.stroke = { MapCoord(5.0, 0.0), MapCoord(15.0, 10.0) };
	QVERIFY(!ops::reshape(map, { holed }, params).error.isEmpty());  // outer boundary to hole
	params.stroke = { MapCoord(15.0, 10.0), MapCoord(20.0, 15.0) };
	auto hole = ops::reshape(map, { holed }, params);
	QVERIFY2(hole.error.isEmpty(), hole.error.toLatin1());
	QCOMPARE(hole.out.front()->asPath()->parts().size(), std::size_t(2));
	QCOMPARE(hole.out.front()->asPath()->parts().front().size(), holed->parts().front().size());
	
	// Through applyOp, the output takes the input's place.
	params.stroke = { MapCoord(10.0, 0.0), MapCoord(15.0, 5.0), MapCoord(20.0, 0.0) };
	QVERIFY(ops::applyOp(map, ops::reshape(map, { line }, params)));
	auto const* replaced = map.findObject(uuid);
	QVERIFY(replaced && replaced != line);
	QCOMPARE(replaced->asPath()->getRawCoordinateVector(), expected);
	QVERIFY(map.undoManager().undo());
	QCOMPARE(map.findObject(uuid), line);
}


void OpsTest::offsetTest()
{
	// the design notes, "Parallel offset".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 1);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 2);
	auto const area = [](const MapCoordVector& coords, std::size_t first, std::size_t last) {
		auto sum = 0.0;
		for (auto i = first; i < last; ++i)
			sum += coords[i].x() * coords[i+1].y() - coords[i+1].x() * coords[i].y();
		return std::abs(sum) / 2;
	};
	auto const offsetOf = [&map](const Object* object, double distance, bool duplicate = false) {
		ops::OffsetParams params;
		params.distance = distance;
		params.duplicate = duplicate;
		return ops::offset(map, { object }, params);
	};
	
	// A straight line: left of eastward travel is up, towards smaller y.
	auto* straight = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.addObject(straight);
	auto const uuid = straight->uuid();
	auto up = offsetOf(straight, 1.0);
	QVERIFY2(up.error.isEmpty(), up.error.toLatin1());
	QCOMPARE(up.out.size(), std::size_t(1));
	QCOMPARE(up.consumed, (std::vector<const Object*>{ straight }));
	QCOMPARE(up.out.front()->uuid(), uuid);
	QCOMPARE(up.out.front()->asPath()->getRawCoordinateVector(), (MapCoordVector{ MapCoord(0.0, -1.0), MapCoord(10.0, -1.0) }));
	auto down = offsetOf(straight, -1.0);
	QCOMPARE(down.out.front()->asPath()->getRawCoordinateVector(), (MapCoordVector{ MapCoord(0.0, 1.0), MapCoord(10.0, 1.0) }));
	auto kept = offsetOf(straight, 1.0, true);
	QVERIFY(kept.consumed.empty());
	QCOMPARE(kept.out.size(), std::size_t(1));
	
	// A right turn: the inner side is trimmed, the outer side rounded.
	auto corner = MapCoord(10.0, 0.0);
	corner.setDashPoint(true);
	auto* bent = new PathObject(symbol, { MapCoord(0.0, 0.0), corner, MapCoord(10.0, 10.0) });
	map.addObject(bent);
	auto inner = offsetOf(bent, -1.0);
	QVERIFY2(inner.error.isEmpty(), inner.error.toLatin1());
	auto const& inner_coords = inner.out.front()->asPath()->getRawCoordinateVector();
	auto const inner_expected = MapCoordVector{ MapCoord(0.0, 1.0), MapCoord(9.0, 1.0), MapCoord(9.0, 10.0) };
	QCOMPARE(inner_coords.size(), inner_expected.size());
	for (std::size_t i = 0; i < inner_expected.size(); ++i)
		QVERIFY(inner_coords[i].isPositionEqualTo(inner_expected[i]));
	QVERIFY(inner_coords[1].isDashPoint());
	auto outer = offsetOf(bent, 1.0);
	QVERIFY2(outer.error.isEmpty(), outer.error.toLatin1());
	auto const& outer_coords = outer.out.front()->asPath()->getRawCoordinateVector();
	QVERIFY(outer_coords.size() >= 6);
	QCOMPARE(outer_coords[0], MapCoord(0.0, -1.0));
	QCOMPARE(outer_coords[1], MapCoord(10.0, -1.0));
	QCOMPARE(outer_coords[outer_coords.size() - 2], MapCoord(11.0, 0.0));
	QCOMPARE(outer_coords.back(), MapCoord(11.0, 10.0));
	for (std::size_t i = 1; i + 1 < outer_coords.size(); ++i)
		QVERIFY(std::abs(MapCoordF(outer_coords[i]).distanceTo(MapCoordF(10.0, 0.0)) - 1.0) < 0.011);
	QVERIFY(std::any_of(begin(outer_coords), end(outer_coords), [](const MapCoord& c) { return c.isDashPoint(); }));
	
	// A closed square, drawn clockwise on paper: -1 shrinks it, +1 grows it with round corners.
	auto const makeSquare = [area_symbol](double size, double origin) {
		auto* square = new PathObject(area_symbol);
		for (auto const& coord : { MapCoord(origin, origin), MapCoord(origin + size, origin), MapCoord(origin + size, origin + size), MapCoord(origin, origin + size) })
			square->addCoordinate(coord);
		square->parts().front().setClosed(true);
		return square;
	};
	auto* square = makeSquare(10.0, 0.0);
	map.addObject(square);
	auto shrunk = offsetOf(square, -1.0);
	QVERIFY2(shrunk.error.isEmpty(), shrunk.error.toLatin1());
	auto const* shrunk_path = shrunk.out.front()->asPath();
	QVERIFY(shrunk_path->parts().front().isClosed());
	QCOMPARE(shrunk_path->getRawCoordinateVector(), (MapCoordVector{ MapCoord(1.0, 1.0), MapCoord(9.0, 1.0), MapCoord(9.0, 9.0), MapCoord(1.0, 9.0), shrunk_path->getRawCoordinateVector().back() }));
	QCOMPARE(shrunk_path->getRawCoordinateVector().back().x(), 1.0);
	QCOMPARE(shrunk_path->getRawCoordinateVector().back().y(), 1.0);
	auto grown = offsetOf(square, 1.0);
	QVERIFY2(grown.error.isEmpty(), grown.error.toLatin1());
	auto const* grown_path = grown.out.front()->asPath();
	QVERIFY(grown_path->parts().front().isClosed());
	auto const grown_area = area(grown_path->getRawCoordinateVector(), 0, grown_path->getCoordinateCount() - 1);
	QVERIFY2(std::abs(grown_area - (140.0 + M_PI)) < 0.005 * (140.0 + M_PI), QByteArray::number(grown_area));
	
	// A hole drawn the same way as the outer boundary grows when the boundary shrinks.
	auto* holed = makeSquare(40.0, 0.0);
	holed->addCoordinate(MapCoord(10.0, 10.0), true);
	for (auto const& coord : { MapCoord(20.0, 10.0), MapCoord(20.0, 20.0), MapCoord(10.0, 20.0) })
		holed->addCoordinate(coord);
	holed->parts().back().setClosed(true);
	map.addObject(holed);
	auto uniform = offsetOf(holed, -1.0);
	QVERIFY2(uniform.error.isEmpty(), uniform.error.toLatin1());
	auto const* uniform_path = uniform.out.front()->asPath();
	QCOMPARE(uniform_path->parts().size(), std::size_t(2));
	auto const& uniform_coords = uniform_path->getRawCoordinateVector();
	auto const& outer_part = uniform_path->parts().front();
	QCOMPARE(outer_part.size(), VirtualPath::size_type(5));
	QCOMPARE(uniform_coords[outer_part.first_index], MapCoord(1.0, 1.0));
	QCOMPARE(uniform_coords[outer_part.first_index + 2], MapCoord(39.0, 39.0));
	auto const& hole_part = uniform_path->parts().back();
	QVERIFY(hole_part.isClosed());
	auto const hole_area = area(uniform_coords, hole_part.first_index, hole_part.last_index);
	QVERIFY2(std::abs(hole_area - (140.0 + M_PI)) < 0.005 * (140.0 + M_PI), QByteArray::number(hole_area));
	
	// A curved input gives an output with curves.
	auto curve_start = MapCoord(0.0, 0.0);
	curve_start.setCurveStart(true);
	auto* curved = new PathObject(symbol, { curve_start, MapCoord(0.0, 10.0), MapCoord(20.0, 10.0), MapCoord(20.0, 0.0) });
	map.addObject(curved);
	auto refit = offsetOf(curved, 1.0);
	QVERIFY2(refit.error.isEmpty(), refit.error.toLatin1());
	QVERIFY(ops::hasCurve(refit.out.front()->asPath()->getRawCoordinateVector()));
	QVERIFY(refit.out.front()->asPath()->getCoordinateCount() < 40);
	
	// Points are ignored; an empty input and a zero distance are errors.
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	ops::OffsetParams params;
	params.distance = 1.0;
	auto mixed = ops::offset(map, { point, straight }, params);
	QVERIFY(mixed.error.isEmpty());
	QCOMPARE(mixed.out.size(), std::size_t(1));
	QCOMPARE(mixed.consumed, (std::vector<const Object*>{ straight }));
	QVERIFY(!ops::offset(map, {}, params).error.isEmpty());
	QVERIFY(!ops::offset(map, { point }, params).error.isEmpty());
	params.distance = 0.0;
	QVERIFY(!ops::offset(map, { straight }, params).error.isEmpty());
	
	// The signed distance helper: left of eastward travel is negative y.
	QCOMPARE(ops::signedDistance(*straight, MapCoordF(5.0, -3.0)), 3.0);
	QCOMPARE(ops::signedDistance(*straight, MapCoordF(5.0, 2.0)), -2.0);
}


void OpsTest::vertexTypeTest()
{
	// the design notes, "Vertex types".
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 1);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 2);
	auto const dashed = [](const MapCoordVector& coords) {
		std::vector<bool> flags;
		for (auto const& coord : coords)
			flags.push_back(coord.isDashPoint());
		return flags;
	};
	auto const apply = [&map](const Object* object, bool dash, bool keep_ends) {
		ops::VertexTypeParams params;
		params.dash = dash;
		params.keep_ends = keep_ends;
		return ops::setVertexType(map, { object }, params);
	};
	
	auto curve_start = MapCoord(20.0, 0.0);
	curve_start.setCurveStart(true);
	auto* line = new PathObject(symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), curve_start, MapCoord(22.0, 3.0), MapCoord(28.0, 3.0), MapCoord(30.0, 0.0), MapCoord(40.0, 0.0) });
	map.addObject(line);
	auto const uuid = line->uuid();
	auto dash = apply(line, true, true);
	QVERIFY2(dash.error.isEmpty(), dash.error.toLatin1());
	QCOMPARE(dash.out.size(), std::size_t(1));
	QCOMPARE(dash.consumed, (std::vector<const Object*>{ line }));
	QCOMPARE(dash.out.front()->uuid(), uuid);
	auto const& dash_coords = dash.out.front()->asPath()->getRawCoordinateVector();
	QCOMPARE(dashed(dash_coords), (std::vector<bool>{ false, true, true, false, false, true, false }));
	QVERIFY(dash_coords[2].isCurveStart());
	auto normal = apply(dash.out.front().get(), false, false);
	QVERIFY(normal.error.isEmpty());
	QCOMPARE(dashed(normal.out.front()->asPath()->getRawCoordinateVector()), std::vector<bool>(7, false));
	auto unchanged = apply(line, false, false);
	QVERIFY(unchanged.error.isEmpty());
	QVERIFY(unchanged.out.empty() && unchanged.consumed.empty());
	
	auto* square = new PathObject(area_symbol);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) })
		square->addCoordinate(coord);
	square->parts().front().setClosed(true);
	map.addObject(square);
	QCOMPARE(square->getCoordinateCount(), MapCoordVector::size_type(5));
	auto inner = apply(square, true, true);
	QVERIFY2(inner.error.isEmpty(), inner.error.toLatin1());
	QCOMPARE(dashed(inner.out.front()->asPath()->getRawCoordinateVector()), (std::vector<bool>{ false, true, true, true, false }));
	QVERIFY(inner.out.front()->asPath()->parts().front().isClosed());
	auto all = apply(square, true, false);
	QCOMPARE(dashed(all.out.front()->asPath()->getRawCoordinateVector()), std::vector<bool>(5, true));
	
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	ops::VertexTypeParams params;
	params.dash = true;
	auto mixed = ops::setVertexType(map, { point, line }, params);
	QVERIFY(mixed.error.isEmpty());
	QCOMPARE(mixed.out.size(), std::size_t(1));
	QVERIFY(!ops::setVertexType(map, { point }, params).error.isEmpty());
	QVERIFY(!ops::setVertexType(map, {}, params).error.isEmpty());
}


void OpsTest::groupTest()
{
	// the design notes, "Groups".
	Map map;
	auto* symbol = new PointSymbol();
	map.addSymbol(symbol, 0);
	auto* first = new PointObject(symbol);
	map.addObject(first);
	auto* second = new PointObject(symbol);
	second->setGroup(5);
	map.addObject(second);
	ops::GroupParams params;
	params.group = 5;
	auto result = ops::setGroup(map, { first, second }, params);
	QVERIFY(result.error.isEmpty());
	QCOMPARE(result.out.size(), std::size_t(1));
	QCOMPARE(result.consumed, (std::vector<const Object*>{ first }));
	QCOMPARE(result.out.front()->group(), 5);
	QCOMPARE(result.out.front()->uuid(), first->uuid());
	QVERIFY(!ops::setGroup(map, {}, params).error.isEmpty());
	auto const uuid = first->uuid();
	QVERIFY(ops::applyOp(map, std::move(result)));
	QCOMPARE(map.findObject(uuid)->group(), 5);
	QVERIFY(map.undoManager().undo());
	QCOMPARE(map.findObject(uuid), first);
	QCOMPARE(first->group(), 0);
}


void OpsTest::replaceTextTest()
{
	// the design notes, "Find and replace text".
	Map map;
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 0);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 1);
	std::vector<const Object*> in;
	for (auto const* content : { "Hill 1", "hill top", "Hillside" })
	{
		auto* text = new TextObject(text_symbol);
		text->setText(QString::fromLatin1(content));
		map.addObject(text);
		in.push_back(text);
	}
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	in.push_back(point);
	auto const texts = [](const ops::OpResult& result) {
		QStringList texts;
		for (auto const& object : result.out)
			texts << object->asText()->getText();
		return texts;
	};
	
	ops::ReplaceTextParams params;
	params.find = QStringLiteral("hill");
	params.replacement = QStringLiteral("Knoll");
	auto all = ops::replaceText(map, in, params);
	QVERIFY(all.error.isEmpty());
	QCOMPARE(texts(all), (QStringList{ QStringLiteral("Knoll 1"), QStringLiteral("Knoll top"), QStringLiteral("Knollside") }));
	QCOMPARE(all.consumed.size(), std::size_t(3));
	QCOMPARE(all.out.front()->uuid(), in.front()->uuid());
	params.whole_word = true;
	QCOMPARE(texts(ops::replaceText(map, in, params)), (QStringList{ QStringLiteral("Knoll 1"), QStringLiteral("Knoll top") }));
	params.whole_word = false;
	params.case_sensitive = true;
	params.find = QStringLiteral("Hill");
	QCOMPARE(texts(ops::replaceText(map, in, params)), (QStringList{ QStringLiteral("Knoll 1"), QStringLiteral("Knollside") }));
	params.case_sensitive = false;
	params.find = QStringLiteral("hill");
	
	auto removed = ops::deleteMatchingText(map, in, params);
	QVERIFY(removed.error.isEmpty());
	QVERIFY(removed.out.empty());
	QCOMPARE(removed.consumed, (std::vector<const Object*>{ in[0], in[1], in[2] }));
	
	params.find.clear();
	QVERIFY(!ops::replaceText(map, in, params).error.isEmpty());
	QVERIFY(!ops::deleteMatchingText(map, in, params).error.isEmpty());
	QVERIFY(!ops::textMatches(QStringLiteral("Hill"), params));
}

void OpsTest::topologyTest()
{
	// the design notes, "Topology commands", Accept.
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* other_symbol = new LineSymbol();
	map.addSymbol(other_symbol, 1);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 2);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 3);
	auto const line = [&map](const Symbol* s, const MapCoord& a, const MapCoord& b) {
		auto* object = new PathObject(s, { a, b });
		map.addObject(object);
		return object;
	};
	auto const ring = [&map, area_symbol](std::initializer_list<MapCoord> coords, bool close) {
		auto* object = new PathObject(area_symbol);
		for (auto const& coord : coords)
			object->addCoordinate(coord);
		if (close)
			object->parts().front().setClosed(true);
		map.addObject(object);
		return object;
	};
	auto const positions = [](const Object* object) {
		std::vector<QPointF> out;
		for (auto const& coord : object->asPath()->getRawCoordinateVector())
			out.emplace_back(coord.x(), coord.y());
		return out;
	};
	auto const near = [](const QPointF& a, const QPointF& b) { return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6; };

	// Join.
	auto* a = line(symbol, MapCoord(0.0, 0.0), MapCoord(10.0, 0.0));
	auto* b = line(symbol, MapCoord(10.1, 0.0), MapCoord(20.0, 0.0));
	auto* c = line(symbol, MapCoord(30.0, 0.0), MapCoord(40.0, 0.0));
	auto* d = line(other_symbol, MapCoord(20.0, 0.1), MapCoord(20.0, 10.0));
	ops::JoinParams join;
	auto joined = ops::joinLines(map, { a, b, c, d }, join);
	QVERIFY2(joined.error.isEmpty(), joined.error.toLatin1());
	QCOMPARE(joined.out.size(), std::size_t(1));
	QCOMPARE(joined.consumed.size(), std::size_t(2));
	QVERIFY(std::find(joined.consumed.begin(), joined.consumed.end(), a) != joined.consumed.end());
	QVERIFY(std::find(joined.consumed.begin(), joined.consumed.end(), b) != joined.consumed.end());
	QCOMPARE(joined.out.front()->uuid(), a->uuid());
	auto const joined_positions = positions(joined.out.front().get());
	QCOMPARE(joined_positions.size(), std::size_t(3));
	QVERIFY(near(joined_positions[0], QPointF(0.0, 0.0)));
	QVERIFY(near(joined_positions[1], QPointF(10.05, 0.0)));
	QVERIFY(near(joined_positions[2], QPointF(20.0, 0.0)));
	join.tolerance = 0.05;
	auto const nothing = ops::joinLines(map, { a, b, c, d }, join);
	QVERIFY(nothing.error.isEmpty() && nothing.out.empty() && nothing.consumed.empty());
	QVERIFY(!ops::joinLines(map, { a }, join).error.isEmpty());

	// Close areas.
	auto* open_area = ring({ MapCoord(0.0, 50.0), MapCoord(10.0, 50.0), MapCoord(10.0, 60.0) }, false);
	auto* closed_area = ring({ MapCoord(20.0, 50.0), MapCoord(30.0, 50.0), MapCoord(30.0, 60.0) }, true);
	auto closed = ops::closeAreas(map, { open_area, closed_area, a }, ops::OpParams());
	QVERIFY(closed.error.isEmpty());
	QCOMPARE(closed.out.size(), std::size_t(1));
	QCOMPARE(closed.consumed, (std::vector<const Object*>{ open_area }));
	QVERIFY(closed.out.front()->asPath()->parts().front().isClosed());
	QCOMPARE(closed.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(4));

	// Areas anticlockwise.
	auto* clockwise = ring({ MapCoord(0.0, 100.0), MapCoord(10.0, 100.0), MapCoord(10.0, 110.0), MapCoord(0.0, 110.0) }, true);
	auto* anticlockwise = ring({ MapCoord(20.0, 100.0), MapCoord(20.0, 110.0), MapCoord(30.0, 110.0), MapCoord(30.0, 100.0) }, true);
	auto* holed = ring({ MapCoord(40.0, 100.0), MapCoord(40.0, 110.0), MapCoord(50.0, 110.0), MapCoord(50.0, 100.0) }, true);
	auto first = true;
	for (auto const& coord : { MapCoord(42.0, 102.0), MapCoord(42.0, 108.0), MapCoord(48.0, 108.0), MapCoord(48.0, 102.0) })
	{
		holed->addCoordinate(coord, first);
		first = false;
	}
	holed->parts().back().setClosed(true);
	auto const& cw_coords = clockwise->getRawCoordinateVector();
	QVERIFY(ops::signedAreaOnPaper(cw_coords, 0, cw_coords.size() - 1) < 0.0);
	auto ccw = ops::areasAnticlockwise(map, { clockwise, anticlockwise, holed }, ops::OpParams());
	QVERIFY(ccw.error.isEmpty());
	QCOMPARE(ccw.out.size(), std::size_t(2));
	QCOMPARE(ccw.consumed, (std::vector<const Object*>{ clockwise, holed }));
	for (auto const& output : ccw.out)
	{
		auto const* path = output->asPath();
		auto const& coords = path->getRawCoordinateVector();
		QVERIFY(ops::signedAreaOnPaper(coords, path->parts()[0].first_index, path->parts()[0].last_index) > 0.0);
		if (path->parts().size() > 1)
			QVERIFY(ops::signedAreaOnPaper(coords, path->parts()[1].first_index, path->parts()[1].last_index) < 0.0);
	}

	// Duplicate vertices.
	auto* stutter = new PathObject(symbol, { MapCoord(0.0, 150.0), MapCoord(5.0, 150.0), MapCoord(5.0, 150.0), MapCoord(10.0, 150.0) });
	map.addObject(stutter);
	auto cleaned = ops::removeDuplicateVertices(map, { stutter, a }, ops::OpParams());
	QVERIFY(cleaned.error.isEmpty());
	QCOMPARE(cleaned.out.size(), std::size_t(1));
	QCOMPARE(cleaned.consumed, (std::vector<const Object*>{ stutter }));
	QCOMPARE(cleaned.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(3));
	QCOMPARE(cleaned.out.front()->uuid(), stutter->uuid());

	// Overshoots and undershoots.
	auto* h = line(symbol, MapCoord(0.0, 200.0), MapCoord(10.0, 200.0));
	auto* v = line(symbol, MapCoord(5.0, 199.7), MapCoord(5.0, 210.0));
	auto* u = line(symbol, MapCoord(7.0, 200.3), MapCoord(7.0, 210.0));
	auto* w = line(symbol, MapCoord(3.0, 198.0), MapCoord(3.0, 210.0));
	ops::OvershootParams overshoot;
	overshoot.tolerance = 0.5;
	auto fixed = ops::fixOvershoots(map, { h, v, u, w }, overshoot);
	QVERIFY2(fixed.error.isEmpty(), fixed.error.toLatin1());
	QCOMPARE(fixed.out.size(), std::size_t(2));
	QCOMPARE(fixed.consumed, (std::vector<const Object*>{ v, u }));
	QVERIFY(near(positions(fixed.out[0].get()).front(), QPointF(5.0, 200.0)));
	QVERIFY(near(positions(fixed.out[0].get()).back(), QPointF(5.0, 210.0)));
	QVERIFY(near(positions(fixed.out[1].get()).front(), QPointF(7.0, 200.0)));
	QVERIFY(near(positions(fixed.out[1].get()).back(), QPointF(7.0, 210.0)));
	overshoot.remove_overshoots = false;
	auto extended_only = ops::fixOvershoots(map, { h, v, u, w }, overshoot);
	QCOMPARE(extended_only.consumed, (std::vector<const Object*>{ u }));

	// Intersections.
	auto* x1 = line(symbol, MapCoord(0.0, 300.0), MapCoord(10.0, 300.0));
	auto* x2 = line(symbol, MapCoord(5.0, 295.0), MapCoord(5.0, 305.0));
	ops::IntersectionParams params;
	auto inserted = ops::intersections(map, { x1, x2 }, params);
	QVERIFY2(inserted.error.isEmpty(), inserted.error.toLatin1());
	QCOMPARE(inserted.out.size(), std::size_t(2));
	QCOMPARE(inserted.consumed, (std::vector<const Object*>{ x1, x2 }));
	for (auto const& output : inserted.out)
	{
		auto const p = positions(output.get());
		QCOMPARE(p.size(), std::size_t(3));
		QVERIFY(near(p[1], QPointF(5.0, 300.0)));
		QVERIFY(!output->asPath()->getCoordinate(1).isDashPoint());
	}
	params.dash = true;
	auto dashed = ops::intersections(map, { x1, x2 }, params);
	QVERIFY(dashed.out.front()->asPath()->getCoordinate(1).isDashPoint());
	params.dash = false;
	params.action = ops::IntersectionParams::Cut;
	auto cut = ops::intersections(map, { x1, x2 }, params);
	QVERIFY2(cut.error.isEmpty(), cut.error.toLatin1());
	QCOMPARE(cut.out.size(), std::size_t(4));
	QCOMPARE(cut.consumed, (std::vector<const Object*>{ x1, x2 }));
	QCOMPARE(cut.out[0]->uuid(), x1->uuid());
	QCOMPARE(cut.out[1]->derivedFrom(), x1->uuid());
	QCOMPARE(cut.out[2]->uuid(), x2->uuid());
	QVERIFY(near(positions(cut.out[0].get()).back(), QPointF(5.0, 300.0)));
	QVERIFY(near(positions(cut.out[1].get()).front(), QPointF(5.0, 300.0)));
	params.action = ops::IntersectionParams::AddPoint;
	QVERIFY(!ops::intersections(map, { x1, x2 }, params).error.isEmpty());
	params.point_symbol = point_symbol;
	auto points = ops::intersections(map, { x1, x2 }, params);
	QVERIFY2(points.error.isEmpty(), points.error.toLatin1());
	QCOMPARE(points.out.size(), std::size_t(1));
	QVERIFY(points.consumed.empty());
	QCOMPARE(points.out.front()->getType(), Object::Point);
	QVERIFY(near(QPointF(points.out.front()->asPoint()->getCoordF()), QPointF(5.0, 300.0)));
	auto* t = line(symbol, MapCoord(8.0, 300.0), MapCoord(8.0, 305.0));
	params.action = ops::IntersectionParams::InsertVertex;
	auto touched = ops::intersections(map, { x1, t }, params);
	QCOMPARE(touched.consumed, (std::vector<const Object*>{ x1 }));
	QCOMPARE(touched.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(3));
	params.include_touching = false;
	auto untouched = ops::intersections(map, { x1, t }, params);
	QVERIFY(untouched.error.isEmpty() && untouched.out.empty());
}


void OpsTest::mergeContoursTest()
{
	// the design notes, "Merging contour lines", Accept (1) and (2).
	Map map;
	auto* contour = new LineSymbol();
	map.addSymbol(contour, 0);
	auto* other = new LineSymbol();
	map.addSymbol(other, 1);
	auto const line = [&map](const Symbol* s, const MapCoord& a, const MapCoord& b, const char* ele) {
		auto* object = new PathObject(s, { a, b });
		if (ele)
			object->setTag(QStringLiteral("ele"), QString::fromLatin1(ele));
		map.addObject(object);
		return object;
	};
	auto* a = line(contour, MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), "410");
	auto* b = line(contour, MapCoord(10.1, 0.0), MapCoord(20.0, 0.0), "410");
	auto* c = line(contour, MapCoord(20.0, 0.0), MapCoord(30.0, 5.0), "415");
	auto* d = line(contour, MapCoord(0.0, 20.0), MapCoord(10.0, 20.0), nullptr);
	auto* e = line(contour, MapCoord(10.0, 20.0), MapCoord(20.0, 20.0), nullptr);
	auto* f = line(other, MapCoord(30.1, 5.0), MapCoord(40.0, 5.0), "415");
	ops::JoinParams params;
	auto const merged = ops::mergeContourLines(map, { a, b, c, d, e, f }, params);
	QVERIFY2(merged.error.isEmpty(), qPrintable(merged.error));
	QCOMPARE(merged.out.size(), std::size_t(2));
	QCOMPARE(merged.consumed.size(), std::size_t(4));
	for (auto const* consumed : { a, b, d, e })
		QVERIFY(std::find(merged.consumed.begin(), merged.consumed.end(), consumed) != merged.consumed.end());
	for (auto const& out : merged.out)
		QCOMPARE(int(out->asPath()->getRawCoordinateVector().size()), 3);
	params.tolerance = 0.05;
	auto const strict = ops::mergeContourLines(map, { a, b, c }, params);
	QVERIFY(strict.error.isEmpty() && strict.out.empty() && strict.consumed.empty());
}


void OpsTest::selectionQueriesTest()
{
	// the design notes, "Selection queries", Accept (1) to (5).
	Map map;
	auto* line_symbol = new LineSymbol();
	map.addSymbol(line_symbol, 0);
	auto* other_line = new LineSymbol();
	map.addSymbol(other_line, 1);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 2);
	auto* point_symbol = new PointSymbol();
	point_symbol->setRotatable(true);
	map.addSymbol(point_symbol, 3);
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 4);
	auto const path = [&map](const Symbol* s, MapCoordVector coords) {
		auto* object = new PathObject(s, coords);
		map.addObject(object);
		return object;
	};
	auto const contains = [](const std::vector<Object*>& found, const Object* object) {
		return std::find(found.begin(), found.end(), object) != found.end();
	};
	auto const& part = *map.getCurrentPart();
	
	// Duplicates.
	auto* a = path(line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	auto* b = path(line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	auto* c = path(line_symbol, { MapCoord(10.0, 0.0), MapCoord(0.0, 0.0) });
	auto* d = path(other_line, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	auto* e = path(line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.002, 0.0) });
	auto* p1 = new PointObject(point_symbol);
	p1->setPosition(MapCoord(5.0, 5.0));
	map.addObject(p1);
	auto* p2 = new PointObject(point_symbol);
	p2->setPosition(MapCoord(5.0, 5.0));
	map.addObject(p2);
	auto duplicates = findDuplicates(part);
	QCOMPARE(int(duplicates.size()), 3);
	QVERIFY(contains(duplicates, b) && contains(duplicates, c) && contains(duplicates, p2));
	QVERIFY(!contains(duplicates, a) && !contains(duplicates, d) && !contains(duplicates, e) && !contains(duplicates, p1));
	
	// Self-intersection.
	auto* eight = path(line_symbol, { MapCoord(100.0, 0.0), MapCoord(110.0, 10.0), MapCoord(110.0, 0.0), MapCoord(100.0, 10.0) });
	auto* square = path(area_symbol, { MapCoord(200.0, 0.0), MapCoord(210.0, 0.0), MapCoord(210.0, 10.0), MapCoord(200.0, 10.0) });
	square->closeAllParts();
	auto* repeated = path(line_symbol, { MapCoord(300.0, 0.0), MapCoord(305.0, 0.0), MapCoord(305.0, 0.0), MapCoord(310.0, 0.0) });
	auto* holed = path(area_symbol, { MapCoord(400.0, 0.0), MapCoord(420.0, 0.0), MapCoord(420.0, 20.0), MapCoord(400.0, 20.0) });
	holed->closeAllParts();
	holed->addCoordinate(MapCoord(415.0, 5.0), true);
	holed->addCoordinate(MapCoord(425.0, 5.0));
	holed->addCoordinate(MapCoord(425.0, 15.0));
	holed->addCoordinate(MapCoord(415.0, 15.0));
	holed->closeAllParts();
	auto const crossing = findSelfIntersecting(part);
	QVERIFY(contains(crossing, eight));
	QVERIFY(contains(crossing, holed));
	QVERIFY(!contains(crossing, square) && !contains(crossing, repeated) && !contains(crossing, a));
	
	// Invalid geometry.
	auto* single = path(line_symbol, { MapCoord(500.0, 0.0) });
	auto* equal = path(line_symbol, { MapCoord(510.0, 0.0), MapCoord(510.0, 0.0) });
	auto* thin = path(area_symbol, { MapCoord(520.0, 0.0), MapCoord(530.0, 0.0) });
	MapCoord curve_start(545.0, 0.0);
	curve_start.setCurveStart(true);
	auto* broken = path(line_symbol, { MapCoord(540.0, 0.0), curve_start, MapCoord(550.0, 0.0) });
	auto* empty_text = new TextObject(text_symbol);
	empty_text->setAnchorPosition(MapCoord(560.0, 0.0));
	map.addObject(empty_text);
	auto const invalid = findInvalid(part);
	for (auto const* object : std::initializer_list<const Object*>{ single, equal, thin, broken, empty_text })
		QVERIFY(contains(invalid, object));
	QVERIFY(!contains(invalid, a) && !contains(invalid, square));
	
	// Dates, on a part of their own.
	Map dated;
	auto* dated_symbol = new LineSymbol();
	dated.addSymbol(dated_symbol, 0);
	std::vector<PathObject*> stamped;
	qint64 const created[] = { 1000, 2000, 3000 };
	qint64 const modified[] = { 5000, 4000, 6000 };
	for (int k = 0; k < 3; ++k)
	{
		auto* object = new PathObject(dated_symbol, MapCoordVector{ MapCoord(double(k), 0.0), MapCoord(double(k), 10.0) });
		object->setIdentity(QUuid::createUuid(), created[k], modified[k]);
		dated.addObject(object);
		stamped.push_back(object);
	}
	auto const by_created = findByDate(*dated.getCurrentPart(), DateKind::Created, 1500, 3000);
	QCOMPARE(int(by_created.size()), 2);
	QVERIFY(contains(by_created, stamped[1]) && contains(by_created, stamped[2]));
	auto const by_modified = findByDate(*dated.getCurrentPart(), DateKind::Modified, 0, 4500);
	QCOMPARE(int(by_modified.size()), 1);
	QVERIFY(contains(by_modified, stamped[1]));
	
	// Properties, on a part of their own.
	Map measured;
	auto* m_line = new LineSymbol();
	measured.addSymbol(m_line, 0);
	auto* m_line2 = new LineSymbol();
	measured.addSymbol(m_line2, 1);
	auto* m_area = new AreaSymbol();
	measured.addSymbol(m_area, 2);
	auto* m_point = new PointSymbol();
	m_point->setRotatable(true);
	measured.addSymbol(m_point, 3);
	auto* m_text = new TextSymbol();
	measured.addSymbol(m_text, 4);
	auto* short_line = new PathObject(m_line, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	measured.addObject(short_line);
	auto* long_line = new PathObject(m_line2, MapCoordVector{ MapCoord(0.0, 10.0), MapCoord(20.0, 10.0) });
	measured.addObject(long_line);
	auto* big = new PathObject(m_area, MapCoordVector{ MapCoord(0.0, 20.0), MapCoord(10.0, 20.0), MapCoord(10.0, 30.0), MapCoord(0.0, 30.0) });
	big->closeAllParts();
	measured.addObject(big);
	auto* small = new PathObject(m_area, MapCoordVector{ MapCoord(20.0, 20.0), MapCoord(25.0, 20.0), MapCoord(25.0, 25.0), MapCoord(20.0, 25.0) });
	small->closeAllParts();
	measured.addObject(small);
	auto* turned = new PointObject(m_point);
	turned->setRotation(0.5);
	measured.addObject(turned);
	auto* upright = new PointObject(m_point);
	measured.addObject(upright);
	auto* bach = new TextObject(m_text);
	bach->setText(QStringLiteral("Bach"));
	measured.addObject(bach);
	auto* brook = new TextObject(m_text);
	brook->setText(QStringLiteral("Brook"));
	measured.addObject(brook);
	auto const& mpart = *measured.getCurrentPart();
	auto const query = [&mpart](PropertyQuery::Property property, PropertyQuery::Compare compare, double value, std::vector<const Symbol*> symbols = {}) {
		PropertyQuery q;
		q.property = property;
		q.compare = compare;
		q.value = value;
		q.symbols = std::move(symbols);
		return findByProperty(mpart, q);
	};
	auto found = query(PropertyQuery::Length, PropertyQuery::Greater, 15.0);
	QVERIFY(contains(found, long_line) && !contains(found, short_line));
	found = query(PropertyQuery::Vertices, PropertyQuery::Equal, 2.0);
	QVERIFY(contains(found, long_line) && contains(found, short_line));
	found = query(PropertyQuery::Area, PropertyQuery::GreaterEqual, 50.0);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(contains(found, big));
	found = query(PropertyQuery::Area, PropertyQuery::Less, 50.0);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(contains(found, small));
	found = query(PropertyQuery::Rotation, PropertyQuery::NotEqual, 0.0);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(contains(found, turned));
	found = query(PropertyQuery::TextLength, PropertyQuery::Less, 5.0);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(contains(found, bach));
	found = query(PropertyQuery::Length, PropertyQuery::Greater, 0.0, { m_line2 });
	QCOMPARE(int(found.size()), 1);
	QVERIFY(contains(found, long_line));
}


void OpsTest::interpolateTest()
{
	// the design notes, "Interpolate", Accept (1) to (5).
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* other = new LineSymbol();
	map.addSymbol(other, 1);
	auto* area = new AreaSymbol();
	map.addSymbol(area, 2);
	auto const path = [&map](const Symbol* s, MapCoordVector coords, bool closed = false) {
		auto* object = new PathObject(s, coords);
		if (closed)
			object->closeAllParts();
		map.addObject(object);
		return object;
	};
	auto const coords = [](const Object* object) { return object->getRawCoordinateVector(); };
	auto const near = [](const MapCoord& c, double x, double y, double tolerance) {
		return std::abs(c.x() - x) <= tolerance && std::abs(c.y() - y) <= tolerance;
	};
	
	auto* a = path(symbol, { MapCoord(0.0, 0.0), MapCoord(100.0, 0.0) });
	auto* b = path(symbol, { MapCoord(0.0, 10.0), MapCoord(100.0, 10.0) });
	ops::InterpolateParams params;
	auto result = ops::interpolate(map, { a, b }, params);
	QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
	QCOMPARE(result.out.size(), std::size_t(1));
	QVERIFY(result.consumed.empty());
	QCOMPARE(result.out.front()->getSymbol(), static_cast<const Symbol*>(symbol));
	auto c = coords(result.out.front().get());
	QCOMPARE(int(c.size()), 2);
	QVERIFY(near(c.front(), 0.0, 5.0, 0.01) && near(c.back(), 100.0, 5.0, 0.01));
	auto* reversed = path(symbol, { MapCoord(100.0, 10.0), MapCoord(0.0, 10.0) });
	c = coords(ops::interpolate(map, { a, reversed }, params).out.front().get());
	QVERIFY(near(c.front(), 0.0, 5.0, 0.01) && near(c.back(), 100.0, 5.0, 0.01));
	
	params.count = 3;
	result = ops::interpolate(map, { a, b }, params);
	QCOMPARE(result.out.size(), std::size_t(3));
	double const expected[] = { 2.5, 5.0, 7.5 };
	for (int k = 0; k < 3; ++k)
	{
		c = coords(result.out[std::size_t(k)].get());
		QVERIFY(near(c.front(), 0.0, expected[k], 0.01) && near(c.back(), 100.0, expected[k], 0.01));
	}
	
	params.count = 1;
	auto* bent = path(symbol, { MapCoord(0.0, 0.0), MapCoord(50.0, 0.0), MapCoord(100.0, 0.0) });
	auto* far = path(symbol, { MapCoord(0.0, 20.0), MapCoord(100.0, 20.0) });
	c = coords(ops::interpolate(map, { bent, far }, params).out.front().get());
	QVERIFY(near(c.front(), 0.0, 10.0, 0.01) && near(c.back(), 100.0, 10.0, 0.01));
	
	auto* outer = path(area, { MapCoord(0.0, 0.0), MapCoord(100.0, 0.0), MapCoord(100.0, 100.0), MapCoord(0.0, 100.0) }, true);
	MapCoordVector const inner_variants[] = {
	    { MapCoord(25.0, 25.0), MapCoord(75.0, 25.0), MapCoord(75.0, 75.0), MapCoord(25.0, 75.0) },
	    { MapCoord(75.0, 75.0), MapCoord(25.0, 75.0), MapCoord(25.0, 25.0), MapCoord(75.0, 25.0) },
	    { MapCoord(25.0, 25.0), MapCoord(25.0, 75.0), MapCoord(75.0, 75.0), MapCoord(75.0, 25.0) } };
	for (auto const& variant : inner_variants)
	{
		auto* inner = path(area, variant, true);
		auto const ring = ops::interpolate(map, { outer, inner }, params);
		QVERIFY2(ring.error.isEmpty(), qPrintable(ring.error));
		QCOMPARE(ring.out.size(), std::size_t(1));
		auto const* out = ring.out.front()->asPath();
		QVERIFY(out->parts().front().isClosed());
		for (auto const& coord : out->getRawCoordinateVector())
		{
			auto const on_edge = std::abs(coord.x() - 12.5) <= 0.5 || std::abs(coord.x() - 87.5) <= 0.5
			                     || std::abs(coord.y() - 12.5) <= 0.5 || std::abs(coord.y() - 87.5) <= 0.5;
			QVERIFY2(on_edge && coord.x() > 12.0 && coord.x() < 88.0 && coord.y() > 12.0 && coord.y() < 88.0,
			         qPrintable(QStringLiteral("%1 %2").arg(coord.x()).arg(coord.y())));
		}
	}
	
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 3);
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	QVERIFY(!ops::interpolate(map, { a }, params).error.isEmpty());
	QVERIFY(!ops::interpolate(map, { a, b, far }, params).error.isEmpty());
	QVERIFY(!ops::interpolate(map, { a, point }, params).error.isEmpty());
	QVERIFY(!ops::interpolate(map, { a, outer }, params).error.isEmpty());
	params.symbol = other;
	QCOMPARE(ops::interpolate(map, { a, b }, params).out.front()->getSymbol(), static_cast<const Symbol*>(other));
}


void OpsTest::objectInformationTest()
{
	// the design notes, "Object information", Accept (1) to (5).
	Map map;
	map.setScaleDenominator(10000);
	auto* brown = new MapColor(QStringLiteral("Brown"), 0);
	map.addColor(brown, 0);
	auto* blue = new MapColor(QStringLiteral("Blue"), 1);
	map.addColor(blue, 1);
	auto* contour = new LineSymbol();
	contour->setNumberComponent(0, 101);
	contour->setNumberComponent(1, 1);
	contour->setName(QStringLiteral("Contour"));
	contour->setColor(brown);
	contour->setLineWidth(0.14);
	map.addSymbol(contour, 0);
	auto* area = new AreaSymbol();
	area->setColor(blue);
	map.addSymbol(area, 1);
	map.getPart(0)->setName(QStringLiteral("Map"));
	
	auto* line = new PathObject(contour, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 5.0) });
	line->setIdentity(QUuid::createUuid(), 1000, 2000);
	line->setTag(QStringLiteral("ele"), QStringLiteral("410"));
	map.addObject(line);
	map.setObjectGroupName(3, QStringLiteral("Hill"));
	line->setGroup(3);
	auto info = objectInformation(map, *line);
	QCOMPARE(info.uuid, line->uuid().toString(QUuid::WithoutBraces));
	QCOMPARE(info.kind, QStringLiteral("Line"));
	QCOMPARE(info.symbol, QStringLiteral("101.1 Contour"));
	QCOMPARE(info.part, QStringLiteral("Map"));
	QCOMPARE(info.index, 1);
	QCOMPARE(info.group, QStringLiteral("Hill"));
	QCOMPARE(info.created, QStringLiteral("1970-01-01T00:16:40Z"));
	QCOMPARE(int(info.vertices.size()), 3);
	QVERIFY(std::abs(info.length_mm - 15.0) < 1e-6);
	QVERIFY(std::abs(info.length_m - 150.0) < 1e-6);
	QVERIFY(std::isnan(info.area_mm2));
	QCOMPARE(info.colours, QStringList{ QStringLiteral("Brown") });
	QCOMPARE(info.tags, QStringList{ QStringLiteral("ele=410") });
	
	auto* square = new PathObject(area, MapCoordVector{ MapCoord(20.0, 0.0), MapCoord(30.0, 0.0), MapCoord(30.0, 10.0), MapCoord(20.0, 10.0) });
	square->closeAllParts();
	square->addCoordinate(MapCoord(24.0, 4.0), true);
	square->addCoordinate(MapCoord(26.0, 4.0));
	square->addCoordinate(MapCoord(26.0, 6.0));
	square->addCoordinate(MapCoord(24.0, 6.0));
	square->closeAllParts();
	map.addObject(square);
	info = objectInformation(map, *square);
	QCOMPARE(info.kind, QStringLiteral("Area"));
	QVERIFY2(std::abs(info.area_mm2 - 96.0) < 1e-6, qPrintable(QString::number(info.area_mm2)));
	QVERIFY(std::abs(info.area_m2 - 9600.0) < 1e-3);
	auto const has_flag = [&info](const char* flag) {
		return std::any_of(info.vertices.begin(), info.vertices.end(), [flag](const ObjectInformation::Vertex& v) { return v.flags.contains(QLatin1String(flag)); });
	};
	QVERIFY(has_flag("close") && has_flag("end of part"));
	
	MapCoord curve(40.0, 0.0);
	curve.setCurveStart(true);
	MapCoord dash(55.0, 0.0);
	dash.setDashPoint(true);
	auto* curved = new PathObject(contour, MapCoordVector{ curve, MapCoord(45.0, 5.0), MapCoord(50.0, 5.0), dash, MapCoord(60.0, 0.0) });
	map.addObject(curved);
	info = objectInformation(map, *curved);
	QVERIFY(has_flag("curve start") && has_flag("dash"));
	
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 2);
	auto* text = new TextObject(text_symbol);
	text->setText(QStringLiteral("Bach"));
	map.addObject(text);
	info = objectInformation(map, *text);
	QCOMPARE(info.kind, QStringLiteral("Text"));
	QCOMPARE(info.text, QStringLiteral("Bach"));
	auto* point_symbol = new PointSymbol();
	point_symbol->setRotatable(true);
	map.addSymbol(point_symbol, 3);
	auto* point = new PointObject(point_symbol);
	point->setRotation(0.5);
	map.addObject(point);
	info = objectInformation(map, *point);
	QCOMPARE(info.kind, QStringLiteral("Point"));
	QVERIFY(std::abs(info.rotation_deg - 28.6) < 0.1);
	
	auto const report = objectInformationText(objectInformation(map, *line));
	QVERIFY(report.contains(line->uuid().toString(QUuid::WithoutBraces)));
	QVERIFY(report.contains(QLatin1String("ele=410")));
	QVERIFY(report.contains(QLatin1String("10.000")));
	QCOMPARE(report.count(QLatin1String("\n  ")), 3);
}


void OpsTest::symbolToolsTest()
{
	// the design notes, "Selecting symbols, replacing fonts and colours, renumbering", Accept (1) to (4).
	Map map;
	auto* brown = new MapColor(QStringLiteral("Brown"), 0);
	map.addColor(brown, 0);
	auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
	map.addColor(yellow, 1);
	auto* black = new MapColor(QStringLiteral("Black"), 2);
	map.addColor(black, 2);
	auto const numbered = [](Symbol* symbol, int number) { symbol->setNumberComponent(0, number); symbol->setNumberComponent(1, -1); return symbol; };
	auto* contour = new LineSymbol();
	contour->setColor(brown);
	contour->setLineWidth(0.14);
	map.addSymbol(numbered(contour, 101), 0);
	auto* field = new AreaSymbol();
	field->setColor(yellow);
	map.addSymbol(numbered(field, 401), 1);
	auto* arial = new TextSymbol();
	arial->setFontFamily(QStringLiteral("Arial"));
	map.addSymbol(numbered(arial, 900), 2);
	auto* verdana = new TextSymbol();
	verdana->setFontFamily(QStringLiteral("Verdana"));
	map.addSymbol(numbered(verdana, 901), 3);
	auto* boulder = new PointSymbol();
	boulder->setInnerRadius(200);
	boulder->setInnerColor(black);
	boulder->setHidden(true);
	map.addSymbol(numbered(boulder, 501), 4);
	auto* index = new LineSymbol();
	index->setColor(brown);
	index->setLineWidth(0.25);
	index->setProtected(true);
	map.addSymbol(numbered(index, 102), 5);
	using List = std::vector<const Symbol*>;
	
	QVERIFY(symbolsUsingColor(map, brown) == (List{ contour, index }));
	QVERIFY(symbolsUsingFont(map, QStringLiteral("Arial")) == (List{ arial }));
	QVERIFY(symbolsOfType(map, Symbol::Text) == (List{ arial, verdana }));
	QVERIFY(symbolsWithStatus(map, SymbolStatus::Hidden) == (List{ boulder }));
	QVERIFY(symbolsWithStatus(map, SymbolStatus::Protected) == (List{ index }));
	QVERIFY(symbolsWithStatus(map, SymbolStatus::Normal) == (List{ contour, field, arial, verdana }));
	QVERIFY(symbolsMatchingNumber(map, QStringLiteral("10*")) == (List{ contour, index }));
	QVERIFY(symbolsMatchingNumber(map, QStringLiteral("9??")) == (List{ arial, verdana }));
	QCOMPARE(fontFamilies(map), (QStringList{ QStringLiteral("Arial"), QStringLiteral("Verdana") }));
	
	QCOMPARE(replaceFont(map, QStringLiteral("Arial"), QStringLiteral("Verdana")), 1);
	QVERIFY(symbolsUsingFont(map, QStringLiteral("Arial")).empty());
	QCOMPARE(replaceFont(map, QStringLiteral("Verdana"), QStringLiteral("Arial"), { verdana }), 1);
	QCOMPARE(arial->getFontFamily(), QStringLiteral("Verdana"));
	QCOMPARE(verdana->getFontFamily(), QStringLiteral("Arial"));
	
	QCOMPARE(replaceColor(map, brown, black, { contour }), 1);
	QVERIFY(contour->containsColor(black) && !contour->containsColor(brown));
	QVERIFY(index->containsColor(brown));
	QCOMPARE(replaceColor(map, brown, black), 1);
	QVERIFY(symbolsUsingColor(map, brown).empty());
	
	map.setFavouriteSymbols({ QStringLiteral("900"), QStringLiteral("101") });
	StatusSet status_set;
	status_set.name = QStringLiteral("field");
	status_set.hidden = QStringList{ QStringLiteral("900") };
	map.setStatusSets({ status_set });
	SymbolGroup group;
	group.name = QStringLiteral("Lines");
	group.numbers = QStringList{ QStringLiteral("901"), QStringLiteral("102") };
	map.setSymbolTree({ group });
	QCOMPARE(renumberSymbols(map, 200, 10, { arial, verdana }), 2);
	QCOMPARE(arial->getNumberAsString(), QStringLiteral("200"));
	// Accept (6): the favourites, the status set and the group follow.
	QCOMPARE(map.favouriteSymbols(), (QStringList{ QStringLiteral("200"), QStringLiteral("101") }));
	QCOMPARE(map.statusSets().front().hidden, QStringList{ QStringLiteral("200") });
	QCOMPARE(map.symbolTree().front().numbers, (QStringList{ QStringLiteral("210"), QStringLiteral("102") }));
	QCOMPARE(verdana->getNumberAsString(), QStringLiteral("210"));
	QCOMPARE(contour->getNumberAsString(), QStringLiteral("101"));
	QCOMPARE(renumberSymbols(map, 200, 10), 6);
	QStringList numbers;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		numbers << map.getSymbol(i)->getNumberAsString();
	QCOMPARE(numbers, (QStringList{ QStringLiteral("200"), QStringLiteral("210"), QStringLiteral("220"), QStringLiteral("230"), QStringLiteral("240"), QStringLiteral("250") }));
}


void OpsTest::statusSetsTest()
{
	// the design notes, "Status sets", Accept (1) and (2).
	Map map;
	auto const numbered = [&map](Symbol* symbol, int number) {
		symbol->setNumberComponent(0, number);
		symbol->setNumberComponent(1, -1);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	};
	auto* contour = numbered(new LineSymbol(), 101);
	auto* field = numbered(new AreaSymbol(), 401);
	auto* boulder = numbered(new PointSymbol(), 501);
	contour->setHidden(true);
	boulder->setProtected(true);
	auto const set = currentStatusSet(map, QStringLiteral("field"));
	QCOMPARE(set.hidden, QStringList{ QStringLiteral("101") });
	QCOMPARE(set.protected_symbols, QStringList{ QStringLiteral("501") });
	
	contour->setHidden(false);
	field->setHidden(false);
	auto* meadow = new PathObject(field, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0) });
	map.addObject(meadow);
	map.addObjectToSelection(meadow, false);
	StatusSet hide_field;
	hide_field.hidden = QStringList{ QStringLiteral("401") };
	hide_field.protected_symbols = QStringList{ QStringLiteral("501") };
	QCOMPARE(applyStatusSet(map, hide_field), 1);
	QVERIFY(field->isHidden());
	QVERIFY(!map.isObjectSelected(meadow));
	
	QCOMPARE(applyStatusSet(map, set), 2);
	QVERIFY(contour->isHidden());
	QVERIFY(!field->isHidden());
	QVERIFY(boulder->isProtected());
}


void OpsTest::symbolCompareTest()
{
	// the design notes, "Compare with file", Accept (1) to (4).
	auto const make_map = [](Map& map, bool other) {
		auto* brown = new MapColor(QStringLiteral("Brown"), 0);
		brown->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
		auto* black = new MapColor(QStringLiteral("Black"), 1);
		black->setCmyk(other ? MapColorCmyk(0.1f, 0.1f, 0.1f, 1.0f) : MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
		if (other)
		{
			map.addColor(black, 0);
			map.addColor(brown, 1);
			auto* blue = new MapColor(QStringLiteral("Blue"), 2);
			map.addColor(blue, 2);
		}
		else
		{
			map.addColor(brown, 0);
			map.addColor(black, 1);
		}
		auto const line = [&map, brown](int number, const char* name, double width) {
			auto* symbol = new LineSymbol();
			symbol->setNumberComponent(0, number);
			symbol->setName(QString::fromLatin1(name));
			symbol->setColor(map.getColor(map.findColorIndex(brown)));
			symbol->setLineWidth(width);
			map.addSymbol(symbol, map.getNumSymbols());
			return symbol;
		};
		line(101, "Contour", 0.14);
		if (other)
		{
			line(102, "Index contour", 0.3);
			auto* field = new AreaSymbol();
			field->setNumberComponent(0, 401);
			field->setName(QStringLiteral("Field"));
			map.addSymbol(field, map.getNumSymbols());
		}
		else
		{
			line(102, "Index", 0.25);
			auto* boulder = new PointSymbol();
			boulder->setNumberComponent(0, 501);
			boulder->setName(QStringLiteral("Boulder"));
			map.addSymbol(boulder, map.getNumSymbols());
		}
	};
	Map here, there;
	make_map(here, false);
	make_map(there, true);
	SymbolCompareOptions options;
	auto result = compareSymbolSets(here, there, options);
	QCOMPARE(result.only_here, QStringList{ QStringLiteral("501 Boulder") });
	QCOMPARE(result.only_there, QStringList{ QStringLiteral("401 Field") });
	QCOMPARE(int(result.changed.size()), 1);
	QCOMPARE(result.changed.front().symbol, QStringLiteral("102 Index"));
	QCOMPARE(result.changed.front().differences, (QStringList{ QStringLiteral("name"), QStringLiteral("definition") }));
	QVERIFY(result.identical.isEmpty());
	options.list_identical = true;
	QCOMPARE(compareSymbolSets(here, there, options).identical, QStringList{ QStringLiteral("101 Contour") });
	
	here.getSymbol(0)->setDescription(QStringLiteral("Joins points of equal height."));
	result = compareSymbolSets(here, there, options);
	QVERIFY(std::any_of(result.changed.begin(), result.changed.end(), [](const SymbolComparison::Change& c) {
		return c.symbol == QLatin1String("101 Contour") && c.differences == QStringList{ QStringLiteral("description") };
	}));
	options.descriptions = false;
	QCOMPARE(compareSymbolSets(here, there, options).identical, QStringList{ QStringLiteral("101 Contour") });
	
	here.getSymbol(0)->setDescription(QString{});
	options = SymbolCompareOptions{};
	options.used_only = true;
	here.addObject(new PathObject(here.getSymbol(0), MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
	result = compareSymbolSets(here, there, options);
	QVERIFY(result.only_here.isEmpty());
	QVERIFY(result.changed.empty());
	QCOMPARE(result.only_there, QStringList{ QStringLiteral("401 Field") });
	
	QCOMPARE(result.colours_only_there, QStringList{ QStringLiteral("Blue") });
	QVERIFY(result.colours_only_here.isEmpty());
	QCOMPARE(result.colours_changed, QStringList{ QStringLiteral("Black") });
	QVERIFY(result.colour_order_differs);
	auto const text = symbolComparisonText(result);
	QVERIFY(text.contains(QLatin1String("Blue")) && text.contains(QLatin1String("Black")) && text.contains(QLatin1String("401 Field")) && text.contains(QLatin1String("another order")));
}


void OpsTest::convertToPointsTest()
{
	// the design notes, "Convert objects", Accept (1) to (3).
	Map map;
	auto* line_symbol = new LineSymbol();
	map.addSymbol(line_symbol, 0);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 1);
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 2);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 3);
	auto const path = [&map](const Symbol* s, MapCoordVector coords, bool closed) {
		auto* object = new PathObject(s, coords);
		if (closed)
			object->closeAllParts();
		map.addObject(object);
		return object;
	};
	auto const at = [](const Object* object) { return MapCoordF(object->getRawCoordinateVector().front()); };
	auto const near = [](const MapCoordF& p, double x, double y) { return std::abs(p.x() - x) <= 0.001 && std::abs(p.y() - y) <= 0.001; };
	
	auto* text = new TextObject(text_symbol);
	text->setAnchorPosition(MapCoord(10.0, 10.0));
	text->setText(QStringLiteral("405"));
	text->setTag(QStringLiteral("ele"), QStringLiteral("405"));
	map.addObject(text);
	ops::ConvertToPointsParams params;
	params.symbol = point_symbol;
	params.text_offset = MapCoordF(1.0, -2.0);
	auto result = ops::convertToPoints(map, { text }, params);
	QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
	QCOMPARE(result.out.size(), std::size_t(1));
	QCOMPARE(result.consumed, std::vector<const Object*>{ text });
	QCOMPARE(result.out.front()->getType(), Object::Point);
	QCOMPARE(result.out.front()->getSymbol(), static_cast<const Symbol*>(point_symbol));
	QVERIFY(near(at(result.out.front().get()), 11.0, 8.0));
	QCOMPARE(result.out.front()->uuid(), text->uuid());
	QCOMPARE(result.out.front()->getTag(QStringLiteral("ele")), QStringLiteral("405"));
	
	params.text_offset = MapCoordF();
	auto* square = path(area_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) }, true);
	auto* ell = path(area_symbol, { MapCoord(0.0, 0.0), MapCoord(20.0, 0.0), MapCoord(20.0, 10.0), MapCoord(10.0, 10.0), MapCoord(10.0, 20.0), MapCoord(0.0, 20.0) }, true);
	auto* bend = path(line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(10.0, 10.0) }, false);
	result = ops::convertToPoints(map, { square, ell, bend }, params);
	QCOMPARE(result.out.size(), std::size_t(3));
	QVERIFY(near(at(result.out[0].get()), 5.0, 5.0));
	QVERIFY2(near(at(result.out[1].get()), 8.333, 8.333), qPrintable(QStringLiteral("%1 %2").arg(at(result.out[1].get()).x()).arg(at(result.out[1].get()).y())));
	QVERIFY(near(at(result.out[2].get()), 7.5, 2.5));
	
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	result = ops::convertToPoints(map, { point, bend }, params);
	QCOMPARE(result.out.size(), std::size_t(1));
	QVERIFY(!result.warning.isEmpty());
	params.symbol = nullptr;
	QVERIFY(!ops::convertToPoints(map, { bend }, params).error.isEmpty());
}


void OpsTest::symbolDescriptionsTest()
{
	// the design notes, "Descriptions file", Accept (1) to (3).
	auto const make_map = [](Map& map, bool german) {
		auto* contour = new LineSymbol();
		contour->setNumberComponent(0, 101);
		contour->setName(german ? QString::fromUtf8("H\xc3\xb6henlinie") : QStringLiteral("Contour"));
		if (!german)
			contour->setDescription(QStringLiteral("Joins points;\n\"equal\" height"));
		map.addSymbol(contour, 0);
		auto* field = new AreaSymbol();
		field->setNumberComponent(0, 401);
		field->setName(QStringLiteral("Field"));
		map.addSymbol(field, 1);
	};
	Map english, german;
	make_map(english, false);
	make_map(german, true);
	auto const csv = symbolDescriptionsCsv(english);
	auto const lines = csv.split(QLatin1Char('\n'), QString::SkipEmptyParts);
	QCOMPARE(lines.front(), QStringLiteral("number;name;description"));
	QVERIFY(csv.contains(QLatin1String("101;Contour;\"Joins points;\n\"\"equal\"\" height\"")));
	QVERIFY(csv.contains(QLatin1String("401;Field;\n")));
	
	auto result = applySymbolDescriptionsCsv(german, csv);
	QCOMPARE(result.changed, 1);
	QVERIFY(result.unmatched.isEmpty());
	QCOMPARE(result.malformed, 0);
	QCOMPARE(german.getSymbol(0)->getName(), QStringLiteral("Contour"));
	QCOMPARE(german.getSymbol(0)->getDescription(), english.getSymbol(0)->getDescription());
	
	auto more = csv;
	more.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
	more += QStringLiteral("999;Nothing;\r\n401;Field only\r\n");
	result = applySymbolDescriptionsCsv(german, more);
	QCOMPARE(result.changed, 0);
	QCOMPARE(result.unmatched, QStringList{ QStringLiteral("999") });
	QCOMPARE(result.malformed, 1);
	QCOMPARE(german.getSymbol(0)->getDescription(), english.getSymbol(0)->getDescription());
}


void OpsTest::favouritesTest()
{
	// the design notes, "Favourites", Accept (1).
	Map map;
	auto* contour = new LineSymbol();
	contour->setNumberComponent(0, 101);
	map.addSymbol(contour, 0);
	auto* field = new AreaSymbol();
	field->setNumberComponent(0, 401);
	map.addSymbol(field, 1);
	addFavourites(map, { contour, field });
	QCOMPARE(map.favouriteSymbols(), (QStringList{ QStringLiteral("101"), QStringLiteral("401") }));
	addFavourites(map, { contour });
	QCOMPARE(map.favouriteSymbols().size(), 2);
	removeFavourites(map, { field });
	QCOMPARE(map.favouriteSymbols(), QStringList{ QStringLiteral("101") });
	QVERIFY(isFavourite(map, contour));
	QVERIFY(!isFavourite(map, field));
}


void OpsTest::symbolTreeTest()
{
	// the design notes, "Symbol tree", Accept (1).
	Map map;
	auto* contour = new LineSymbol();
	contour->setNumberComponent(0, 101);
	map.addSymbol(contour, 0);
	auto* index = new LineSymbol();
	index->setNumberComponent(0, 102);
	map.addSymbol(index, 1);
	QCOMPARE(addSymbolGroup(map, QStringLiteral("Contours")), 0);
	QCOMPARE(addSymbolGroup(map, QStringLiteral("Index"), 0), 1);
	QCOMPARE(addSymbolGroup(map, QStringLiteral("Print")), 2);
	addToSymbolGroup(map, 0, { contour });
	addToSymbolGroup(map, 1, { index });
	addToSymbolGroup(map, 2, { index });
	QCOMPARE(symbolGroupNumbers(map, 0), (QStringList{ QStringLiteral("101"), QStringLiteral("102") }));
	QCOMPARE(symbolGroupNumbers(map, 0, false), QStringList{ QStringLiteral("101") });
	QCOMPARE(symbolGroupNumbers(map, 1), QStringList{ QStringLiteral("102") });
	removeFromSymbolGroup(map, 1, { index });
	QVERIFY(symbolGroupNumbers(map, 1).isEmpty());
	QCOMPARE(symbolGroupNumbers(map, 2), QStringList{ QStringLiteral("102") });
	renameSymbolGroup(map, 2, QStringLiteral("Printing"));
	QCOMPARE(map.symbolTree()[2].name, QStringLiteral("Printing"));
	removeSymbolGroup(map, 0);
	QCOMPARE(int(map.symbolTree().size()), 1);
	QCOMPARE(map.symbolTree()[0].name, QStringLiteral("Printing"));
	QCOMPARE(map.symbolTree()[0].parent, -1);
}


void OpsTest::symbolUpdateTest()
{
	// the design notes, "Update from file", Accept (1) and (2).
	auto const make_map = [](Map& map, bool reference) {
		auto* brown = new MapColor(QStringLiteral("Brown"), 0);
		brown->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
		map.addColor(brown, 0);
		auto const line = [&map, brown](int number, const char* name, double width) {
			auto* symbol = new LineSymbol();
			symbol->setNumberComponent(0, number);
			symbol->setName(QString::fromLatin1(name));
			symbol->setColor(brown);
			symbol->setLineWidth(width);
			map.addSymbol(symbol, map.getNumSymbols());
			return symbol;
		};
		line(101, "Contour", 0.14);
		auto* index = line(102, "Index", reference ? 0.3 : 0.25);
		if (reference)
		{
			auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
			yellow->setCmyk(MapColorCmyk(0.0f, 0.27f, 0.79f, 0.0f));
			map.addColor(yellow, 1);
			auto* field = new AreaSymbol();
			field->setNumberComponent(0, 401);
			field->setName(QStringLiteral("Field"));
			field->setColor(yellow);
			map.addSymbol(field, map.getNumSymbols());
		}
		else
		{
			index->setProtected(true);
			auto* boulder = new PointSymbol();
			boulder->setNumberComponent(0, 501);
			boulder->setName(QStringLiteral("Boulder"));
			boulder->setHidden(true);
			map.addSymbol(boulder, map.getNumSymbols());
			map.addObject(new PathObject(index, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
		}
	};
	Map map, reference;
	make_map(map, false);
	make_map(reference, true);
	auto const report = updateSymbolsFrom(map, reference);
	QCOMPARE(report.added, QStringList{ QStringLiteral("401 Field") });
	QCOMPARE(report.changed, QStringList{ QStringLiteral("102 Index") });
	QCOMPARE(report.unchanged, QStringList{ QStringLiteral("101 Contour") });
	QCOMPARE(map.getNumPublicSymbols(), 4);
	auto const* index = map.getSymbol(1);
	QCOMPARE(index->getNumberAsString(), QStringLiteral("102"));
	QCOMPARE(index->asLine()->getLineWidth(), 300);
	QVERIFY(index->isProtected());
	QCOMPARE(map.getPart(0)->getObject(0)->getSymbol(), index);
	auto has_yellow = false;
	for (int i = 0; i < map.getNumColors(); ++i)
		has_yellow |= map.getColor(i)->getName() == QLatin1String("Yellow");
	QVERIFY(has_yellow);
	QCOMPARE(map.getSymbol(2)->getNumberAsString(), QStringLiteral("501"));
	QVERIFY(map.getSymbol(2)->isHidden());
	
	Map scaled;
	make_map(scaled, false);
	auto const scaled_report = updateSymbolsFrom(scaled, reference, 1.5);
	QCOMPARE(scaled_report.changed, (QStringList{ QStringLiteral("101 Contour"), QStringLiteral("102 Index") }));
	QCOMPARE(scaled.getSymbol(1)->asLine()->getLineWidth(), 450);
}

void OpsTest::buildingsTest()
{
	// the design notes, "Make rectangular" and "Generalize buildings", Accept.
	Map map;
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 0);
	auto* line_symbol = new LineSymbol();
	map.addSymbol(line_symbol, 1);
	auto const ring = [&map, area_symbol](std::initializer_list<MapCoord> coords) {
		auto* object = new PathObject(area_symbol);
		for (auto const& coord : coords)
			object->addCoordinate(coord);
		object->parts().front().setClosed(true);
		map.addObject(object);
		return object;
	};
	auto const vertices = [](const Object* object) {
		std::vector<QPointF> out;
		auto const& coords = object->asPath()->getRawCoordinateVector();
		for (std::size_t i = 0; i + 1 < coords.size(); ++i)  // without the close point
			out.emplace_back(coords[i].x(), coords[i].y());
		return out;
	};
	auto const area = [](const std::vector<QPointF>& ring) {
		auto sum = 0.0;
		for (std::size_t i = 0; i < ring.size(); ++i)
			sum += ring[i].x() * ring[(i + 1) % ring.size()].y() - ring[(i + 1) % ring.size()].x() * ring[i].y();
		return std::abs(sum) / 2;
	};
	// Coordinates have a resolution of 0.001 mm, so positions are compared within 0.01 mm and areas within 0.05 mm².
	auto const near = [](double a, double b, double tolerance = 0.01) { return std::abs(a - b) <= tolerance; };
	auto const has_vertex = [&near](const std::vector<QPointF>& ring, double x, double y) {
		for (auto const& p : ring)
		{
			if (near(p.x(), x) && near(p.y(), y))
				return true;
		}
		return false;
	};

	// Make rectangular with a reference line.
	auto* reference = new PathObject(line_symbol, { MapCoord(0.0, 20.0), MapCoord(10.0, 20.0) });
	map.addObject(reference);
	auto* skewed = ring({ MapCoord(0.0, 0.0), MapCoord(10.0, 0.2), MapCoord(10.2, 5.0), MapCoord(0.0, 5.1) });
	ops::RectangularParams rectangular;
	rectangular.reference = reference;
	auto squared = ops::makeRectangular(map, { reference, skewed }, rectangular);
	QVERIFY2(squared.error.isEmpty(), squared.error.toLatin1());
	QVERIFY(squared.warning.isEmpty());
	QCOMPARE(squared.out.size(), std::size_t(1));
	QCOMPARE(squared.consumed, (std::vector<const Object*>{ skewed }));
	QCOMPARE(squared.out.front()->uuid(), skewed->uuid());
	auto corners = vertices(squared.out.front().get());
	QCOMPARE(corners.size(), std::size_t(4));
	for (auto const& p : { QPointF(0.0, 0.1), QPointF(10.1, 0.1), QPointF(10.1, 5.05), QPointF(0.0, 5.05) })
		QVERIFY2(has_vertex(corners, p.x(), p.y()), qPrintable(QString::number(p.x()) + QLatin1Char(' ') + QString::number(p.y())));
	rectangular.place_at_distance = true;
	rectangular.distance_mm = 2.0;
	auto placed = ops::makeRectangular(map, { reference, skewed }, rectangular);
	QVERIFY(placed.error.isEmpty());
	corners = vertices(placed.out.front().get());
	QVERIFY(has_vertex(corners, 0.0, 18.0) && has_vertex(corners, 10.1, 18.0) && has_vertex(corners, 0.0, 13.05));

	// Without a reference: right angles and a close area.
	ops::RectangularParams own;
	auto own_result = ops::makeRectangular(map, { skewed }, own);
	QVERIFY2(own_result.error.isEmpty(), own_result.error.toLatin1());
	QCOMPARE(own_result.out.size(), std::size_t(1));
	corners = vertices(own_result.out.front().get());
	QCOMPARE(corners.size(), std::size_t(4));
	for (std::size_t i = 0; i < 4; ++i)
	{
		auto const a = corners[(i + 3) % 4] - corners[i];
		auto const b = corners[(i + 1) % 4] - corners[i];
		auto const angle = std::abs(qRadiansToDegrees(std::acos((a.x() * b.x() + a.y() * b.y()) / (std::hypot(a.x(), a.y()) * std::hypot(b.x(), b.y())))));
		QVERIFY2(std::abs(angle - 90.0) < 0.01, qPrintable(QString::number(angle)));
	}
	QVERIFY(std::abs(area(corners) - area(vertices(skewed))) < 0.05 * area(vertices(skewed)));

	// A chamfer is a free edge: nothing changes.
	auto* chamfered = ring({ MapCoord(0.0, 30.0), MapCoord(8.0, 30.0), MapCoord(10.0, 32.0), MapCoord(10.0, 35.0), MapCoord(0.0, 35.0) });
	auto unchanged = ops::makeRectangular(map, { chamfered }, own);
	QVERIFY(unchanged.error.isEmpty());
	QVERIFY(unchanged.out.empty());

	// Rejection.
	auto* triangle = ring({ MapCoord(0.0, 40.0), MapCoord(10.0, 40.0), MapCoord(10.0, 41.0) });
	auto rejected = ops::makeRectangular(map, { triangle }, own);
	QVERIFY(!rejected.error.isEmpty());
	auto partly = ops::makeRectangular(map, { triangle, skewed }, own);
	QVERIFY(partly.error.isEmpty());
	QVERIFY(!partly.warning.isEmpty());
	QCOMPARE(partly.out.size(), std::size_t(1));
	auto* open_line = new PathObject(line_symbol, { MapCoord(0.0, 50.0), MapCoord(10.0, 50.2), MapCoord(10.0, 55.0) });
	map.addObject(open_line);
	QVERIFY(!ops::makeRectangular(map, { open_line }, own).error.isEmpty());

	// Simplify.
	auto* jogged = ring({ MapCoord(0.0, 60.0), MapCoord(3.0, 60.0), MapCoord(3.0, 60.04), MapCoord(10.0, 60.04), MapCoord(10.0, 65.0), MapCoord(0.0, 65.0) });
	ops::SimplifyBuildingParams simplify;
	simplify.offset_mm = 0.05;
	auto simplified = ops::simplifyBuildings(map, { jogged }, simplify);
	QVERIFY2(simplified.error.isEmpty(), simplified.error.toLatin1());
	QCOMPARE(simplified.out.size(), std::size_t(1));
	QCOMPARE(vertices(simplified.out.front().get()).size(), std::size_t(4));
	QVERIFY(near(area(vertices(simplified.out.front().get())), area(vertices(jogged)), 0.05));
	simplify.compensate_area = false;
	auto uncompensated = ops::simplifyBuildings(map, { jogged }, simplify);
	QVERIFY(!near(area(vertices(uncompensated.out.front().get())), area(vertices(jogged)), 0.05));
	simplify.compensate_area = true;
	jogged->getCoordinateRef(1).setDashPoint(true);
	auto kept = ops::simplifyBuildings(map, { jogged }, simplify);
	QCOMPARE(vertices(kept.out.front().get()).size(), std::size_t(5));
	QVERIFY(kept.out.front()->asPath()->getCoordinate(1).isDashPoint());
	auto* flat = ring({ MapCoord(0.0, 70.0), MapCoord(5.0, 70.001), MapCoord(10.0, 70.0), MapCoord(10.0, 75.0), MapCoord(0.0, 75.0) });
	simplify.offset_mm = 0.0;
	auto by_angle = ops::simplifyBuildings(map, { flat }, simplify);
	QCOMPARE(by_angle.out.size(), std::size_t(1));
	QCOMPARE(vertices(by_angle.out.front().get()).size(), std::size_t(4));

	// Rectangles: each.
	auto const c = std::cos(qDegreesToRadians(30.0));
	auto const s = std::sin(qDegreesToRadians(30.0));
	auto const rotate = [c, s](double x, double y) { return MapCoord(c * x - s * y + 50.0, s * x + c * y + 100.0); };
	auto* rotated_box = ring({ rotate(0.0, 0.0), rotate(10.0, 0.0), rotate(10.0, 4.0), rotate(0.0, 4.0) });
	ops::RectangleParams rectangle;
	auto boxed = ops::buildingRectangles(map, { rotated_box }, rectangle);
	QVERIFY2(boxed.error.isEmpty(), boxed.error.toLatin1());
	QCOMPARE(boxed.out.size(), std::size_t(1));
	corners = vertices(boxed.out.front().get());
	QCOMPARE(corners.size(), std::size_t(4));
	for (auto const& coord : vertices(rotated_box))
		QVERIFY(has_vertex(corners, coord.x(), coord.y()));
	rectangle.area_factor = 2.0;
	auto doubled = ops::buildingRectangles(map, { rotated_box }, rectangle);
	QVERIFY(near(area(vertices(doubled.out.front().get())), 80.0, 0.05));

	// Rectangles: all and series.
	auto* square_a = ring({ MapCoord(0.0, 200.0), MapCoord(4.0, 200.0), MapCoord(4.0, 204.0), MapCoord(0.0, 204.0) });
	auto* square_b = ring({ MapCoord(6.0, 200.0), MapCoord(10.0, 200.0), MapCoord(10.0, 204.0), MapCoord(6.0, 204.0) });
	rectangle.area_factor = 1.0;
	rectangle.mode = ops::RectangleParams::All;
	auto all = ops::buildingRectangles(map, { square_a, square_b }, rectangle);
	QVERIFY2(all.error.isEmpty(), all.error.toLatin1());
	QCOMPARE(all.out.size(), std::size_t(1));
	QCOMPARE(all.consumed, (std::vector<const Object*>{ square_a, square_b }));
	QCOMPARE(all.out.front()->uuid(), square_a->uuid());
	corners = vertices(all.out.front().get());
	QVERIFY(near(area(corners), 32.0, 0.05));
	QPointF centre;
	for (auto const& p : corners)
		centre += p / 4.0;
	QVERIFY(near(centre.x(), 5.0) && near(centre.y(), 202.0));
	rectangle.mode = ops::RectangleParams::Series;
	rectangle.count = 2;
	auto series = ops::buildingRectangles(map, { square_a, square_b }, rectangle);
	QVERIFY2(series.error.isEmpty(), series.error.toLatin1());
	QCOMPARE(series.out.size(), std::size_t(2));
	for (auto const& output : series.out)
	{
		auto const piece = vertices(output.get());
		QVERIFY(near(area(piece), 16.0, 0.05));
		auto min_x = 1e9, max_x = -1e9, sum_y = 0.0;
		for (auto const& p : piece)
		{
			min_x = std::min(min_x, p.x());
			max_x = std::max(max_x, p.x());
			sum_y += p.y();
		}
		QVERIFY(near(max_x - min_x, 5.0) && (near(min_x, 0.0) || near(min_x, 5.0)));
		QVERIFY(near(sum_y / 4, 202.0));
	}
	QCOMPARE(series.out[0]->uuid(), square_a->uuid());
	QCOMPARE(series.out[1]->derivedFrom(), square_a->uuid());
}

void OpsTest::smoothTest()
{
	// the design notes, "Smooth", Accept.
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* area_symbol = new AreaSymbol();
	map.addSymbol(area_symbol, 1);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 2);
	auto const line = [&map, symbol](const MapCoordVector& coords) {
		auto* object = new PathObject(symbol, coords);
		map.addObject(object);
		return object;
	};
	auto const vertices = [](const Object* object) {
		std::vector<QPointF> out;
		auto const& coords = object->asPath()->getRawCoordinateVector();
		for (std::size_t i = 0; i < coords.size(); ++i)
		{
			out.emplace_back(coords[i].x(), coords[i].y());
			if (coords[i].isCurveStart())
				i += 2;
		}
		return out;
	};
	auto const near = [](const QPointF& a, const QPointF& b) { return std::abs(a.x() - b.x()) <= 0.002 && std::abs(a.y() - b.y()) <= 0.002; };

	MapCoordVector zigzag;
	for (int i = 0; i <= 10; ++i)
		zigzag.emplace_back(double(i), (i == 0 || i == 10) ? 0.0 : (i % 2 ? 0.05 : -0.05));
	auto* zig = line(zigzag);
	ops::SmoothParams params;
	auto smoothed = ops::smoothPaths(map, { zig }, params);
	QVERIFY2(smoothed.error.isEmpty(), smoothed.error.toLatin1());
	QCOMPARE(smoothed.out.size(), std::size_t(1));
	QCOMPARE(smoothed.consumed, (std::vector<const Object*>{ zig }));
	QCOMPARE(smoothed.out.front()->uuid(), zig->uuid());
	QCOMPARE(smoothed.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(2));
	QVERIFY(!smoothed.out.front()->asPath()->getCoordinate(0).isCurveStart());
	QVERIFY(near(vertices(smoothed.out.front().get()).back(), QPointF(10.0, 0.0)));

	auto dashed = zigzag;
	dashed[5].setDashPoint(true);
	auto* zig_dash = line(dashed);
	auto with_dash = ops::smoothPaths(map, { zig_dash }, params);
	QVERIFY(with_dash.error.isEmpty());
	auto const* dash_path = with_dash.out.front()->asPath();
	QCOMPARE(dash_path->getCoordinateCount(), MapCoordVector::size_type(7));
	QVERIFY(dash_path->getCoordinate(0).isCurveStart() && dash_path->getCoordinate(3).isCurveStart());
	QVERIFY(dash_path->getCoordinate(3).isDashPoint());
	auto const v = vertices(dash_path);
	QCOMPARE(v.size(), std::size_t(3));
	QVERIFY(near(v[1], QPointF(5.0, 0.05)));
	auto const h_in = QPointF(dash_path->getCoordinate(2).x(), dash_path->getCoordinate(2).y());
	auto const h_out = QPointF(dash_path->getCoordinate(4).x(), dash_path->getCoordinate(4).y());
	auto const a = v[1] - h_in;
	auto const b = h_out - v[1];
	QVERIFY(std::abs(a.x() * b.y() - a.y() * b.x()) / std::hypot(b.x(), b.y()) < 0.01);

	auto* corner = line({ MapCoord(0.0, 20.0), MapCoord(10.0, 20.0), MapCoord(10.0, 30.0) });
	params.keep_corners = true;
	auto kept = ops::smoothPaths(map, { corner }, params);
	QVERIFY(kept.error.isEmpty() && kept.out.empty());
	params.keep_corners = false;
	auto rounded = ops::smoothPaths(map, { corner }, params);
	QCOMPARE(rounded.out.size(), std::size_t(1));
	QCOMPARE(rounded.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(7));
	QVERIFY(near(vertices(rounded.out.front().get())[1], QPointF(10.0, 20.0)));

	auto* s_curve = line({ MapCoord(0.0, 40.0), MapCoord(2.0, 41.0), MapCoord(4.0, 41.5), MapCoord(6.0, 41.0), MapCoord(8.0, 40.0), MapCoord(10.0, 39.0), MapCoord(12.0, 38.5), MapCoord(14.0, 39.0), MapCoord(16.0, 40.0) });
	params.tolerance = 2.0;
	auto flattened = ops::smoothPaths(map, { s_curve }, params);
	QCOMPARE(flattened.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(2));
	params.keep_inflections = true;
	auto inflected = ops::smoothPaths(map, { s_curve }, params);
	QCOMPARE(inflected.out.front()->asPath()->getCoordinateCount(), MapCoordVector::size_type(7));
	QVERIFY(near(vertices(inflected.out.front().get())[1], QPointF(10.0, 39.0)));
	params.keep_inflections = false;
	params.tolerance = 0.01;

	auto* ring = new PathObject(area_symbol);
	for (int i = 0; i < 12; ++i)
		ring->addCoordinate(MapCoord(50.0 + 5.0 * std::cos(qDegreesToRadians(30.0 * i)), 50.0 + 5.0 * std::sin(qDegreesToRadians(30.0 * i))));
	ring->parts().front().setClosed(true);
	map.addObject(ring);
	auto circled = ops::smoothPaths(map, { ring }, params);
	QVERIFY2(circled.error.isEmpty(), circled.error.toLatin1());
	QCOMPARE(circled.out.size(), std::size_t(1));
	auto const* circle = circled.out.front()->asPath();
	QCOMPARE(circle->getCoordinateCount(), MapCoordVector::size_type(37));
	QVERIFY(circle->parts().front().isClosed());
	QCOMPARE(vertices(circle).size(), std::size_t(13));  // twelve plus the close point

	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	QVERIFY(!ops::smoothPaths(map, { point }, params).error.isEmpty());
}


void OpsTest::toCurveTest()
{
	// the design notes, "Polyline to curve", Accept.
	Map map;
	auto* symbol = new LineSymbol();
	map.addSymbol(symbol, 0);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 1);
	auto const line = [&map, symbol](const MapCoordVector& coords) {
		auto* object = new PathObject(symbol, coords);
		map.addObject(object);
		return object;
	};
	auto const coordsOf = [](const ops::OpResult& result) {
		return result.out.front()->asPath()->getRawCoordinateVector();
	};
	auto const at = [](const MapCoord& coord) { return QPointF(coord.x(), coord.y()); };
	auto const len = [](const QPointF& v) { return std::hypot(v.x(), v.y()); };
	auto const sine = [len](const QPointF& a, const QPointF& b) { return std::abs(a.x() * b.y() - a.y() * b.x()) / (len(a) * len(b)); };
	auto const along = [sine](const QPointF& v, const QPointF& direction) {
		return sine(v, direction) < 0.001 && QPointF::dotProduct(v, direction) > 0;
	};
	auto const smoothAt = [at, sine](const MapCoordVector& c, std::size_t i) {
		auto const a = at(c[i - 1]) - at(c[i]);
		auto const b = at(c[i + 1]) - at(c[i]);
		return sine(a, b) < 0.001 && QPointF::dotProduct(a, b) < 0;
	};
	ops::ToCurveParams defaults;
	
	// (1) Every vertex is kept, every segment becomes a curve.
	auto* zig = line({ MapCoord(0.0, 0.0), MapCoord(10.0, 5.0), MapCoord(20.0, 0.0), MapCoord(30.0, 5.0) });
	auto result = ops::toCurve(map, { zig }, defaults);
	QVERIFY2(result.error.isEmpty(), result.error.toLatin1());
	QCOMPARE(result.out.size(), std::size_t(1));
	QCOMPARE(result.consumed, (std::vector<const Object*>{ zig }));
	QCOMPARE(result.out.front()->uuid(), zig->uuid());
	auto c = coordsOf(result);
	QCOMPARE(c.size(), MapCoordVector::size_type(10));
	for (std::size_t i : { 0u, 3u, 6u })
		QVERIFY(c[i].isCurveStart());
	QVERIFY(c[0].isPositionEqualTo(MapCoord(0.0, 0.0)));
	QVERIFY(c[3].isPositionEqualTo(MapCoord(10.0, 5.0)));
	QVERIFY(c[6].isPositionEqualTo(MapCoord(20.0, 0.0)));
	QVERIFY(c[9].isPositionEqualTo(MapCoord(30.0, 5.0)));
	QVERIFY(smoothAt(c, 3));
	QVERIFY(smoothAt(c, 6));
	
	// (2) The end angles.
	QVERIFY(along(at(c[1]) - at(c[0]), QPointF(10.0, 5.0)));
	auto free_ends = defaults;
	free_ends.keep_end_angles = false;
	auto const free = coordsOf(ops::toCurve(map, { zig }, free_ends));
	QVERIFY(along(at(free[1]) - at(free[0]), QPointF(0.6, 0.8)));
	
	// (3) The tangent factor.
	auto longer = defaults;
	longer.tangent_factor = 0.5;
	auto const l = coordsOf(ops::toCurve(map, { zig }, longer));
	QCOMPARE(l.size(), c.size());
	for (std::size_t i = 0; i < 9; i += 3)
	{
		auto const ratio1 = len(at(l[i + 1]) - at(l[i])) / len(at(c[i + 1]) - at(c[i]));
		auto const ratio2 = len(at(l[i + 2]) - at(l[i + 3])) / len(at(c[i + 2]) - at(c[i + 3]));
		QVERIFY2(std::abs(ratio1 - 1.5) < 0.002, qPrintable(QString::number(ratio1)));
		QVERIFY2(std::abs(ratio2 - 1.5) < 0.002, qPrintable(QString::number(ratio2)));
	}
	
	// (4) Corners.
	auto* bend = line({ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(12.0, 10.0), MapCoord(22.0, 10.0) });
	auto sharp = defaults;
	sharp.corner_angle_deg = 60.0;
	c = coordsOf(ops::toCurve(map, { bend }, sharp));
	QCOMPARE(c.size(), MapCoordVector::size_type(10));
	QVERIFY(c[2].isPositionEqualTo(c[3]));
	QVERIFY(c[4].isPositionEqualTo(c[3]));
	c = coordsOf(ops::toCurve(map, { bend }, defaults));
	QVERIFY(!c[2].isPositionEqualTo(c[3]));
	QVERIFY(smoothAt(c, 3));
	
	// (5) Keep straight parts.
	auto* road = line({ MapCoord(0.0, 0.0), MapCoord(40.0, 0.0), MapCoord(42.0, 0.5), MapCoord(44.0, 1.5), MapCoord(45.5, 3.0), MapCoord(73.5, 31.0) });
	auto roads = defaults;
	roads.keep_straight_parts = true;
	c = coordsOf(ops::toCurve(map, { road }, roads));
	QCOMPARE(c.size(), MapCoordVector::size_type(12));
	QVERIFY(!c[0].isCurveStart());
	QVERIFY(c[1].isCurveStart() && c[4].isCurveStart() && c[7].isCurveStart());
	QVERIFY(!c[10].isCurveStart());
	QVERIFY(c[10].isPositionEqualTo(MapCoord(45.5, 3.0)));
	QVERIFY(std::abs(c[2].y()) < 0.001 && c[2].x() > 40.0);
	QVERIFY(along(at(c[10]) - at(c[9]), QPointF(1.0, 1.0)));
	QCOMPARE(coordsOf(ops::toCurve(map, { road }, defaults)).size(), MapCoordVector::size_type(16));
	
	// (6) A closed part.
	MapCoordVector hexagon;
	for (int i = 0; i < 6; ++i)
		hexagon.emplace_back(10 * std::cos(i * M_PI / 3), 10 * std::sin(i * M_PI / 3));
	auto* closed = line(hexagon);
	closed->closeAllParts();
	result = ops::toCurve(map, { closed }, defaults);
	QCOMPARE(result.out.size(), std::size_t(1));
	auto const* closed_out = result.out.front()->asPath();
	QCOMPARE(closed_out->parts().size(), std::size_t(1));
	QVERIFY(closed_out->parts().front().isClosed());
	c = closed_out->getRawCoordinateVector();
	QCOMPARE(c.size(), MapCoordVector::size_type(19));
	for (std::size_t i = 0; i < 18; i += 3)
		QVERIFY(c[i].isCurveStart());
	auto const first_handles = std::vector<QPointF>{ at(c[17]) - at(c[0]), at(c[1]) - at(c[0]) };
	QVERIFY(sine(first_handles[0], first_handles[1]) < 0.001 && QPointF::dotProduct(first_handles[0], first_handles[1]) < 0);
	
	// (7) An existing curve stays, and the new handle continues it.
	MapCoord joint(10, 0);
	joint.setCurveStart(true);
	joint.setDashPoint(true);
	auto* mixed = line({ MapCoord(0.0, 0.0), joint, MapCoord(10.0, 5.0), MapCoord(15.0, 10.0), MapCoord(20.0, 10.0) });
	c = coordsOf(ops::toCurve(map, { mixed }, defaults));
	QCOMPARE(c.size(), MapCoordVector::size_type(7));
	QVERIFY(c[0].isCurveStart());
	QVERIFY(c[3].isCurveStart() && c[3].isDashPoint());
	QVERIFY(c[3].isPositionEqualTo(MapCoord(10.0, 0.0)));
	QVERIFY(c[4].isPositionEqualTo(MapCoord(10.0, 5.0)));
	QVERIFY(c[5].isPositionEqualTo(MapCoord(15.0, 10.0)));
	QVERIFY(c[6].isPositionEqualTo(MapCoord(20.0, 10.0)));
	QVERIFY(smoothAt(c, 3));
	QVERIFY(along(at(c[1]) - at(c[0]), QPointF(1.0, 0.0)));
	
	// (8) Nothing to convert.
	MapCoord curve_start(0, 0);
	curve_start.setCurveStart(true);
	auto* curved = line({ curve_start, MapCoord(3.0, 0.0), MapCoord(7.0, 0.0), MapCoord(10.0, 0.0) });
	auto* point = new PointObject(point_symbol);
	map.addObject(point);
	result = ops::toCurve(map, { curved, point }, defaults);
	QVERIFY(result.error.isEmpty());
	QVERIFY(result.out.empty());
	QVERIFY(result.consumed.empty());
	result = ops::toCurve(map, { point }, defaults);
	QVERIFY(!result.error.isEmpty());
}


void OpsTest::stairwayTest()
{
	// the design notes, "Stairway", Accept (1) to (3).
	Map map;
	auto* line = new LineSymbol();
	map.addSymbol(line, 0);
	auto* area = new AreaSymbol();
	map.addSymbol(area, 1);
	map.addObject(new PathObject(line, { MapCoord(50.0, 50.0), MapCoord(60.0, 50.0) }));
	map.getPart(0)->getObject(0)->setGroup(1);
	
	ops::StairwayGeometry geometry;
	geometry.first = MapCoordF(0.0, 0.0);
	geometry.second = MapCoordF(10.0, 0.0);
	geometry.last = MapCoordF(0.0, 4.0);
	ops::StairwayParams params;
	params.symbol = line;
	params.tread_mm = 1.0;
	params.group = 2;
	auto result = ops::stairway(map, geometry, params);
	QVERIFY2(result.error.isEmpty(), result.error.toLatin1());
	QCOMPARE(result.out.size(), std::size_t(5));
	QVERIFY(result.consumed.empty());
	for (std::size_t k = 0; k < 5; ++k)
	{
		auto const* path = result.out[k]->asPath();
		QCOMPARE(path->getSymbol(), line);
		QCOMPARE(path->group(), 2);
		QCOMPARE(path->getCoordinateCount(), MapCoordVector::size_type(2));
		QVERIFY(path->getCoordinate(0).isPositionEqualTo(MapCoord(0.0, double(k))));
		QVERIFY(path->getCoordinate(1).isPositionEqualTo(MapCoord(10.0, double(k))));
	}
	
	params.tread_mm = 1.5;
	result = ops::stairway(map, geometry, params);
	QCOMPARE(result.out.size(), std::size_t(4));
	for (std::size_t k = 0; k < 4; ++k)
		QVERIFY(std::abs(result.out[k]->asPath()->getCoordinate(0).y() - 4.0 * double(k) / 3.0) < 0.001);
	
	params.tread_mm = 0.0;
	QVERIFY(!ops::stairway(map, geometry, params).error.isEmpty());
	params.tread_mm = 1.0;
	params.symbol = area;
	QVERIFY(!ops::stairway(map, geometry, params).error.isEmpty());
	params.symbol = line;
	geometry.second = geometry.first;
	QVERIFY(!ops::stairway(map, geometry, params).error.isEmpty());
}

void OpsTest::frameTest()
{
	// the design notes, "Frame around the map", Accept (1) and (2).
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setColor(black);
	line->setLineWidth(0.2);
	map.addSymbol(line, 0);
	auto* point = new PointSymbol();
	map.addSymbol(point, 1);
	ops::FrameParams params;
	params.symbol = line;
	params.margin_mm = 2.0;
	QVERIFY(!ops::frameAroundMap(map, params).error.isEmpty());
	
	map.addObject(new PathObject(line, { MapCoord(0.0, 0.0), MapCoord(20.0, 10.0) }));
	map.updateAllObjects();
	auto const extent = map.calculateExtent(false, false);
	auto result = ops::frameAroundMap(map, params);
	QVERIFY2(result.error.isEmpty(), result.error.toLatin1());
	QCOMPARE(result.out.size(), std::size_t(1));
	auto const* frame = result.out.front()->asPath();
	QCOMPARE(frame->getSymbol(), line);
	QVERIFY(frame->parts().front().isClosed());
	auto const grown = extent.adjusted(-2.0, -2.0, 2.0, 2.0);
	auto const near = [](const MapCoordF& a, const QPointF& b) { return std::abs(a.x() - b.x()) < 0.001 && std::abs(a.y() - b.y()) < 0.001; };
	QVERIFY(near(MapCoordF(frame->getCoordinate(0)), grown.topLeft()));
	QVERIFY(near(MapCoordF(frame->getCoordinate(1)), grown.topRight()));
	QVERIFY(near(MapCoordF(frame->getCoordinate(2)), grown.bottomRight()));
	QVERIFY(near(MapCoordF(frame->getCoordinate(3)), grown.bottomLeft()));
	
	params.margin_mm = 0.0;
	result = ops::frameAroundMap(map, params);
	QVERIFY(near(MapCoordF(result.out.front()->asPath()->getCoordinate(0)), extent.topLeft()));
	
	params.symbol = point;
	QVERIFY(!ops::frameAroundMap(map, params).error.isEmpty());
	params.symbol = line;
	params.margin_mm = -1.0;
	QVERIFY(!ops::frameAroundMap(map, params).error.isEmpty());
}

void OpsTest::rectangleBoxTest()
{
	// the design notes, "Rectangle objects", Accept.
	Map map;
	auto* frame_symbol = new LineSymbol();
	map.addSymbol(frame_symbol, 0);
	auto* grid_symbol = new LineSymbol();
	map.addSymbol(grid_symbol, 1);
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 2);
	auto const near = [](const MapCoord& coord, double x, double y) {
		return std::abs(coord.x() - x) < 0.01 && std::abs(coord.y() - y) < 0.01;
	};
	
	ops::RectangleBoxGeometry geometry;
	geometry.top_left = MapCoordF(0.0, 0.0);
	geometry.top_right = MapCoordF(30.0, 0.0);
	geometry.bottom_left = MapCoordF(0.0, 20.0);
	ops::RectangleBoxParams params;
	params.frame = frame_symbol;
	params.group = 7;
	
	auto plain = ops::rectangleBox(map, geometry, params);
	QVERIFY2(plain.error.isEmpty(), plain.error.toLatin1());
	QCOMPARE(plain.out.size(), std::size_t(1));
	auto const* frame = plain.out.front()->asPath();
	QVERIFY(frame && frame->getSymbol() == frame_symbol);
	QVERIFY(frame->parts().front().isClosed());
	QCOMPARE(frame->getCoordinateCount(), MapCoordVector::size_type(5));
	QVERIFY(near(frame->getCoordinate(0), 0.0, 0.0));
	QVERIFY(near(frame->getCoordinate(1), 30.0, 0.0));
	QVERIFY(near(frame->getCoordinate(2), 30.0, 20.0));
	QVERIFY(near(frame->getCoordinate(3), 0.0, 20.0));
	QCOMPARE(frame->group(), 7);
	
	params.corner_radius = 2.0;
	auto rounded = ops::rectangleBox(map, geometry, params);
	QCOMPARE(rounded.out.size(), std::size_t(1));
	auto const* rounded_frame = rounded.out.front()->asPath();
	QCOMPARE(rounded_frame->getCoordinateCount(), MapCoordVector::size_type(17));
	QVERIFY(rounded_frame->getCoordinate(0).isCurveStart());
	QVERIFY(near(rounded_frame->getCoordinate(0), 28.0, 0.0));
	QVERIFY(near(rounded_frame->getCoordinate(3), 30.0, 2.0));
	QVERIFY(near(rounded_frame->getCoordinate(4), 30.0, 18.0));
	QVERIFY(near(rounded_frame->getCoordinate(12), 0.0, 2.0));
	QVERIFY(near(rounded_frame->getCoordinate(15), 2.0, 0.0));
	params.corner_radius = 0.0;
	
	params.grid = grid_symbol;
	params.cell_width = 10.0;
	params.cell_height = 10.0;
	auto gridded = ops::rectangleBox(map, geometry, params);
	QCOMPARE(gridded.out.size(), std::size_t(4));
	auto const* vertical = gridded.out[1]->asPath();
	QVERIFY(vertical->getSymbol() == grid_symbol);
	QVERIFY(near(vertical->getCoordinate(0), 10.0, 0.0) && near(vertical->getCoordinate(1), 10.0, 20.0));
	QVERIFY(near(gridded.out[2]->asPath()->getCoordinate(0), 20.0, 0.0));
	auto const* horizontal = gridded.out[3]->asPath();
	QVERIFY(near(horizontal->getCoordinate(0), 0.0, 10.0) && near(horizontal->getCoordinate(1), 30.0, 10.0));
	
	params.numbering = true;
	params.number_text = text_symbol;
	auto numbered = ops::rectangleBox(map, geometry, params);
	QCOMPARE(numbered.out.size(), std::size_t(10));
	QStringList texts;
	for (std::size_t i = 4; i < numbered.out.size(); ++i)
	{
		auto const* text = numbered.out[i]->asText();
		QVERIFY(text && text->getSymbol() == text_symbol);
		QCOMPARE(text->getRotation(), 0.0);
		QCOMPARE(text->group(), 7);
		auto const anchor = text->getAnchorCoordF();
		auto const cell_x = int((i - 4) % 3);
		auto const cell_y = int((i - 4) / 3);
		QVERIFY(anchor.x() > cell_x * 10.0 && anchor.x() < (cell_x + 1) * 10.0);
		QVERIFY(anchor.y() > cell_y * 10.0 && anchor.y() < (cell_y + 1) * 10.0);
		texts << text->getText();
	}
	QCOMPARE(texts.join(QLatin1Char(' ')), QStringLiteral("1 2 3 4 5 6"));
	
	params.number_from_bottom = true;
	params.unnumbered_cells = 2;
	params.unnumbered_text = QStringLiteral("X");
	auto from_bottom = ops::rectangleBox(map, geometry, params);
	texts.clear();
	for (std::size_t i = 4; i < from_bottom.out.size(); ++i)
		texts << from_bottom.out[i]->asText()->getText();
	QCOMPARE(texts.join(QLatin1Char(' ')), QStringLiteral("4 X X 1 2 3"));
	params.number_from_bottom = false;
	params.unnumbered_cells = 0;
	
	// The same box turned by 90 degrees.
	geometry.top_right = MapCoordF(0.0, 30.0);
	geometry.bottom_left = MapCoordF(-20.0, 0.0);
	auto turned = ops::rectangleBox(map, geometry, params);
	QCOMPARE(turned.out.size(), std::size_t(10));
	auto const* turned_line = turned.out[1]->asPath();
	QVERIFY(near(turned_line->getCoordinate(0), 0.0, 10.0) && near(turned_line->getCoordinate(1), -20.0, 10.0));
	QVERIFY(std::abs(turned.out[4]->asText()->getRotation() + M_PI / 2) < 1e-6);
	
	params.frame = nullptr;
	QVERIFY(!ops::rectangleBox(map, geometry, params).error.isEmpty());
	params.frame = frame_symbol;
	geometry.top_right = geometry.top_left;
	QVERIFY(!ops::rectangleBox(map, geometry, params).error.isEmpty());
}


QTEST_MAIN(OpsTest)
#include "ops_t.moc"  // IWYU pragma: keep
