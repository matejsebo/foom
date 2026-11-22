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
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
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
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/ocd_course_records.h"
#include "course/route_course.h"
#include "course/route_print.h"
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

void addPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

Course courseOf(const char* name, std::initializer_list<const char*> codes)
{
	Course course;
	course.name = QString::fromLatin1(name);
	for (auto const* code : codes)
	{
		CourseItem item;
		auto const text = QString::fromLatin1(code);
		item.type = text.startsWith(QLatin1Char('S')) ? CourseItem::Start
		            : text.startsWith(QLatin1Char('F')) ? CourseItem::Finish : CourseItem::Control;
		item.code = text;
		course.items.push_back(item);
	}
	return course;
}

/**
 * Open land of 40 by 20 mm at 1:10,000; the start S1 at (2, 5), the
 * controls 31 at (32, 5) and 32 at (5, 15), the finish F1 at (32, 15); the
 * courses A (S1 31 F1) and B (S1 F1), whose routes are kept, and C (S1 32).
 */
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	auto* land = new PathObject(open);
	land->addCoordinate(MapCoord(0.0, 0.0));
	land->addCoordinate(MapCoord(40.0, 0.0));
	land->addCoordinate(MapCoord(40.0, 20.0));
	land->addCoordinate(MapCoord(0.0, 20.0));
	land->parts().back().setClosed(true);
	map.addObject(land);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	addPoint(map, start, 2.0, 5.0);
	addPoint(map, control, 32.0, 5.0);
	addPoint(map, control, 5.0, 15.0);
	addPoint(map, finish, 32.0, 15.0);
	auto data = map.courseData();
	data.courses.push_back(courseOf("A", { "S1", "31", "F1" }));
	data.courses.push_back(courseOf("B", { "S1", "F1" }));
	data.courses.push_back(courseOf("C", { "S1", "32" }));
	changeCourseData(map, data);

	auto const terrain = buildRouteTerrain(map, RouteSettings{});
	RouteSearch search(terrain, RouteSettings{}, 10000);
	StoredRoutes store;
	analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search, store);
	analyseCourseRoutes(map, map.courseData(), QStringLiteral("B"), -1, search, store);
	data = map.courseData();
	data.routes = store;
	changeCourseData(map, data);
}

int pagesOf(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return -1;
	// Not fromLatin1(QByteArray), which ends at the first NUL of the binary file.
	auto const bytes = file.readAll();
	auto const text = QString::fromLatin1(bytes.constData(), bytes.size());
	return text.count(QRegularExpression(QStringLiteral("/Type\\s*/Page(?![s])")));
}

/// Whether some pixel within a radius in mm of a place is dark; the image has 10 pixels for a mm.
bool darkNear(const QImage& image, const MapCoordF& place, double radius)
{
	for (int y = int((place.y() - radius) * 10); y <= int((place.y() + radius) * 10); ++y)
	{
		for (int x = int((place.x() - radius) * 10); x <= int((place.x() + radius) * 10); ++x)
		{
			if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
				continue;
			if (qGray(image.pixel(x, y)) < 160)
				return true;
		}
	}
	return false;
}

}  // namespace



/**
 * @test Tests the print of the routes of the route analysis.
 *
 * See the design notes, "Route analysis: a print of the routes".
 */
class RoutePrintTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void captionsTest();
	void pagesTest();
	void withoutCoursesTest();
	void runnerTest();
};


void RoutePrintTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RoutePrintTest::captionsTest()
{
	CourseRoutes routes;
	routes.course = QStringLiteral("A");
	routes.analysed = true;
	CourseRouteLeg long_leg;
	long_leg.from = QStringLiteral("S1");
	long_leg.to = QStringLiteral("31");
	long_leg.route.found = true;
	long_leg.route.points = { MapCoordF(2.0, 5.0), MapCoordF(32.0, 5.0) };
	long_leg.route.length_m = 300.0;
	long_leg.route.time_s = 90.0;
	CourseRouteLeg short_leg;
	short_leg.from = QStringLiteral("31");
	short_leg.to = QStringLiteral("F1");
	short_leg.route.found = true;
	short_leg.route.points = { MapCoordF(32.0, 5.0), MapCoordF(32.0, 15.0) };
	short_leg.route.length_m = 100.0;
	short_leg.route.time_s = 30.0;
	routes.legs = { long_leg, short_leg };

	QImage image(400, 200, QImage::Format_RGB32);
	image.fill(Qt::white);
	{
		QPainter painter(&image);
		painter.scale(10.0, 10.0);
		paintRouteCaptions(&painter, routes, false);
	}
	auto const place = OcdCourse::routeCaptionPlace(long_leg.route.points);
	QVERIFY(darkNear(image, place.anchor, 3.0));
	QVERIFY(!darkNear(image, MapCoordF(32.0, 10.0), 2.0));
	// The line itself is not painted by the captions.
	QVERIFY(!darkNear(image, MapCoordF(5.0, 5.0), 0.3));
}


/// Accept (2)
void RoutePrintTest::pagesTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);

	RouteMapOptions options;
	options.path = QDir(dir.path()).absoluteFilePath(QStringLiteral("all.pdf"));
	auto report = exportRoutesMap(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.pages, 2);
	QCOMPARE(pagesOf(options.path), 2);

	options.path = QDir(dir.path()).absoluteFilePath(QStringLiteral("b.pdf"));
	options.courses = QStringList{ QStringLiteral("B") };
	report = exportRoutesMap(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.pages, 1);
	QCOMPARE(pagesOf(options.path), 1);

	options.path = QDir(dir.path()).absoluteFilePath(QStringLiteral("c.pdf"));
	options.courses = QStringList{ QStringLiteral("C") };
	report = exportRoutesMap(map, options);
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	QVERIFY(!QFileInfo::exists(options.path));
}


/// Accept (3)
void RoutePrintTest::withoutCoursesTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	RouteMapOptions options;
	options.courses = QStringList{ QStringLiteral("A") };
	options.path = QDir(dir.path()).absoluteFilePath(QStringLiteral("captions.pdf"));
	QVERIFY(exportRoutesMap(map, options).ok);
	options.captions = false;
	options.path = QDir(dir.path()).absoluteFilePath(QStringLiteral("plain.pdf"));
	QVERIFY(exportRoutesMap(map, options).ok);
	auto const with_captions = QFileInfo(QDir(dir.path()).absoluteFilePath(QStringLiteral("captions.pdf"))).size();
	auto const plain = QFileInfo(options.path).size();
	QVERIFY2(with_captions > plain + 100, qPrintable(QString::number(with_captions) + QLatin1Char('/') + QString::number(plain)));
	for (int i = 0; i < map.getNumSymbols(); ++i)
		QVERIFY(!map.getSymbol(i)->isHidden());
}


/// Accept (4)
void RoutePrintTest::runnerTest()
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
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-routes-map") }, { QStringLiteral("out"), QStringLiteral("routes.pdf") } });
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
	QVERIFY2(output.contains("\"pages\":2"), output.constData());
	QCOMPARE(pagesOf(QDir(dir.path()).absoluteFilePath(QStringLiteral("routes.pdf"))), 2);
}


QTEST_MAIN(RoutePrintTest)
#include "route_print_t.moc"  // IWYU pragma: keep
