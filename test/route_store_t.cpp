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
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

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

void drawLine(Map& map, const Symbol* symbol, double x1, double y1, double x2, double y2)
{
	auto* object = new PathObject(symbol);
	object->addCoordinate(MapCoord(x1, y1));
	object->addCoordinate(MapCoord(x2, y2));
	push(map, object);
}

Course courseOf(const char* name, Course::Type type, int legs, const char* items)
{
	Course course;
	course.name = QString::fromLatin1(name);
	course.type = type;
	course.legs = legs;
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		CourseItem item;
		auto const first = word.at(0);
		if (word == QLatin1String("v"))
		{
			item.type = CourseItem::ForkBranch;
		}
		else if (word == QLatin1String("q"))
		{
			item.type = CourseItem::TeamForkEnd;
		}
		else if (first == QLatin1Char('r'))
		{
			item.type = CourseItem::TeamForkBegin;
			item.count = word.mid(1).toInt();
		}
		else
		{
			item.type = first == QLatin1Char('S') ? CourseItem::Start
			            : first == QLatin1Char('M') ? CourseItem::MarkedRoute
			            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
			item.code = word;
		}
		course.items.push_back(item);
	}
	return course;
}

/**
 * Open land with a marked route M1 to the start S1, the controls 31 and 32 with a
 * building between them, and the finish F1, on a line; the courses A, B and L, the
 * relay R and the course W, whose variation is a fault.
 */
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	auto* marked = addSymbol<LineSymbol>(map, 707, Symbol::CourseMarkedRoute);
	auto* building = addSymbol<AreaSymbol>(map, 521);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	auto* land = new PathObject(open);
	land->addCoordinate(MapCoord(0.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 10.0));
	land->addCoordinate(MapCoord(0.0, 10.0));
	land->parts().back().setClosed(true);
	map.addObject(land);
	drawPoint(map, start, 4.0, 5.0);
	drawPoint(map, control, 8.0, 5.0);
	drawPoint(map, control, 14.0, 5.0);
	drawPoint(map, finish, 18.0, 5.0);
	drawLine(map, marked, 1.0, 5.0, 3.0, 5.0);
	auto* wall = new PathObject(building);
	wall->addCoordinate(MapCoord(10.5, 3.0));
	wall->addCoordinate(MapCoord(11.5, 3.0));
	wall->addCoordinate(MapCoord(11.5, 7.0));
	wall->addCoordinate(MapCoord(10.5, 7.0));
	wall->parts().back().setClosed(true);
	map.addObject(wall);
	
	auto data = map.courseData();
	data.courses.push_back(courseOf("A", Course::Normal, 0, "S1 31 32 F1"));
	data.courses.push_back(courseOf("R", Course::Relay, 2, "S1 r2 v 31 v 32 q F1"));
	data.courses.push_back(courseOf("W", Course::Normal, 0, "S1 r2 v 31 v 32 q F1"));
	data.courses.push_back(courseOf("B", Course::Normal, 0, "S1 31 F1"));
	data.courses.push_back(courseOf("L", Course::Normal, 0, "M1 S1 31 F1"));
	changeCourseData(map, data);
}

QRectF mapArea()
{
	return QRectF(0.0, 0.0, 20.0, 10.0);
}

QString namesOf(const CourseRoutes& routes)
{
	QStringList names;
	for (auto const& leg : routes.legs)
		names.push_back(leg.marked ? leg.from : QString(leg.from + QLatin1Char('-') + leg.to));
	return names.join(QLatin1Char(' '));
}

QString tableNames(const AllCourseRoutes& all)
{
	QStringList names;
	for (auto const& table : all.tables)
		names.push_back(table.variant.isEmpty() ? table.course : QString(table.course + QLatin1Char('-') + table.variant));
	return names.join(QLatin1Char(' '));
}

StoredRoute routeOf(const char* from, const char* to, std::vector<MapCoordF> points)
{
	StoredRoute route;
	route.from = QString::fromLatin1(from);
	route.to = QString::fromLatin1(to);
	route.length_m = 123.4;
	route.time_s = 56.7;
	route.points = std::move(points);
	return route;
}

}  // namespace



