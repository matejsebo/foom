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
#include <memory>

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
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
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
#include "course/course_storage.h"
#include "course/route_store.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_map.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

using namespace OpenOrienteering;


namespace {

bool saveMap(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

void drawBox(Map& map, const Symbol* symbol, double x1, double y1, double x2, double y2)
{
	auto* box = new PathObject(symbol);
	box->addCoordinate(MapCoord(x1, y1));
	box->addCoordinate(MapCoord(x2, y1));
	box->addCoordinate(MapCoord(x2, y2));
	box->addCoordinate(MapCoord(x1, y2));
	box->parts().back().setClosed(true);
	map.addObject(box);
}

/// The ground: yellow open land of 20 by 10 mm with a black building of 1 by 6 mm in its middle.
void fillGround(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
	yellow->setCmyk(MapColorCmyk(0.0f, 0.2f, 1.0f, 0.0f));
	map.addColor(yellow, 1);
	auto* open = new AreaSymbol();
	open->setNumberComponent(0, 401);
	open->setColor(yellow);
	map.addSymbol(open, 0);
	auto* building = new AreaSymbol();
	building->setNumberComponent(0, 521);
	building->setColor(black);
	map.addSymbol(building, 1);
	drawBox(map, open, 0.0, 0.0, 20.0, 10.0);
	drawBox(map, building, 9.5, 2.0, 10.5, 8.0);
}

/// The course file: a start and a control on the two sides of the building, the course A, and the ground as a background map.
void fillEvent(Map& map, const QString& ground_path)
{
	map.setScaleDenominator(10000);
	auto* start = new PointSymbol();
	start->setNumberComponent(0, 701);
	start->setCourseRole(Symbol::CourseStart);
	map.addSymbol(start, 0);
	auto* control = new PointSymbol();
	control->setNumberComponent(0, 703);
	control->setCourseRole(Symbol::CourseControl);
	map.addSymbol(control, 1);
	auto const drawPoint = [&map](const Symbol* symbol, double x, double y) {
		auto* object = new PointObject(symbol);
		object->setPosition(MapCoordF(x, y));
		auto const index = map.addObject(object);
		auto* step = new DeleteObjectsUndoStep(&map);
		step->addObject(index);
		map.push(step);
	};
	drawPoint(start, 4.0, 5.0);
	drawPoint(control, 16.0, 5.0);
	auto data = map.courseData();
	Course course;
	course.name = QStringLiteral("A");
	CourseItem item;
	item.type = CourseItem::Start;
	item.code = QStringLiteral("S1");
	course.items.push_back(item);
	item.type = CourseItem::Control;
	item.code = QStringLiteral("31");
	course.items.push_back(item);
	data.courses.push_back(course);
	changeCourseData(map, data);
	
	// Behind the map.
	map.addTemplate(-1, std::make_unique<TemplateMap>(ground_path, &map));
}

int runJob(const QString& job_path, const QJsonArray& steps, QByteArray& output)
{
	{
		QFile job_file(job_path);
		if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return -1;
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	QProcess process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process.setProcessEnvironment(env);
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	if (!process.waitForFinished(120000))
		return -1;
	output = process.readAllStandardOutput();
	output += process.readAllStandardError();
	return process.exitCode();
}

QJsonObject openStep(const QString& path)
{
	return QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), path } };
}

}  // namespace



/**
 * @test Tests that the runner, which has no view, loads the files of the
 * templates for what needs them.
 *
 * See the design notes, "Headless runner", Accept (11).
 */
class RunnerTemplateTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString ground_path;
	QString event_path;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
private slots:
	void initTestCase();
	void printTest();
	void courseMapTest();
	void routesTest();
};


void RunnerTemplateTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QVERIFY(dir.isValid());
	ground_path = file("ground.foom");
	event_path = file("event.foom");
	{
		Map ground;
		fillGround(ground);
		QVERIFY(saveMap(ground, ground_path));
	}
	Map event;
	fillEvent(event, ground_path);
	QCOMPARE(event.getNumTemplates(), 1);
	QCOMPARE(event.getFirstFrontTemplate(), 1);
	QVERIFY(saveMap(event, event_path));
}


