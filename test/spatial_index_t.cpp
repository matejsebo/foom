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


#include "spatial_index_t.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLatin1String>
#include <QRandomGenerator>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "test_config.h"

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_view.h"
#include "core/spatial_index.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "undo/object_undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace
{
	// The spec's bounds are 1 ms per 10 mm query and 0.1 ms of index work per
	// moved object. Unoptimized and sanitized builds get a generous factor.
#if defined(__SANITIZE_ADDRESS__) || !defined(QT_NO_DEBUG)
	constexpr qreal timing_factor = 10;
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
	constexpr qreal timing_factor = 10;
#  else
	constexpr qreal timing_factor = 1;
#  endif
#else
	constexpr qreal timing_factor = 1;
#endif
	
	constexpr int num_random_rects = 1000;
	
	
	qreal uniform(QRandomGenerator& rng, qreal low, qreal high)
	{
		return low + (high - low) * rng.generateDouble();
	}
	
	/// A random rectangle in or near the given extent, with log-uniform side lengths.
	QRectF randomRect(QRandomGenerator& rng, const QRectF& extent)
	{
		auto const longest = qMax(1.0, qMax(extent.width(), extent.height()));
		auto const w = std::exp(uniform(rng, std::log(0.02), std::log(longest)));
		auto const h = std::exp(uniform(rng, std::log(0.02), std::log(longest)));
		auto const x = uniform(rng, extent.left() - 10, extent.right() + 10);
		auto const y = uniform(rng, extent.top() - 10, extent.bottom() + 10);
		return { x, y, w, h };
	}
	
	/// The brute-force scan the index must not miss anything from.
	std::vector<Object*> bruteForceExtentScan(MapPart& part, const QRectF& rect)
	{
		std::vector<Object*> out;
		for (int i = 0; i < part.getNumObjects(); ++i)
		{
			auto* object = part.getObject(i);
			object->update();
			if (object->getExtent().intersects(rect))
				out.push_back(object);
		}
		return out;
	}
	
	/// MapPart::findObjectsAt as upstream OOM edd22fc implemented it, scanning every object.
	void bruteForceFindObjectsAt(MapPart& part, const MapCoordF& coord, qreal tolerance, bool treat_areas_as_paths,
	                             bool extended_selection, bool include_hidden_objects, bool include_protected_objects,
	                             SelectionInfoVector& out)
	{
		for (int i = 0; i < part.getNumObjects(); ++i)
		{
			auto* object = part.getObject(i);
			if (!include_hidden_objects && object->getSymbol()->isHidden())
				continue;
			if (!include_protected_objects && object->getSymbol()->isProtected())
				continue;
				
			object->update();
			int selected_type = object->isPointOnObject(coord, tolerance, treat_areas_as_paths, extended_selection);
			if (selected_type != int(Symbol::NoSymbol))
				out.emplace_back(selected_type, object);
		}
	}
	
	/// MapPart::findObjectsAtBox as upstream OOM edd22fc implemented it, scanning every object.
	void bruteForceFindObjectsAtBox(MapPart& part, const MapCoordF& corner1, const MapCoordF& corner2,
	                                bool include_hidden_objects, bool include_protected_objects,
	                                std::vector<Object*>& out)
	{
		auto rect = QRectF(corner1, corner2).normalized();
		for (int i = 0; i < part.getNumObjects(); ++i)
		{
			auto* object = part.getObject(i);
			if (!include_hidden_objects && object->getSymbol()->isHidden())
				continue;
			if (!include_protected_objects && object->getSymbol()->isProtected())
				continue;
				
			object->update();
			if (rect.intersects(object->getExtent()) && object->intersectsBox(rect))
				out.push_back(object);
		}
	}
	
	template <typename T>
	std::vector<T> sorted(std::vector<T> v)
	{
		std::sort(begin(v), end(v));
		return v;
	}
	
	bool hasDuplicates(std::vector<Object*> v)
	{
		std::sort(begin(v), end(v));
		return std::adjacent_find(begin(v), end(v)) != end(v);
	}
	
	bool contains(const std::vector<Object*>& v, const Object* object)
	{
		return std::find(begin(v), end(v), object) != end(v);
	}
	
	QRectF rectAround(qreal x, qreal y, qreal half_size = 1)
	{
		return { x - half_size, y - half_size, 2 * half_size, 2 * half_size };
	}
	
	PathObject* makeLine(const Symbol* symbol, qreal x0, qreal y0, qreal x1, qreal y1)
	{
		auto* path = new PathObject(symbol);
		path->addCoordinate(MapCoord(x0, y0));
		path->addCoordinate(MapCoord(x1, y1));
		return path;
	}
	
}  // namespace