/**
 * @test Tests the routes which are kept in the map, the analysis of all
 * courses and the text of the routes.
 *
 * See the design notes, "Route analysis: all courses, and
 * the routes in the file".
 */
class RouteStoreTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void storeTest();
	void mapFileTest();
	void wholeCourseTest();
	void keptRoutesTest();
	void allCoursesTest();
	void runnerTest();
};


void RouteStoreTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteStoreTest::storeTest()
{
	StoredRoutes store;
	QVERIFY(store.isEmpty());
	QVERIFY(!store.find(QStringLiteral("S1"), QStringLiteral("31")));
	store.put(routeOf("S1", "31", { MapCoordF(4.0, 5.0), MapCoordF(8.0, 5.0) }));
	store.put(routeOf("31", "S1", { MapCoordF(8.0, 5.0), MapCoordF(4.0, 5.0) }));
	QCOMPARE(store.routes.size(), std::size_t(2));
	auto const* route = store.find(QStringLiteral("S1"), QStringLiteral("31"));
	QVERIFY(route);
	QCOMPARE(route->points.front(), MapCoordF(4.0, 5.0));
	QVERIFY(!route->isMarkedRoute());
	
	auto other = routeOf("S1", "31", { MapCoordF(4.0, 5.0), MapCoordF(6.0, 6.0), MapCoordF(8.0, 5.0) });
	other.time_s = 99.0;
	store.put(other);
	QCOMPARE(store.routes.size(), std::size_t(2));
	QCOMPARE(store.find(QStringLiteral("S1"), QStringLiteral("31"))->time_s, 99.0);
	
	// The ends.
	QVERIFY(other.fits(MapCoordF(4.0, 5.0), MapCoordF(8.0, 5.0)));
	QVERIFY(other.fits(MapCoordF(4.1, 5.1), MapCoordF(8.0, 4.85)));
	QVERIFY(!other.fits(MapCoordF(4.0, 5.3), MapCoordF(8.0, 5.0)));
	QVERIFY(!other.fits(MapCoordF(4.0, 5.0), MapCoordF(8.3, 5.0)));
	
	// Routes with heights are not mixed with routes without.
	StoredRoutes flat;
	flat.put(routeOf("32", "F1", { MapCoordF(14.0, 5.0), MapCoordF(18.0, 5.0) }));
	store.take(flat);
	QCOMPARE(store.routes.size(), std::size_t(3));
	StoredRoutes high;
	high.with_heights = true;
	high.put(routeOf("31", "32", { MapCoordF(8.0, 5.0), MapCoordF(14.0, 5.0) }));
	store.take(high);
	QVERIFY(store.with_heights);
	QCOMPARE(store.routes.size(), std::size_t(1));
	QVERIFY(store.find(QStringLiteral("31"), QStringLiteral("32")));
	// Nothing is taken from a search which found nothing.
	store.take(StoredRoutes{});
	QVERIFY(store.with_heights);
	QCOMPARE(store.routes.size(), std::size_t(1));
}


