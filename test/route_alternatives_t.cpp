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

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
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

QRectF fieldArea()
{
	return QRectF(0.0, 0.0, 20.0, 10.0);
}

/**
 * Forest of 20 by 10 mm at 1:10,000 with a building
 * from (8, 3) to (12, 7); with course objects, the start S1 at (2, 5), the
 * control 31 at (18, 5) and the finish F1 at (18, 9), and the course A.
 */
void fillMap(Map& map, bool with_course)
{
	map.setScaleDenominator(10000);
	auto* building = addSymbol<AreaSymbol>(map, 521);
	// The forest of 405, drawn, gives the rasters their extent; it is as fast as bare paper.
	addArea(map, addSymbol<AreaSymbol>(map, 405), QRectF(0.0, 0.0, 20.0, 10.0));
	addArea(map, building, QRectF(8.0, 3.0, 4.0, 4.0));
	if (!with_course)
		return;
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	addPoint(map, start, 2.0, 5.0);
	addPoint(map, control, 18.0, 5.0);
	addPoint(map, finish, 18.0, 9.0);
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
	changeCourseData(map, data);
}

/// The share of a route's length which lies farther than a distance from another route, both in mm.
double distinctShare(const std::vector<MapCoordF>& route, const std::vector<MapCoordF>& other, double distance)
{
	auto const nearest = [&other](const MapCoordF& place) {
		auto best = 1e9;
		for (std::size_t i = 1; i < other.size(); ++i)
		{
			auto const ax = other[i - 1].x();
			auto const ay = other[i - 1].y();
			auto const dx = other[i].x() - ax;
			auto const dy = other[i].y() - ay;
			auto const length2 = dx * dx + dy * dy;
			auto t = length2 > 0.0 ? ((place.x() - ax) * dx + (place.y() - ay) * dy) / length2 : 0.0;
			t = std::max(0.0, std::min(1.0, t));
			best = std::min(best, std::hypot(place.x() - ax - t * dx, place.y() - ay - t * dy));
		}
		return best;
	};
	auto total = 0.0;
	auto distinct = 0.0;
	for (std::size_t i = 1; i < route.size(); ++i)
	{
		auto const length = std::hypot(route[i].x() - route[i - 1].x(), route[i].y() - route[i - 1].y());
		auto const pieces = std::max(1, int(std::ceil(length / 0.1)));
		for (int p = 0; p < pieces; ++p)
		{
			auto const t = (p + 0.5) / pieces;
			auto const place = MapCoordF(route[i - 1].x() + t * (route[i].x() - route[i - 1].x()), route[i - 1].y() + t * (route[i].y() - route[i - 1].y()));
			total += length / pieces;
			if (nearest(place) > distance)
				distinct += length / pieces;
		}
	}
	return total > 0.0 ? distinct / total : 0.0;
}

/// The side of y = 5 on which a route passes the building, between x = 9 and x = 11: -1 above, 1 below, 0 for both or none.
int sideOf(const std::vector<MapCoordF>& route)
{
	auto above = false;
	auto below = false;
	for (std::size_t i = 1; i < route.size(); ++i)
	{
		for (int p = 0; p <= 20; ++p)
		{
			auto const t = p / 20.0;
			auto const x = route[i - 1].x() + t * (route[i].x() - route[i - 1].x());
			auto const y = route[i - 1].y() + t * (route[i].y() - route[i - 1].y());
			if (x < 9.0 || x > 11.0)
				continue;
			(y < 5.0 ? above : below) = true;
		}
	}
	if (above == below)
		return 0;
	return above ? -1 : 1;
}

}  // namespace



/**
 * @test Tests the alternative routes of the route analysis.
 *
 * See the design notes, "Route analysis: alternative routes".
 */
class RouteAlternativesTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void twoWaysTest();
	void oneWayTest();
	void storeTest();
	void courseTest();
	void runnerTest();
};


void RouteAlternativesTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteAlternativesTest::twoWaysTest()
{
	Map map;
	fillMap(map, false);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	auto const from = MapCoordF(2.0, 5.0);
	auto const to = MapCoordF(18.0, 5.0);
	auto const fastest = search.fastest(from, to);
	QVERIFY(fastest.found);
	auto const fastest_side = sideOf(fastest.points);
	QVERIFY(fastest_side != 0);

	auto const alternatives = search.alternatives(from, to, fastest, 1);
	QCOMPARE(int(alternatives.size()), 1);
	auto const& alternative = alternatives.front();
	QVERIFY(alternative.found);
	QCOMPARE(sideOf(alternative.points), -fastest_side);
	QVERIFY2(alternative.time_s >= fastest.time_s - 1e-3, qPrintable(QString::number(alternative.time_s)));
	QVERIFY2(alternative.time_s <= 1.25 * fastest.time_s, qPrintable(QString::number(alternative.time_s)));
	auto const expected = alternative.length_m * 0.2 / 0.83;
	QVERIFY2(std::abs(alternative.time_s / expected - 1.0) <= 0.02, qPrintable(QString::number(alternative.time_s) + QLatin1Char('/') + QString::number(expected)));
	QCOMPARE(alternative.points.front(), from);
	QCOMPARE(alternative.points.back(), to);
	for (auto const& point : alternative.points)
		QVERIFY(!terrain.isImpassableAt(point));

	// The search can be used again, and its fastest route is as before.
	auto const again = search.fastest(from, to);
	QCOMPARE(again.time_s, fastest.time_s);
}


/// Accept (2)
void RouteAlternativesTest::oneWayTest()
{
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* open = addSymbol<AreaSymbol>(map, 401);
		addArea(map, open, QRectF(0.0, 0.0, 20.0, 2.0));
		auto const terrain = buildRouteTerrain(map, RouteSettings{}, QRectF(0.0, 0.0, 20.0, 2.0));
		RouteSearch search(terrain, RouteSettings{}, 10000);
		auto const fastest = search.fastest(MapCoordF(2.0, 1.0), MapCoordF(18.0, 1.0));
		QVERIFY(fastest.found);
		QVERIFY(search.alternatives(MapCoordF(2.0, 1.0), MapCoordF(18.0, 1.0), fastest, 2).empty());
	}
	{
		Map map;
		fillMap(map, false);
		auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
		RouteSearch search(terrain, RouteSettings{}, 10000);
		auto const from = MapCoordF(2.0, 5.0);
		auto const to = MapCoordF(18.0, 5.0);
		auto const fastest = search.fastest(from, to);
		auto const alternatives = search.alternatives(from, to, fastest, 2);
		QVERIFY(!alternatives.empty());
		QVERIFY(alternatives.size() <= 2);
		for (std::size_t i = 0; i < alternatives.size(); ++i)
		{
			QVERIFY(distinctShare(alternatives[i].points, fastest.points, 2.0) > 0.2);
			if (i > 0)
			{
				QVERIFY(alternatives[i].time_s >= alternatives[i - 1].time_s);
				QVERIFY(distinctShare(alternatives[i].points, alternatives[i - 1].points, 2.0) > 0.2);
			}
		}
		// Nothing for a route which was not found.
		QVERIFY(search.alternatives(from, to, RouteResult{}, 2).empty());
	}
}


