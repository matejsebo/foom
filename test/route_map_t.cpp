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


#include <cmath>
#include <cstddef>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_map.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/map/course_maps_dialog.h"
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

void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

Course courseOf(const char* name, const char* items)
{
	Course course;
	course.name = QString::fromLatin1(name);
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		CourseItem item;
		auto const first = word.at(0);
		item.type = first == QLatin1Char('S') ? CourseItem::Start
		            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
		item.code = word;
		course.items.push_back(item);
	}
	return course;
}

/**
 * Yellow open land of 20 by 10 mm with the start S1, the control 31 and the
 * finish F1 on a line; the courses A, over the control, and C, without it.
 * The routes of A are kept, with one over a via point for its first leg.
 */
void fillMap(Map& map, bool with_routes = true)
{
	map.setScaleDenominator(10000);
	auto* yellow = new MapColor(QStringLiteral("Yellow"), 0);
	yellow->setCmyk(MapColorCmyk(0.0f, 0.27f, 0.79f, 0.0f));
	map.addColor(yellow, 0);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	open->setColor(yellow);
	auto* land = new PathObject(open);
	land->addCoordinate(MapCoord(0.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 0.0));
	land->addCoordinate(MapCoord(20.0, 10.0));
	land->addCoordinate(MapCoord(0.0, 10.0));
	land->parts().back().setClosed(true);
	map.addObject(land);
	drawPoint(map, start, 2.0, 5.0);
	drawPoint(map, control, 10.0, 5.0);
	drawPoint(map, finish, 18.0, 5.0);
	
	auto data = map.courseData();
	data.courses.push_back(courseOf("A", "S1 31 F1"));
	data.courses.push_back(courseOf("C", "S1 F1"));
	changeCourseData(map, data);
	if (!with_routes)
		return;
	
	auto const settings = map.courseData().route;
	auto const terrain = buildRouteTerrain(map, settings);
	RouteSearch search(terrain, settings, 10000);
	StoredRoutes store;
	analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	addViaRoute(store, search, QStringLiteral("S1"), QStringLiteral("31"), { MapCoordF(6.0, 8.0) });
	data = map.courseData();
	data.routes = store;
	changeCourseData(map, data);
}

/// The size of the map of a course, written with or without its routes.
qint64 writtenSize(Map& map, const QString& folder, const QString& course, bool with_routes, QString* path = nullptr)
{
	CourseMapExportOptions options;
	options.folder = folder;
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ course };
	options.with_routes = with_routes;
	auto const report = exportCourseMaps(map, options);
	if (!report.ok || report.files.size() != 1)
		return -1;
	if (path)
		*path = report.files.front();
	return QFileInfo(report.files.front()).size();
}

}  // namespace



/**
 * @test Tests the kept routes on the maps of the courses.
 *
 * See the design notes, "Route analysis: the routes on the
 * maps of the courses".
 */
class RouteMapTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void paintTest();
	void exportTest();
	void runnerTest();
	void dialogTest();
};


void RouteMapTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteMapTest::paintTest()
{
	Map map;
	fillMap(map);
	auto const routes = storedCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1);
	QCOMPARE(routes.unknown, 0);
	QCOMPARE(routes.legs.size(), std::size_t(2));
	QCOMPARE(routes.legs[0].via_routes.size(), std::size_t(1));
	
	QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	{
		QPainter painter(&image);
		painter.scale(10.0, 10.0);
		paintCourseRoutes(&painter, routes);
	}
	auto const at = [&image](double x, double y) { return QColor(image.pixel(qBound(0, qRound(x * 10.0), 199), qBound(0, qRound(y * 10.0), 99))); };
	auto const on_route = at(14.0, 5.0);
	QVERIFY2(on_route.red() > 180 && on_route.green() < 120 && on_route.blue() < 140, qPrintable(on_route.name()));
	QCOMPARE(at(14.0, 7.0), QColor(Qt::white));
	QCOMPARE(at(14.0, 3.0), QColor(Qt::white));
	
	// Along the route over the via point, in the middle of its pieces.
	auto const& via_points = routes.legs[0].via_routes[0].points;
	auto blue = 0;
	auto places = 0;
	for (std::size_t i = 1; i < via_points.size(); ++i)
	{
		auto const steps = qMax(1, int(std::hypot(via_points[i].x() - via_points[i - 1].x(), via_points[i].y() - via_points[i - 1].y()) / 0.2));
		for (int s = 0; s < steps; ++s)
		{
			auto const t = (s + 0.5) / steps;
			auto const x = via_points[i - 1].x() + t * (via_points[i].x() - via_points[i - 1].x());
			auto const y = via_points[i - 1].y() + t * (via_points[i].y() - via_points[i - 1].y());
			// Where it leaves and joins the fastest route the two lie on each other.
			if (std::abs(y - 5.0) < 0.6)
				continue;
			auto const colour = at(x, y);
			++places;
			blue += colour.blue() > colour.red() + 60 ? 1 : 0;
		}
	}
	QVERIFY2(places > 20, qPrintable(QString::number(places)));
	QVERIFY2(blue > 0.4 * places, qPrintable(QString::number(blue) + QLatin1Char('/') + QString::number(places)));
	
	// A course of which no route is kept.
	auto const none = storedCourseRoutes(map, map.courseData(), QStringLiteral("C"), -1);
	QCOMPARE(none.unknown, 1);
	QImage blank(200, 100, QImage::Format_ARGB32_Premultiplied);
	blank.fill(Qt::white);
	auto painted = blank;
	{
		QPainter painter(&painted);
		painter.scale(10.0, 10.0);
		paintCourseRoutes(&painter, none);
	}
	QVERIFY(painted == blank);
}


/// Accept (2)
void RouteMapTest::exportTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	auto const folder = [&dir](const char* name) { return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name)); };
	
	QString plain_path;
	QString routes_path;
	auto const plain = writtenSize(map, folder("plain"), QStringLiteral("A"), false, &plain_path);
	auto const with_routes = writtenSize(map, folder("routes"), QStringLiteral("A"), true, &routes_path);
	QVERIFY(plain > 500);
	QVERIFY2(with_routes > plain + 100, qPrintable(QString::number(with_routes) + QLatin1Char('/') + QString::number(plain)));
	QCOMPARE(QFileInfo(routes_path).fileName(), QStringLiteral("m.A.pdf"));
	
	// A course of which no route is kept is written all the same.
	QVERIFY(writtenSize(map, folder("none"), QStringLiteral("C"), true) > 500);
	
	// The export leaves the routes and the map as they were.
	QCOMPARE(map.courseData().routes.routes.size(), std::size_t(3));
}


/// Accept (3)
void RouteMapTest::runnerTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	{
		Map map;
		fillMap(map);
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	auto const job_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("job.json"));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("plain") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("A") } } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("routes") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("A") } },
		                             { QStringLiteral("routes"), true } });
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
	
	auto const plain = QFileInfo(QDir(dir.path()).absoluteFilePath(QStringLiteral("plain/event.A.pdf")));
	auto const with_routes = QFileInfo(QDir(dir.path()).absoluteFilePath(QStringLiteral("routes/event.A.pdf")));
	QVERIFY2(plain.exists() && with_routes.exists(), output.constData());
	QVERIFY2(with_routes.size() > plain.size() + 100, qPrintable(QString::number(with_routes.size()) + QLatin1Char('/') + QString::number(plain.size())));
}


/// Accept (4)
void RouteMapTest::dialogTest()
{
	{
		Map map;
		fillMap(map, false);
		CourseMapsDialog dialog(&map, QDir::tempPath());
		auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("course-maps-routes"));
		QVERIFY(check);
		QVERIFY(!check->isEnabled());
		QVERIFY(!check->isChecked());
		QVERIFY(!dialog.options().with_routes);
	}
	Map map;
	fillMap(map);
	CourseMapsDialog dialog(&map, QDir::tempPath());
	auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("course-maps-routes"));
	QVERIFY(check);
	QVERIFY(check->isEnabled());
	QVERIFY(!check->isChecked());
	QVERIFY(!dialog.options().with_routes);
	check->setChecked(true);
	QVERIFY(dialog.options().with_routes);
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteMapTest)
#include "route_map_t.moc"  // IWYU pragma: keep