/// Accept (2)
void RouteStoreTest::mapFileTest()
{
	CourseData data;
	QVERIFY(data.isEmpty());
	data.routes.with_heights = true;
	auto leg = routeOf("S1", "31", { MapCoordF(4.0, 5.0), MapCoordF(5.016, 5.127), MapCoordF(6.0, 5.254), MapCoordF(7.5, 5.0), MapCoordF(8.0, 5.0) });
	leg.climb_m = 12.3;
	data.routes.put(leg);
	auto marked = routeOf("M1", "M1", { MapCoordF(1.0, 5.0), MapCoordF(3.0, 5.0) });
	QVERIFY(marked.isMarkedRoute());
	data.routes.put(marked);
	auto none = routeOf("31", "32", { MapCoordF(8.0, 5.0), MapCoordF(14.0, 5.0) });
	none.found = false;
	none.length_m = 0.0;
	none.time_s = 0.0;
	data.routes.put(none);
	QVERIFY(!data.isEmpty());
	QVERIFY(data != CourseData{});
	
	QByteArray bytes;
	{
		QXmlStreamWriter writer(&bytes);
		saveCourseData(data, writer);
	}
	QVERIFY(bytes.contains("<routes heights=\"true\">"));
	QVERIFY2(bytes.contains("<route from=\"S1\" to=\"31\" length=\"123.4\" time=\"56.7\" climb=\"12.3\">4.000 5.000;5.016 5.127;"), bytes.constData());
	QVERIFY(bytes.contains("found=\"false\""));
	{
		QXmlStreamReader reader(bytes);
		QVERIFY(reader.readNextStartElement());
		auto const loaded = loadCourseData(reader);
		QVERIFY(!reader.hasError());
		QVERIFY(loaded.routes == data.routes);
		QVERIFY(loaded == data);
		QVERIFY(loaded.routes.with_heights);
		QCOMPARE(loaded.routes.routes.size(), std::size_t(3));
		QVERIFY(!loaded.routes.find(QStringLiteral("31"), QStringLiteral("32"))->found);
		QCOMPARE(loaded.routes.find(QStringLiteral("S1"), QStringLiteral("31"))->points.size(), std::size_t(5));
	}
	
	// Without routes there is no element.
	data.routes = {};
	data.courses.push_back(courseOf("A", Course::Normal, 0, "S1 31"));
	bytes.clear();
	{
		QXmlStreamWriter writer(&bytes);
		saveCourseData(data, writer);
	}
	QVERIFY(!bytes.contains("routes"));
}


/// Accept (3)
void RouteStoreTest::wholeCourseTest()
{
	Map map;
	fillMap(map);
	auto const& data = map.courseData();
	QVERIFY(data.findObject(QStringLiteral("S1")) && data.findObject(QStringLiteral("32")) && data.findObject(QStringLiteral("M1")));
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, mapArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	auto const routes = analyseCourseRoutes(map, data, QStringLiteral("L"), -1, search);
	QVERIFY(routes.analysed);
	QCOMPARE(namesOf(routes), QStringLiteral("M1 M1-S1 S1-31 31-F1"));
	QCOMPARE(routes.unknown, 0);
	QCOMPARE(routes.missing, 0);
	auto const lines = courseRoutesText(routes).split(QLatin1Char('\n'));
	QCOMPARE(lines[1], QStringLiteral("M1: \t20 m \t20 m \t00:05"));
	QVERIFY2(lines[2].startsWith(QLatin1String("Leg M1-S1\t")), qPrintable(lines[2]));
	// The length of the course box: with the marked route, without the leg from it to the start.
	auto const length = measureCourse(map, data, *data.findCourse(QStringLiteral("L")));
	QVERIFY(length.known);
	QVERIFY2(std::abs(routes.course_length_m - length.measured_m) < 0.01, qPrintable(QString::number(routes.course_length_m)));
	QVERIFY(routes.length_m > routes.course_length_m + 5.0);
}


