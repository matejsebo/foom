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
#include <QString>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/apply_op.h"
#include "core/ops/flatten.h"
#include "core/ops/op.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
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
	
	
QTEST_MAIN(OpsTest)
#include "ops_t.moc"  // IWYU pragma: keep