/// Accept (11): an image with the templates.
void RunnerTemplateTest::printTest()
{
	QByteArray output;
	QJsonArray steps;
	steps.push_back(openStep(event_path));
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export") }, { QStringLiteral("path"), QStringLiteral("with.png") },
	                             { QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 } }, { QStringLiteral("dpi"), 254 },
	                             { QStringLiteral("templates"), true } });
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export") }, { QStringLiteral("path"), QStringLiteral("without.png") },
	                             { QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 } }, { QStringLiteral("dpi"), 254 },
	                             { QStringLiteral("templates"), false } });
	QVERIFY2(runJob(file("print.json"), steps, output) == 0, output.constData());
	
	// 10 pixels for a millimetre.
	QImage with(file("with.png"));
	QVERIFY2(!with.isNull(), output.constData());
	QCOMPARE(with.width(), 200);
	auto const land = QColor(with.pixel(40, 50));
	QVERIFY2(land.red() > 200 && land.green() > 160 && land.blue() < 90, qPrintable(land.name()));
	auto const building = QColor(with.pixel(100, 50));
	QVERIFY2(building.red() < 60 && building.green() < 60 && building.blue() < 60, qPrintable(building.name()));
	
	QImage without(file("without.png"));
	QVERIFY(!without.isNull());
	QCOMPARE(QColor(without.pixel(40, 50)), QColor(Qt::white));
	QCOMPARE(QColor(without.pixel(100, 50)), QColor(Qt::white));
}


/// Accept (11): the map of a course has its background map.
void RunnerTemplateTest::courseMapTest()
{
	// The same course file without the background map, to compare with.
	auto const bare_path = file("bare.foom");
	{
		Map bare;
		XMLFileFormat format;
		auto importer = format.makeImporter(event_path, &bare, nullptr);
		QVERIFY(importer && importer->doImport());
		QCOMPARE(bare.getNumTemplates(), 1);
		bare.deleteTemplate(0);
		QVERIFY(saveMap(bare, bare_path));
	}
	QByteArray output;
	for (auto const& path : { event_path, bare_path })
	{
		QJsonArray steps;
		steps.push_back(openStep(path));
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("maps") } });
		QVERIFY2(runJob(file("maps.json"), steps, output) == 0, output.constData());
	}
	auto const with = QFileInfo(file("maps/event.A.pdf"));
	auto const without = QFileInfo(file("maps/bare.A.pdf"));
	QVERIFY2(with.exists() && without.exists(), output.constData());
	QVERIFY2(with.size() > without.size() + 100, qPrintable(QString::number(with.size()) + QLatin1Char('/') + QString::number(without.size())));
}


/// Accept (11): the routes go around what the background map has.
void RunnerTemplateTest::routesTest()
{
	QByteArray output;
	QJsonArray steps;
	steps.push_back(openStep(event_path));
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("analyse-routes") } });
	steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("save-as") }, { QStringLiteral("path"), QStringLiteral("analysed.foom") } });
	QVERIFY2(runJob(file("routes.json"), steps, output) == 0, output.constData());
	QVERIFY2(output.contains("\"routes\":1"), output.constData());
	QVERIFY2(output.contains("\"missing\":0"), output.constData());
	
	Map analysed;
	XMLFileFormat format;
	auto importer = format.makeImporter(file("analysed.foom"), &analysed, nullptr);
	QVERIFY(importer && importer->doImport());
	auto const* route = analysed.courseData().routes.find(QStringLiteral("S1"), QStringLiteral("31"));
	QVERIFY(route);
	QVERIFY(route->found);
	// 120 m straight through the building; around its end, which is 30 m beside the line, about 134 m.
	QVERIFY2(route->length_m > 128.0 && route->length_m < 142.0, qPrintable(QString::number(route->length_m)));
	auto beside = 0.0;
	for (auto const& point : route->points)
		beside = qMax(beside, std::abs(point.y() - 5.0));
	QVERIFY2(beside > 2.9, qPrintable(QString::number(beside)));
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RunnerTemplateTest)
#include "runner_template_t.moc"  // IWYU pragma: keep