/// Accept (4)
void RouteStoreTest::keptRoutesTest()
{
	Map map;
	fillMap(map);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, mapArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	StoredRoutes store;
	auto const a = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QCOMPARE(namesOf(a), QStringLiteral("S1-31 31-32 32-F1"));
	QCOMPARE(store.routes.size(), std::size_t(3));
	auto const b = analyseCourseRoutes(map, map.courseData(), QStringLiteral("B"), -1, search, store);
	QCOMPARE(namesOf(b), QStringLiteral("S1-31 31-F1"));
	QCOMPARE(store.routes.size(), std::size_t(4));
	QVERIFY(!store.with_heights);
	
	// What is kept and fits is not searched again.
	auto const original = *store.find(QStringLiteral("S1"), QStringLiteral("31"));
	auto doctored = original;
	doctored.time_s = 999.0;
	store.put(doctored);
	auto const again = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QCOMPARE(again.legs[0].route.time_s, 999.0);
	store.put(original);
	
	// From the store alone.
	auto data = map.courseData();
	QVERIFY(storedCourseRoutes(map, data, QStringLiteral("A"), -1).unknown == 3);
	QVERIFY(courseRoutesText(storedCourseRoutes(map, data, QStringLiteral("A"), -1)).contains(QLatin1String("Leg S1-31\tnot analysed\n")));
	data.routes = store;
	changeCourseData(map, data);
	auto const kept = storedCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1);
	QVERIFY(kept.analysed);
	QCOMPARE(kept.unknown, 0);
	QCOMPARE(courseRoutesText(kept), courseRoutesText(a));
	QCOMPARE(kept.legs[1].route.points.size(), a.legs[1].route.points.size());
	// The course L has legs which were not searched.
	auto const partly = storedCourseRoutes(map, map.courseData(), QStringLiteral("L"), -1);
	QCOMPARE(namesOf(partly), QStringLiteral("M1 M1-S1 S1-31 31-F1"));
	QCOMPARE(partly.unknown, 2);
	QVERIFY(!partly.legs[0].known && !partly.legs[1].known && partly.legs[2].known && partly.legs[3].known);
	
	// The control 31 is moved by 1 mm: the legs at it are no longer those of the routes.
	PointObject* control = nullptr;
	auto* part = map.getPart(0);
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto* object = part->getObject(i);
		if (object->getType() == Object::Point && std::abs(object->asPoint()->getCoordF().x() - 8.0) < 0.01)
			control = object->asPoint();
	}
	QVERIFY(control);
	control->setPosition(MapCoordF(8.0, 6.0));
	control->update();
	auto const moved = storedCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1);
	QCOMPARE(moved.unknown, 2);
	QVERIFY(!moved.legs[0].known && !moved.legs[1].known && moved.legs[2].known);
	QVERIFY(courseRoutesText(moved).contains(QLatin1String("Leg 31-32\tnot analysed\n")));
	// Searched again, they are kept in the place of the old ones.
	auto const anew = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	QCOMPARE(anew.unknown, 0);
	QCOMPARE(store.routes.size(), std::size_t(4));
	QVERIFY(store.find(QStringLiteral("S1"), QStringLiteral("31"))->fits(MapCoordF(4.0, 5.0), MapCoordF(8.0, 6.0)));
}


