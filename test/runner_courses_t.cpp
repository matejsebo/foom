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
#include <initializer_list>

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_storage.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
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

bool loadMap(Map& map, const QString& path)
{
	XMLFileFormat format;
	auto importer = format.makeImporter(path, &map, nullptr);
	return importer && importer->doImport();
}

Georeferencing utm32Georeferencing()
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	return georef;
}

struct Symbols
{
	const Symbol* start = nullptr;
	const Symbol* control = nullptr;
	const Symbol* finish = nullptr;
};

/// The scale, the georeferencing and the symbols of start, control and finish.
Symbols fillSymbols(Map& map)
{
	map.setScaleDenominator(10000);
	map.setGeoreferencing(utm32Georeferencing());
	Symbols symbols;
	auto const add = [&map](int number, Symbol::CourseRole role) {
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, number);
		symbol->setCourseRole(role);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	};
	symbols.start = add(701, Symbol::CourseStart);
	symbols.control = add(703, Symbol::CourseControl);
	symbols.finish = add(706, Symbol::CourseFinish);
	return symbols;
}

/// Adds a point object as the tool which draws points does, so that it becomes a course object.
void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

/// The map of Accept (13).
void fillEventMap(Map& map)
{
	auto const symbols = fillSymbols(map);
	drawPoint(map, symbols.start, 0.0, 0.0);
	drawPoint(map, symbols.control, 10.0, 0.0);
	drawPoint(map, symbols.control, 20.0, 0.0);
	drawPoint(map, symbols.finish, 30.0, 0.0);
	auto data = map.courseData();
	auto const addCourseOf = [&data](const QString& name, const QString& codes) {
		QVERIFY(addCourse(data, name));
		auto index = 0;
		for (auto const& code : codes.split(QLatin1Char(' ')))
			QVERIFY(insertCourseItem(data, name, index++, code));
	};
	addCourseOf(QStringLiteral("A"), QStringLiteral("S1 31 32 F1"));
	addCourseOf(QStringLiteral("B"), QStringLiteral("S1 32 F1"));
	CourseClass course_class;
	course_class.name = QStringLiteral("H21");
	course_class.course = QStringLiteral("A");
	course_class.runners = 40;
	data.classes.push_back(course_class);
	changeCourseData(map, data);
}

struct JobResult
{
	int exit_code = -1;
	QByteArray out;
	QByteArray err;
	
	QByteArray all() const { return out + err; }
	
	/// The log line of the last step with the name.
	QJsonObject logOf(const char* step) const
	{
		QJsonObject result;
		for (auto const& line : out.split('\n'))
		{
			auto const object = QJsonDocument::fromJson(line).object();
			if (object.value(QLatin1String("step")).toString() == QLatin1String(step))
				result = object;
		}
		return result;
	}
};

QJsonObject stepOf(const char* name, QJsonObject params)
{
	params.insert(QStringLiteral("step"), QString::fromLatin1(name));
	return params;
}

}  // namespace



/**
 * @test Tests the steps `export-courses` and `import-courses` of the headless runner.
 *
 * See the design notes, "Headless runner", Accept (13).
 */
class RunnerCoursesTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString event_path;
	QString blank_path;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	QByteArray readFile(const char* name) const
	{
		QFile f(file(name));
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}
	
	JobResult runJob(const char* name, const QList<QJsonObject>& steps) const
	{
		QJsonArray array;
		for (auto const& step : steps)
			array.append(step);
		auto const job_path = file(name);
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return {};
			job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), array } }).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return {};
		return { process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError() };
	}
	
	QJsonObject openEvent() const
	{
		return stepOf("open", {{ QStringLiteral("path"), event_path }});
	}
	
private slots:
	void initTestCase();
	void iofTest();
	void oneCourseTest();
	void perCourseTest();
	void missingCourseTest();
	void importTest();
	void importChoicesTest();
};


void RunnerCoursesTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QVERIFY(dir.isValid());
	event_path = file("event.foom");
	blank_path = file("blank.foom");
	{
		Map map;
		fillEventMap(map);
		QCOMPARE(int(map.courseData().objects.size()), 4);
		QVERIFY(saveMap(map, event_path));
	}
	Map blank;
	fillSymbols(blank);
	QVERIFY(saveMap(blank, blank_path));
}


/// Accept (13): IOF XML of all courses, by the extension.
void RunnerCoursesTest::iofTest()
{
	auto const result = runJob("iof.json", { openEvent(), stepOf("export-courses", {{ QStringLiteral("path"), QStringLiteral("courses.xml") }}) });
	QVERIFY2(result.exit_code == 0, result.all().constData());
	auto const xml = readFile("courses.xml");
	QVERIFY(xml.contains("<CourseData"));
	QCOMPARE(xml.count("<Course>"), 2);
	QCOMPARE(xml.count("<ClassCourseAssignment"), 1);
	QCOMPARE(result.logOf("export-courses").value(QLatin1String("files")).toArray().size(), 1);
}