void SpatialIndexTest::initTestCase()
{
	doStaticInitializations();
	
	auto const test_data_dir = QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR "/data"));
	QVERIFY(test_data_dir.exists());
	for (auto const& info : test_data_dir.entryInfoList({ QStringLiteral("*.omap"), QStringLiteral("*.xmap"), QStringLiteral("*.foom") }, QDir::Files, QDir::Name))
	{
		// legacy_map.omap is the binary format of Mapper before 0.9, which is refused.
		if (info.fileName() != QLatin1String("legacy_map.omap"))
			sample_maps.append(info.absoluteFilePath());
	}
		
	auto const examples_dir = QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR "/../examples"));
	QVERIFY(examples_dir.exists());
	for (auto const& info : examples_dir.entryInfoList({ QStringLiteral("*.omap") }, QDir::Files, QDir::Name))
		sample_maps.append(info.absoluteFilePath());
		
	QVERIFY(!sample_maps.isEmpty());
}



void SpatialIndexTest::candidateSupersetTest_data()
{
	QTest::addColumn<QString>("path");
	for (auto const& path : qAsConst(sample_maps))
		QTest::newRow(QFileInfo(path).fileName().toUtf8().constData()) << path;
}

void SpatialIndexTest::candidateSupersetTest()
{
	// the design notes, "Spatial index", Accept: for 1,000 random
	// rectangles on each example map the candidate set contains every object
	// the brute-force scan returns.
	QFETCH(QString, path);
	
	Map map;
	MapView view{ &map };
	QVERIFY(map.loadFrom(path, &view));
	if (map.getNumObjects() == 0)
		QSKIP("The map has no objects.");
	
	QRandomGenerator rng(42);
	auto num_queries = 0;
	auto num_candidates = 0;
	auto num_hits = 0;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (part->getNumObjects() == 0)
			continue;
			
		auto const extent = part->calculateExtent(true);
		QVERIFY(extent.isValid());
		
		std::vector<Object*> candidates;
		for (int i = 0; i < num_random_rects; ++i)
		{
			auto const rect = randomRect(rng, extent);
			auto const expected = bruteForceExtentScan(*part, rect);
			
			candidates.clear();
			part->query(rect, candidates);
			QVERIFY2(!hasDuplicates(candidates), "query() returned an object twice");
			for (auto const* object : expected)
			{
				QVERIFY2(contains(candidates, object),
				         qPrintable(QString::fromLatin1("Object missing from the candidates for %1,%2 %3x%4")
				                    .arg(rect.left()).arg(rect.top()).arg(rect.width()).arg(rect.height())));
			}
			++num_queries;
			num_candidates += int(candidates.size());
			num_hits += int(expected.size());
		}
	}
	QVERIFY(num_queries > 0);
	qInfo("%d objects, %d queries, %d candidates for %d extent hits", map.getNumObjects(), num_queries, num_candidates, num_hits);
}



void SpatialIndexTest::findObjectsUnchangedTest_data()
{
	candidateSupersetTest_data();
}

void SpatialIndexTest::findObjectsUnchangedTest()
{
	// findObjectsAt() and findObjectsAtBox() take their candidates from the
	// index but keep their exact tests, so their results do not change.
	QFETCH(QString, path);
	
	Map map;
	MapView view{ &map };
	QVERIFY(map.loadFrom(path, &view));
	
	QRandomGenerator rng(7);
	auto num_hits = 0;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (part->getNumObjects() == 0)
			continue;
			
		auto const extent = part->calculateExtent(true);
		for (int i = 0; i < 200; ++i)
		{
			auto const coord = MapCoordF(uniform(rng, extent.left() - 2, extent.right() + 2),
			                             uniform(rng, extent.top() - 2, extent.bottom() + 2));
			auto const tolerance = uniform(rng, 0.01, 3.0);
			for (auto flags = 0; flags < 8; ++flags)
			{
				auto const treat_areas_as_paths = (flags & 1) != 0;
				auto const extended_selection = (flags & 2) != 0;
				auto const include_hidden_and_protected = (flags & 4) != 0;
				
				SelectionInfoVector expected;
				bruteForceFindObjectsAt(*part, coord, tolerance, treat_areas_as_paths, extended_selection,
				                        include_hidden_and_protected, include_hidden_and_protected, expected);
				SelectionInfoVector actual;
				part->findObjectsAt(coord, tolerance, treat_areas_as_paths, extended_selection,
				                    include_hidden_and_protected, include_hidden_and_protected, actual);
				QCOMPARE(actual.size(), expected.size());
				QVERIFY(sorted(actual) == sorted(expected));
				num_hits += int(expected.size());
			}
		}
		
		for (int i = 0; i < 200; ++i)
		{
			auto const rect = randomRect(rng, extent);
			auto const corner1 = MapCoordF(rect.topLeft());
			auto const corner2 = MapCoordF(rect.bottomRight());
			for (auto flags = 0; flags < 2; ++flags)
			{
				auto const include_hidden_and_protected = flags != 0;
				std::vector<Object*> expected;
				bruteForceFindObjectsAtBox(*part, corner1, corner2, include_hidden_and_protected, include_hidden_and_protected, expected);
				std::vector<Object*> actual;
				part->findObjectsAtBox(corner1, corner2, include_hidden_and_protected, include_hidden_and_protected, actual);
				QCOMPARE(actual.size(), expected.size());
				QVERIFY(sorted(actual) == sorted(expected));
				num_hits += int(expected.size());
			}
		}
	}
	qInfo("%d hits compared", num_hits);
}



