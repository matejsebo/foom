/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/text_object.h"
#include "core/objects/object.h"
#include "core/ops/align.h"
#include "core/ops/apply_op.h"
#include "core/ops/flatten.h"
#include "core/ops/bounds.h"
#include "core/ops/mirror.h"
#include "core/ops/offset.h"
#include "core/ops/transform.h"
#include "core/ops/vertex_type.h"
#include "core/ops/op.h"
#include "core/ops/reshape.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "undo/undo_manager.h"

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
	
	// An empty input is an error.
	QVERIFY(!ops::mirror(map, {}, vertical).error.isEmpty());
	
	// Without the duplicate flag the output replaces the input and keeps its uuid.
	auto* part = map.getCurrentPart();
	map.addObjectToSelection(line, false);
	auto replace_result = ops::mirror(map, { line }, vertical);
	QCOMPARE(replace_result.consumed.size(), std::size_t(1));
	QVERIFY(ops::applyOp(map, std::move(replace_result)));
	QCOMPARE(part->getNumObjects(), 2);
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
	QCOMPARE(part->getNumObjects(), 3);
	QCOMPARE(map.findObject(uuid), line);
	auto const* copy = part->getObject(2);
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


QTEST_MAIN(OpsTest)
#include "ops_t.moc"  // IWYU pragma: keep