/// Accept (13): GPX of one course.
void RunnerCoursesTest::oneCourseTest()
{
	auto const result = runJob("one.json", { openEvent(), stepOf("export-courses", {
		{ QStringLiteral("path"), QStringLiteral("b.gpx") },
		{ QStringLiteral("course"), QStringLiteral("B") },
	}) });
	QVERIFY2(result.exit_code == 0, result.all().constData());
	auto const gpx = readFile("b.gpx");
	QCOMPARE(gpx.count("<trk>"), 1);
	QVERIFY(gpx.contains("<name>B</name>"));
	QCOMPARE(gpx.count("<wpt "), 3);
}


/// Accept (13): KML, one file for every course.
void RunnerCoursesTest::perCourseTest()
{
	auto const result = runJob("per.json", { openEvent(), stepOf("export-courses", {
		{ QStringLiteral("path"), QStringLiteral("c.kml") },
		{ QStringLiteral("per_course"), true },
	}) });
	QVERIFY2(result.exit_code == 0, result.all().constData());
	QVERIFY(QFileInfo::exists(file("c.A.kml")));
	QVERIFY(QFileInfo::exists(file("c.B.kml")));
	QVERIFY(!QFileInfo::exists(file("c.kml")));
	QCOMPARE(result.logOf("export-courses").value(QLatin1String("files")).toArray().size(), 2);
}


/// Accept (13): a course which the map has not.
void RunnerCoursesTest::missingCourseTest()
{
	auto const result = runJob("missing.json", { openEvent(), stepOf("export-courses", {
		{ QStringLiteral("path"), QStringLiteral("z.gpx") },
		{ QStringLiteral("course"), QStringLiteral("Z") },
	}) });
	QCOMPARE(result.exit_code, 1);
	QVERIFY(!QFileInfo::exists(file("z.gpx")));
	QVERIFY(result.logOf("export-courses").value(QLatin1String("message")).toString().contains(QLatin1String("Z")));
}


/// Accept (13): the IOF XML read into a map with the symbols alone.
void RunnerCoursesTest::importTest()
{
	auto const result = runJob("import.json", {
		openEvent(),
		stepOf("export-courses", {{ QStringLiteral("path"), QStringLiteral("round.xml") }}),
		stepOf("open", {{ QStringLiteral("path"), blank_path }}),
		stepOf("import-courses", {{ QStringLiteral("path"), QStringLiteral("round.xml") }}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("imported.foom") }}),
	});
	QVERIFY2(result.exit_code == 0, result.all().constData());
	auto const log = result.logOf("import-courses");
	QCOMPARE(log.value(QLatin1String("objects_added")).toInt(), 4);
	QCOMPARE(log.value(QLatin1String("courses_added")).toInt(), 2);
	QCOMPARE(log.value(QLatin1String("classes_added")).toInt(), 1);
	
	Map map;
	QVERIFY(loadMap(map, file("imported.foom")));
	auto const& data = map.courseData();
	QCOMPARE(int(data.objects.size()), 4);
	QCOMPARE(int(data.courses.size()), 2);
	QCOMPARE(int(data.classes.size()), 1);
	auto const* control = data.findObject(QStringLiteral("32"));
	QVERIFY(control);
	auto const* object = map.findObject(control->object);
	QVERIFY(object);
	auto const place = object->asPoint()->getCoordF();
	QVERIFY(std::abs(place.x() - 20.0) < 0.01);
	QVERIFY(std::abs(place.y()) < 0.01);
}


/// Accept (13): the three choices of the import.
void RunnerCoursesTest::importChoicesTest()
{
	auto const result = runJob("choices.json", {
		openEvent(),
		stepOf("export-courses", {{ QStringLiteral("path"), QStringLiteral("choices.xml") }}),
		stepOf("open", {{ QStringLiteral("path"), blank_path }}),
		stepOf("import-courses", {
			{ QStringLiteral("path"), QStringLiteral("choices.xml") },
			{ QStringLiteral("courses"), false },
			{ QStringLiteral("classes"), false },
		}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("objects-only.foom") }}),
	});
	QVERIFY2(result.exit_code == 0, result.all().constData());
	Map map;
	QVERIFY(loadMap(map, file("objects-only.foom")));
	QCOMPARE(int(map.courseData().objects.size()), 4);
	QVERIFY(map.courseData().courses.empty());
	QVERIFY(map.courseData().classes.empty());
	
	// A file which is no CourseData fails the step.
	auto const refused = runJob("refused.json", {
		stepOf("open", {{ QStringLiteral("path"), blank_path }}),
		stepOf("import-courses", {{ QStringLiteral("path"), QStringLiteral("event.foom") }}),
	});
	QCOMPARE(refused.exit_code, 1);
}


QTEST_MAIN(RunnerCoursesTest)
#include "runner_courses_t.moc"  // IWYU pragma: keep