void SpatialIndexTest::maintenanceTest()
{
	Map map;
	auto* color = new MapColor(QStringLiteral("black"), 0);
	map.addColor(color, 0);
	auto* point_symbol = new PointSymbol();
	point_symbol->setInnerRadius(300);
	point_symbol->setInnerColor(color);
	map.addSymbol(point_symbol, 0);
	auto* line_symbol = new LineSymbol();
	line_symbol->setLineWidth(0.5);
	line_symbol->setColor(color);
	map.addSymbol(line_symbol, 1);
	
	auto* part = map.getCurrentPart();
	auto const& index = part->spatialIndex();
	std::vector<Object*> out;
	
	// An added object is found where it is, and nowhere else.
	auto* point = new PointObject(point_symbol);
	point->setPosition(MapCoord(10, 10));
	map.addObject(point);
	QCOMPARE(index.size(), std::size_t(1));
	part->query(rectAround(10, 10), out);
	QVERIFY(contains(out, point));
	out.clear();
	part->query(rectAround(100, 100), out);
	QVERIFY(out.empty());
	
	// A moved object is re-entered at the next query.
	point->move(90000, 90000);
	out.clear();
	part->query(rectAround(100, 100), out);
	QVERIFY(contains(out, point));
	out.clear();
	part->query(rectAround(10, 10), out);
	QVERIFY(out.empty());
	
	// Editing the symbol changes the extent through Object::forceUpdate().
	auto* line = makeLine(line_symbol, 0, 0, 50, 0);
	map.addObject(line);
	out.clear();
	part->query(rectAround(25, 0, 0.1), out);
	QVERIFY(contains(out, line));
	out.clear();
	part->query(rectAround(25, 8, 0.1), out);
	QVERIFY(!contains(out, line));
	line_symbol->setLineWidth(20);
	map.updateAllObjectsWithSymbol(line_symbol);
	out.clear();
	part->query(rectAround(25, 8, 0.1), out);
	QVERIFY(contains(out, line));
	line_symbol->setLineWidth(0.5);
	map.updateAllObjectsWithSymbol(line_symbol);
	out.clear();
	part->query(rectAround(25, 8, 0.1), out);
	QVERIFY(!contains(out, line));
	
	// Replacing an object (as ReplaceObjectsUndoStep::undo does) swaps the entries.
	auto* replacement = makeLine(line_symbol, 300, 300, 310, 300);
	part->setObject(replacement, part->findObjectIndex(line), true);
	line = nullptr;
	QCOMPARE(index.size(), std::size_t(2));
	out.clear();
	part->query(rectAround(25, 0), out);
	QVERIFY(out.empty());
	out.clear();
	part->query(rectAround(305, 300), out);
	QVERIFY(contains(out, replacement));
	
	// Deleting, undoing and redoing (Edit > Delete) keep the index in step.
	map.addObjectToSelection(replacement, false);
	map.deleteSelectedObjects();
	replacement = nullptr;
	QCOMPARE(index.size(), std::size_t(1));
	out.clear();
	part->query(rectAround(305, 300), out);
	QVERIFY(out.empty());
	map.undoManager().undo();
	QCOMPARE(index.size(), std::size_t(2));
	out.clear();
	part->query(rectAround(305, 300), out);
	QCOMPARE(out.size(), std::size_t(1));
	QCOMPARE(out.front(), part->getObject(1));
	map.undoManager().redo();
	QCOMPARE(index.size(), std::size_t(1));
	out.clear();
	part->query(rectAround(305, 300), out);
	QVERIFY(out.empty());
	QVERIFY(map.selectedObjects().empty());
	
	// Objects imported from another map (Map::importMap, MapPart::importPart) are entered.
	Map other;
	auto* other_color = new MapColor(QStringLiteral("black"), 0);
	other.addColor(other_color, 0);
	auto* other_symbol = new PointSymbol();
	other_symbol->setInnerRadius(300);
	other_symbol->setInnerColor(other_color);
	other.addSymbol(other_symbol, 0);
	auto* other_point = new PointObject(other_symbol);
	other_point->setPosition(MapCoord(200, 200));
	other.addObject(other_point);
	map.importMap(other, Map::CompleteImport);
	QCOMPARE(map.getNumObjects(), 2);
	QCOMPARE(index.size(), std::size_t(2));
	out.clear();
	part->query(rectAround(200, 200), out);
	QCOMPARE(out.size(), std::size_t(1));
	QCOMPARE(out.front(), part->getObject(1));
	
	// Map::query covers the current part or all parts.
	map.addPart(new MapPart(QStringLiteral("second"), &map), 1);
	auto* second = map.getPart(1);
	auto* second_point = new PointObject(point_symbol);
	second_point->setPosition(MapCoord(100, 100));
	second->addObject(second_point);
	QCOMPARE(map.getCurrentPart(), part);
	out.clear();
	map.query(rectAround(100, 100), out);
	QCOMPARE(out.size(), std::size_t(1));
	QCOMPARE(out.front(), point);
	out.clear();
	map.query(rectAround(100, 100), out, Map::AllParts);
	QCOMPARE(out.size(), std::size_t(2));
	QVERIFY(contains(out, point));
	QVERIFY(contains(out, second_point));
	
	// Moving an object between parts moves its entry.
	second->addObject(part->releaseObject(point));
	QCOMPARE(index.size(), std::size_t(1));
	QCOMPARE(second->spatialIndex().size(), std::size_t(2));
	out.clear();
	map.query(rectAround(100, 100), out);
	QVERIFY(out.empty());
	out.clear();
	second->query(rectAround(100, 100), out);
	QCOMPARE(out.size(), std::size_t(2));
	
	// Clearing the map leaves nothing behind.
	map.clear();
	QCOMPARE(map.getNumParts(), 0);
}