/// Accept (5)
void RouteStoreTest::allCoursesTest()
{
	Map map;
	fillMap(map);
	auto const terrain = buildRouteTerrain(map, RouteSettings{}, mapArea());
	RouteSearch search(terrain, RouteSettings{}, 10000);
	
	StoredRoutes store;
	auto const all = analyseAllRoutes(map, map.courseData(), search, store);
	QCOMPARE(tableNames(all), QStringLiteral("A R-1A R-1B R-2A R-2B B L"));
	QCOMPARE(all.skipped, QStringList{ QStringLiteral("W") });
	// S1-31, S1-32, 31-32, 31-F1, 32-F1, the marked route and M1-S1.
	QCOMPARE(store.routes.size(), std::size_t(7));
	QCOMPARE(namesOf(all.tables[1]), QStringLiteral("S1-31 31-F1"));
	QCOMPARE(namesOf(all.tables[2]), QStringLiteral("S1-32 32-F1"));
	QCOMPARE(all.tables[3].legs[0].route.points.size(), all.tables[1].legs[0].route.points.size());
	
	auto const text = allRoutesText(map.courseData(), store, all);
	auto const lines = text.split(QLatin1Char('\n'));
	QCOMPARE(lines[0], QStringLiteral("Leg S1-31: \t%1m").arg(qRound(all.tables[0].legs[0].route.length_m)));
	QVERIFY2(lines[1].startsWith(QLatin1String("Leg S1-32: \t")), qPrintable(lines[1]));
	QVERIFY2(lines[2].startsWith(QLatin1String("Leg 31-32: \t")), qPrintable(lines[2]));
	QVERIFY2(lines[3].startsWith(QLatin1String("Leg 31-F1: \t")), qPrintable(lines[3]));
	QVERIFY2(lines[4].startsWith(QLatin1String("Leg 32-F1: \t")), qPrintable(lines[4]));
	QCOMPARE(lines[5], QStringLiteral("M1: \t20m"));
	QVERIFY2(lines[6].startsWith(QLatin1String("Leg M1-S1: \t")), qPrintable(lines[6]));
	QVERIFY(lines[7].isEmpty());
	QCOMPARE(lines[8], QStringLiteral("-----"));
	QCOMPARE(lines[9], QStringLiteral("Course A\tFastest route (leg)\tFastest route (total)\tTime along route"));
	// A line of dashes for every course, the variants of the relay under one.
	QCOMPARE(text.count(QLatin1String("-----\n")), 4);
	QVERIFY(text.contains(QLatin1String("\n\n-----\nCourse R-1A\t")));
	QVERIFY(text.contains(QLatin1String("\n\nCourse R-1B\t")));
	QVERIFY(text.contains(QLatin1String("\n\nCourse R-2B\t")));
	QVERIFY(text.contains(QLatin1String("\n\n-----\nCourse B\t")));
	QVERIFY(!text.contains(QLatin1String("Course W")));
	QVERIFY2(text.endsWith(QStringLiteral("Time along course = %1\n\n").arg(formatRouteTime(all.tables.back().time_s))), qPrintable(text.right(80)));
	
	// From the routes which the map keeps.
	auto data = map.courseData();
	QVERIFY(storedAllRoutes(map, data).tables.size() == 7);
	QCOMPARE(storedAllRoutes(map, data).tables[0].unknown, 3);
	data.routes = store;
	changeCourseData(map, data);
	auto const kept = storedAllRoutes(map, map.courseData());
	QCOMPARE(tableNames(kept), tableNames(all));
	QCOMPARE(kept.skipped, all.skipped);
	QCOMPARE(allRoutesText(map.courseData(), map.courseData().routes, kept), text);
	
	// With the settings and the terrain of the map.
	StoredRoutes of_map;
	auto const by_map = analyseAllRoutesOfMap(map, of_map);
	QCOMPARE(tableNames(by_map), tableNames(all));
	QCOMPARE(of_map.routes.size(), std::size_t(7));
}


/// Accept (6)
void RouteStoreTest::runnerTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	QByteArray expected;
	{
		Map map;
		fillMap(map);
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
		StoredRoutes store;
		auto const all = analyseAllRoutesOfMap(map, store);
		expected = QByteArray("\xef\xbb\xbf") + allRoutesText(map.courseData(), store, all).toUtf8();
	}
	
	auto const job_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("job.json"));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("analyse-routes") }, { QStringLiteral("out"), QStringLiteral("routes.txt") } });
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
	QVERIFY2(output.contains("\"tables\":7"), output.constData());
	QVERIFY2(output.contains("\"routes\":7"), output.constData());
	QVERIFY2(output.contains("\"missing\":0"), output.constData());
	QVERIFY2(output.contains("\"skipped\":[\"W\"]"), output.constData());
	
	QFile text_file(QDir(dir.path()).absoluteFilePath(QStringLiteral("routes.txt")));
	QVERIFY(text_file.open(QIODevice::ReadOnly));
	QCOMPARE(text_file.readAll(), expected);
	
	Map saved;
	XMLFileFormat format;
	auto importer = format.makeImporter(QDir(dir.path()).absoluteFilePath(QStringLiteral("analysed.foom")), &saved, nullptr);
	QVERIFY(importer && importer->doImport());
	QCOMPARE(saved.courseData().routes.routes.size(), std::size_t(7));
	QVERIFY(saved.courseData().routes.find(QStringLiteral("M1"), QStringLiteral("M1")));
	QCOMPARE(storedCourseRoutes(saved, saved.courseData(), QStringLiteral("A"), -1).unknown, 0);
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteStoreTest)
#include "route_store_t.moc"  // IWYU pragma: keep