/// Accept (3)
void RouteAlternativesTest::storeTest()
{
	Map map;
	fillMap(map, false);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	auto const fastest = search.fastest(MapCoordF(2.0, 5.0), MapCoordF(18.0, 5.0));

	StoredRoutes store;
	StoredRoute stored;
	stored.from = QStringLiteral("S1");
	stored.to = QStringLiteral("31");
	stored.points = fastest.points;
	stored.length_m = fastest.length_m;
	stored.time_s = fastest.time_s;
	store.put(stored);
	StoredRoute via = stored;
	via.index = 1;
	via.via = { MapCoordF(10.0, 9.0) };
	via.points = search.fastestVia(MapCoordF(2.0, 5.0), via.via, MapCoordF(18.0, 5.0)).points;
	store.put(via);

	QCOMPARE(addAlternativeRoutes(store, search, QStringLiteral("S1"), QStringLiteral("31"), 1), 1);
	auto const* alternative = store.find(QStringLiteral("S1"), QStringLiteral("31"), 2);
	QVERIFY(alternative);
	QVERIFY(alternative->alternative);
	QVERIFY(alternative->via.empty());
	QVERIFY(!store.find(QStringLiteral("S1"), QStringLiteral("31"), 1)->alternative);

	// Again: the alternative is replaced, the route over the via point stays.
	QCOMPARE(addAlternativeRoutes(store, search, QStringLiteral("S1"), QStringLiteral("31"), 1), 1);
	QCOMPARE(int(store.routes.size()), 3);
	QVERIFY(store.find(QStringLiteral("S1"), QStringLiteral("31"), 1));
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"), 1)->via.size(), std::size_t(1));

	// A leg without a kept route.
	QCOMPARE(addAlternativeRoutes(store, search, QStringLiteral("31"), QStringLiteral("F1"), 1), 0);

	// The file.
	QByteArray bytes;
	{
		QXmlStreamWriter xml(&bytes);
		xml.writeStartDocument();
		saveStoredRoutes(store, xml);
		xml.writeEndDocument();
	}
	QVERIFY2(bytes.contains("alternative=\"true\""), bytes.constData());
	auto const roundTrip = [](const StoredRoutes& routes) {
		QByteArray written;
		{
			QXmlStreamWriter writer(&written);
			writer.writeStartDocument();
			saveStoredRoutes(routes, writer);
			writer.writeEndDocument();
		}
		QXmlStreamReader reader(written);
		reader.readNextStartElement();
		return loadStoredRoutes(reader);
	};
	// The file keeps thousandths of a millimetre: what was read once is read again the same.
	auto const loaded = roundTrip(store);
	QCOMPARE(loaded.routes.size(), store.routes.size());
	QVERIFY(roundTrip(loaded) == loaded);
	auto const* loaded_alternative = loaded.find(QStringLiteral("S1"), QStringLiteral("31"), 2);
	QVERIFY(loaded_alternative && loaded_alternative->alternative);
	auto changed = loaded;
	changed.routes.back().alternative = !changed.routes.back().alternative;
	QVERIFY(changed != loaded);
}


/// Accept (4)
void RouteAlternativesTest::courseTest()
{
	Map map;
	fillMap(map, true);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, fieldArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	StoredRoutes store;
	auto const before = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QVERIFY(before.analysed);
	auto const added = addCourseAlternatives(map, map.courseData(), QStringLiteral("A"), -1, search, store, 1);
	QVERIFY2(added >= 1, qPrintable(QString::number(added)));
	auto count = 0;
	for (auto const& route : store.routes)
		count += route.alternative ? 1 : 0;
	QCOMPARE(count, added);

	auto data = map.courseData();
	data.routes = store;
	auto const after = storedCourseRoutes(map, data, QStringLiteral("A"), -1);
	QCOMPARE(courseRoutesText(after), courseRoutesText(before));
	QCOMPARE(after.time_s, before.time_s);
	
	// All courses of the map, as the runner analyses them.
	StoredRoutes all_store;
	auto const all = analyseAllRoutesOfMap(map, all_store, 1);
	QVERIFY2(all.alternatives >= 1, qPrintable(QString::number(all.alternatives)));
}


/// Accept (5)
void RouteAlternativesTest::runnerTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	{
		Map map;
		fillMap(map, true);
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	auto const job_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("job.json"));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("analyse-routes") }, { QStringLiteral("alternatives"), 1 } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("save-as") }, { QStringLiteral("path"), QStringLiteral("analysed.foom") } });
		QFile job_file(job_path);
		QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	QProcess process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process.setProcessEnvironment(env);
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	QVERIFY(process.waitForFinished(120000));
	QByteArray output = process.readAllStandardOutput();
	output += process.readAllStandardError();
	QVERIFY2(process.exitCode() == 0, output.constData());
	QVERIFY2(output.contains("\"alternatives\":") && !output.contains("\"alternatives\":0"), output.constData());

	QFile saved(QDir(dir.path()).absoluteFilePath(QStringLiteral("analysed.foom")));
	QVERIFY(saved.open(QIODevice::ReadOnly));
	QVERIFY(saved.readAll().contains("alternative=\"true\""));
}


QTEST_MAIN(RouteAlternativesTest)
#include "route_alternatives_t.moc"  // IWYU pragma: keep