void SpatialIndexTest::largeObjectTest()
{
	Map map;
	auto* color = new MapColor(QStringLiteral("black"), 0);
	map.addColor(color, 0);
	auto* line_symbol = new LineSymbol();
	line_symbol->setLineWidth(0.5);
	line_symbol->setColor(color);
	map.addSymbol(line_symbol, 0);
	auto* part = map.getCurrentPart();
	auto const& index = part->spatialIndex();
	
	// 500 mm x 500 mm is 100 x 100 cells of 5 mm, more than 4,096.
	auto* large = makeLine(line_symbol, 0, 0, 500, 500);
	map.addObject(large);
	auto* small = makeLine(line_symbol, 0, 100, 10, 100);
	map.addObject(small);
	QCOMPARE(index.size(), std::size_t(2));
	QCOMPARE(index.numLarge(), std::size_t(1));
	
	// The large object is a candidate everywhere, as its cells are not recorded...
	std::vector<Object*> out;
	part->query(rectAround(10, 400), out);
	QVERIFY(contains(out, large));
	QVERIFY(!contains(out, small));
	// ... but the exact test still rejects it.
	out.clear();
	part->findObjectsAtBox(MapCoordF(9, 399), MapCoordF(11, 401), true, true, out);
	QVERIFY(out.empty());
	out.clear();
	part->findObjectsAtBox(MapCoordF(249, 249), MapCoordF(251, 251), true, true, out);
	QCOMPARE(out.size(), std::size_t(1));
	QCOMPARE(out.front(), large);
	
	// Shrinking the object moves it from the large list into the cells.
	large->setCoordinate(1, MapCoord(10, 10));
	out.clear();
	part->query(rectAround(5, 5), out);
	QVERIFY(contains(out, large));
	QCOMPARE(index.numLarge(), std::size_t(0));
	out.clear();
	part->query(rectAround(10, 400), out);
	QVERIFY(!contains(out, large));
	
	// Removing it leaves the small object alone in the index.
	map.deleteObject(large);
	QCOMPARE(index.size(), std::size_t(1));
	QCOMPARE(index.numLarge(), std::size_t(0));
}



void SpatialIndexTest::timingTest()
{
	// the design notes, "Spatial index", Accept: on a synthetic map
	// of 200,000 objects a 10 mm query returns in under 1 ms after warm-up,
	// and moving one object costs under 0.1 ms of index work.
	constexpr auto num_objects = 200000;
	constexpr auto map_size = 2000.0;  // mm, i.e. 20 km at 1:10,000
	constexpr auto query_size = 10.0;  // mm
	
	Map map;
	auto* color = new MapColor(QStringLiteral("black"), 0);
	map.addColor(color, 0);
	auto* point_symbol = new PointSymbol();
	point_symbol->setInnerRadius(300);
	point_symbol->setInnerColor(color);
	map.addSymbol(point_symbol, 0);
	auto* part = map.getCurrentPart();
	
	QRandomGenerator rng(2026);
	QElapsedTimer timer;
	timer.start();
	for (int i = 0; i < num_objects; ++i)
	{
		auto* point = new PointObject(point_symbol);
		point->setPosition(MapCoord(uniform(rng, 0, map_size), uniform(rng, 0, map_size)));
		part->addObject(point);
	}
	QCOMPARE(part->getNumObjects(), num_objects);
	qInfo("Building the map of %d objects: %.0f ms", num_objects, timer.nsecsElapsed() / 1e6);
	
	// Warm-up.
	std::vector<Object*> out;
	for (int i = 0; i < 100; ++i)
	{
		out.clear();
		part->query(QRectF(uniform(rng, 0, map_size), uniform(rng, 0, map_size), query_size, query_size), out);
	}
	
	// Queries: the best of five batches of 1,000, to be robust against other load.
	constexpr auto batch_size = 1000;
	std::vector<QRectF> rects;
	rects.reserve(batch_size);
	auto best_query_ms = 1e9;
	auto num_candidates = 0;
	for (int batch = 0; batch < 5; ++batch)
	{
		rects.clear();
		for (int i = 0; i < batch_size; ++i)
			rects.emplace_back(uniform(rng, 0, map_size), uniform(rng, 0, map_size), query_size, query_size);
			
		num_candidates = 0;
		timer.start();
		for (auto const& rect : rects)
		{
			out.clear();
			part->query(rect, out);
			num_candidates += int(out.size());
		}
		best_query_ms = qMin(best_query_ms, timer.nsecsElapsed() / 1e6 / batch_size);
	}
	qInfo("10 mm query: %.4f ms (bound %.1f ms), %.1f candidates on average",
	      best_query_ms, 1.0 * timing_factor, qreal(num_candidates) / batch_size);
	QVERIFY(best_query_ms < 1.0 * timing_factor);
	
	// Index work of a move: leaving the old cells and entering the new ones.
	// A second index over the same objects isolates it from Object::update().
	SpatialIndex index;
	for (int i = 0; i < num_objects; ++i)
		index.enter(part->getObject(i));
	QCOMPARE(index.size(), std::size_t(num_objects));
	
	std::vector<Object*> moved;
	moved.reserve(batch_size);
	for (int i = 0; i < batch_size; ++i)
		moved.push_back(part->getObject(rng.bounded(num_objects)));
	for (auto* object : moved)
	{
		object->move(qint32(uniform(rng, -50000, 50000)), qint32(uniform(rng, -50000, 50000)));
		object->update();
	}
	timer.start();
	for (auto* object : moved)
	{
		index.remove(object);
		index.enter(object);
	}
	auto const move_ms = timer.nsecsElapsed() / 1e6 / batch_size;
	qInfo("Index work per moved object: %.4f ms (bound %.2f ms)", move_ms, 0.1 * timing_factor);
	QVERIFY(move_ms < 0.1 * timing_factor);
	
	// The index of the part re-enters the moved objects lazily at the next
	// query, and finds them at their new positions.
	timer.start();
	for (auto* object : moved)
	{
		auto const pos = MapCoordF(object->getRawCoordinateVector().front());
		out.clear();
		part->query(rectAround(pos.x(), pos.y(), query_size / 2), out);
		QVERIFY(contains(out, object));
	}
	qInfo("Query after %d lazy moves: %.4f ms in total", batch_size, timer.nsecsElapsed() / 1e6);
	
	// Moving one object at a time, as editing does, stays within both bounds together.
	timer.start();
	for (auto* object : moved)
	{
		object->move(1000, 1000);
		auto const pos = MapCoordF(object->getRawCoordinateVector().front());
		out.clear();
		part->query(rectAround(pos.x(), pos.y(), query_size / 2), out);
		QVERIFY(contains(out, object));
	}
	auto const move_and_query_ms = timer.nsecsElapsed() / 1e6 / batch_size;
	qInfo("Move one object and query: %.4f ms", move_and_query_ms);
	QVERIFY(move_and_query_ms < 1.1 * timing_factor);
}



QTEST_MAIN(SpatialIndexTest)
