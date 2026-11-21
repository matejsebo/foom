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
#include <QList>
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
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_report.h"
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

void drawLine(Map& map, const Symbol* symbol, std::initializer_list<MapCoord> coords)
{
	auto* line = new PathObject(symbol);
	for (auto const& coord : coords)
		line->addCoordinate(coord);
	map.addObject(line);
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

PointSymbol* addCourseSymbol(Map& map, int number, Symbol::CourseRole role)
{
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

/// The map of Accept (12).
void fillCheckMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* unused_color = new MapColor(QStringLiteral("Unused"), 1);
	unused_color->setCmyk(MapColorCmyk(0.0f, 1.0f, 0.0f, 0.0f));
	map.addColor(unused_color, 1);
	
	auto* line = new LineSymbol();
	line->setNumberComponent(0, 1);
	line->setColor(black);
	line->setLineWidth(0.5);
	line->setMinimumLength(2000);
	map.addSymbol(line, 0);
	auto* unused_symbol = new LineSymbol();
	unused_symbol->setNumberComponent(0, 2);
	unused_symbol->setColor(black);
	unused_symbol->setLineWidth(0.5);
	map.addSymbol(unused_symbol, 1);
	
	drawLine(map, line, { MapCoord(0.0, 0.0), MapCoord(1.0, 0.0) });
	drawLine(map, line, { MapCoord(0.0, 20.0), MapCoord(0.0, 20.0), MapCoord(10.0, 20.0) });
	
	auto const* start = addCourseSymbol(map, 701, Symbol::CourseStart);
	auto const* control = addCourseSymbol(map, 703, Symbol::CourseControl);
	auto const* finish = addCourseSymbol(map, 706, Symbol::CourseFinish);
	drawPoint(map, start, 0.0, 50.0);
	drawPoint(map, control, 10.0, 50.0);
	drawPoint(map, control, 20.0, 50.0);
	drawPoint(map, finish, 30.0, 50.0);
	
	auto data = map.courseData();
	auto const addCourseOf = [&data](const QString& name, const QString& codes, int climb) {
		QVERIFY(addCourse(data, name));
		auto index = 0;
		for (auto const& code : codes.split(QLatin1Char(' ')))
			QVERIFY(insertCourseItem(data, name, index++, code));
		data.courses.back().climb_m = climb;
		data.courses.back().climb_manual = true;
	};
	addCourseOf(QStringLiteral("A"), QStringLiteral("S1 31 F1"), 0);
	addCourseOf(QStringLiteral("D"), QStringLiteral("31"), 10);
	changeCourseData(map, data);
}

struct JobResult
{
	int exit_code = -1;
	QByteArray out;
	QByteArray err;
	
	/// The log line of the step with the name.
	QJsonObject logOf(const char* step) const
	{
		for (auto const& line : out.split('\n'))
		{
			auto const object = QJsonDocument::fromJson(line).object();
			if (object.value(QLatin1String("step")).toString() == QLatin1String(step))
				return object;
		}
		return {};
	}
};

QJsonObject stepOf(const char* name, QJsonObject params)
{
	params.insert(QStringLiteral("step"), QString::fromLatin1(name));
	return params;
}

/// The issues of the JSON file with the rule.
QList<QJsonObject> issuesOf(const QJsonArray& issues, const char* rule)
{
	QList<QJsonObject> result;
	for (auto const& value : issues)
	{
		auto const issue = value.toObject();
		if (issue.value(QLatin1String("rule")).toString() == QLatin1String(rule))
			result.push_back(issue);
	}
	return result;
}

int csvLinesOf(const QByteArray& csv, const char* rule)
{
	auto count = 0;
	for (auto const& line : csv.split('\n'))
		count += line.startsWith(QByteArray(rule) + ',') ? 1 : 0;
	return count;
}

}  // namespace



/**
 * @test Tests the steps `check` and `course-statistics` of the headless runner.
 *
 * See the design notes, "Headless runner", Accept (12).
 */
class RunnerCheckTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString map_path;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	QByteArray readFile(const char* name) const
	{
		QFile f(file(name));
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}
	
	JobResult runJob(const char* name, QList<QJsonObject> steps) const
	{
		steps.prepend(stepOf("open", {{ QStringLiteral("path"), map_path }}));
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
	
private slots:
	void initTestCase();
	void mapCheckTest();
	void legibilityAndCoursesTest();
	void ruleFilterTest();
	void failOnTest();
	void statisticsTest();
};


void RunnerCheckTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QVERIFY(dir.isValid());
	map_path = file("check.foom");
	Map map;
	fillCheckMap(map);
	QCOMPARE(int(map.courseData().objects.size()), 4);
	QVERIFY(saveMap(map, map_path));
}


/// Accept (12): the map check by default, into CSV.
void RunnerCheckTest::mapCheckTest()
{
	auto const result = runJob("map.json", { stepOf("check", {{ QStringLiteral("out"), QStringLiteral("map.csv") }}) });
	QVERIFY2(result.exit_code == 0, QByteArray(result.out + result.err).constData());
	
	auto const csv = readFile("map.csv");
	QVERIFY(csv.startsWith("rule,severity,objects,x,y,measured,limit,message\n"));
	QCOMPARE(csvLinesOf(csv, "vertex-duplicate"), 1);
	QCOMPARE(csvLinesOf(csv, "symbol-unused"), 1);
	QCOMPARE(csvLinesOf(csv, "color-unused"), 1);
	QCOMPARE(csvLinesOf(csv, "line-short"), 0);
	QCOMPARE(csvLinesOf(csv, "course-climb-zero"), 0);
	QCOMPARE(csvLinesOf(csv, "control-unused"), 0);
	
	auto const log = result.logOf("check");
	QCOMPARE(log.value(QLatin1String("ok")).toBool(), true);
	QCOMPARE(log.value(QLatin1String("issues")).toInt(), csv.count('\n') - 1);
	QCOMPARE(log.value(QLatin1String("rules")).toObject().value(QLatin1String("vertex-duplicate")).toInt(), 1);
}


/// Accept (12): the legibility scan and the courses, into JSON.
void RunnerCheckTest::legibilityAndCoursesTest()
{
	auto const result = runJob("legibility.json", { stepOf("check", {
		{ QStringLiteral("rules"), QJsonArray{ QStringLiteral("legibility"), QStringLiteral("courses") } },
		{ QStringLiteral("out"), QStringLiteral("issues.json") },
	}) });
	QVERIFY2(result.exit_code == 0, QByteArray(result.out + result.err).constData());
	
	auto const issues = QJsonDocument::fromJson(readFile("issues.json")).object().value(QLatin1String("issues")).toArray();
	QVERIFY(!issues.isEmpty());
	
	auto found = issuesOf(issues, "line-short");
	QCOMPARE(found.size(), 1);
	QVERIFY(std::abs(found[0].value(QLatin1String("measured")).toDouble() - 1.0) < 1e-6);
	QVERIFY(std::abs(found[0].value(QLatin1String("limit")).toDouble() - 2.0) < 1e-6);
	QCOMPARE(found[0].value(QLatin1String("severity")).toString(), QStringLiteral("warning"));
	QCOMPARE(found[0].value(QLatin1String("objects")).toArray().size(), 1);
	
	found = issuesOf(issues, "course-start-missing");
	QCOMPARE(found.size(), 1);
	QCOMPARE(found[0].value(QLatin1String("subject")).toString(), QStringLiteral("D"));
	QCOMPARE(found[0].value(QLatin1String("severity")).toString(), QStringLiteral("error"));
	found = issuesOf(issues, "course-finish-missing");
	QCOMPARE(found.size(), 1);
	QCOMPARE(found[0].value(QLatin1String("subject")).toString(), QStringLiteral("D"));
	found = issuesOf(issues, "course-climb-zero");
	QCOMPARE(found.size(), 1);
	QCOMPARE(found[0].value(QLatin1String("subject")).toString(), QStringLiteral("A"));
	found = issuesOf(issues, "control-unused");
	QCOMPARE(found.size(), 1);
	QCOMPARE(found[0].value(QLatin1String("subject")).toString(), QStringLiteral("32"));
	
	for (auto const* rule : { "vertex-duplicate", "symbol-unused", "color-unused" })
		QVERIFY2(issuesOf(issues, rule).isEmpty(), rule);
	
	// The counts of the log line.
	auto const log = result.logOf("check");
	QCOMPARE(log.value(QLatin1String("issues")).toInt(), issues.size());
	QCOMPARE(log.value(QLatin1String("errors")).toInt() + log.value(QLatin1String("warnings")).toInt() + log.value(QLatin1String("infos")).toInt(), issues.size());
	QVERIFY(log.value(QLatin1String("errors")).toInt() >= 2);
	QCOMPARE(log.value(QLatin1String("rules")).toObject().value(QLatin1String("line-short")).toInt(), 1);
}


/// Accept (12): rule ids keep only their rules.
void RunnerCheckTest::ruleFilterTest()
{
	auto const result = runJob("filter.json", { stepOf("check", {
		{ QStringLiteral("rules"), QJsonArray{ QStringLiteral("vertex-duplicate"), QStringLiteral("control-unused") } },
		{ QStringLiteral("out"), QStringLiteral("filter.json") },
	}) });
	QVERIFY2(result.exit_code == 0, QByteArray(result.out + result.err).constData());
	
	auto const issues = QJsonDocument::fromJson(readFile("filter.json")).object().value(QLatin1String("issues")).toArray();
	QCOMPARE(issues.size(), 2);
	QCOMPARE(issuesOf(issues, "vertex-duplicate").size(), 1);
	QCOMPARE(issuesOf(issues, "control-unused").size(), 1);
}


/// Accept (12): fail_on stops the job after the file is written.
void RunnerCheckTest::failOnTest()
{
	auto result = runJob("fail.json", {
		stepOf("check", {
			{ QStringLiteral("rules"), QJsonArray{ QStringLiteral("courses") } },
			{ QStringLiteral("out"), QStringLiteral("fail.json") },
			{ QStringLiteral("fail_on"), QStringLiteral("error") },
		}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("after-fail.foom") }}),
	});
	QCOMPARE(result.exit_code, 1);
	QVERIFY(QFileInfo::exists(file("fail.json")));
	QVERIFY(!QFileInfo::exists(file("after-fail.foom")));
	auto const log = result.logOf("check");
	QCOMPARE(log.value(QLatin1String("ok")).toBool(true), false);
	QVERIFY(!log.value(QLatin1String("message")).toString().isEmpty());
	
	result = runJob("pass.json", { stepOf("check", {
		{ QStringLiteral("rules"), QJsonArray{ QStringLiteral("legibility") } },
		{ QStringLiteral("fail_on"), QStringLiteral("error") },
	}) });
	QVERIFY2(result.exit_code == 0, QByteArray(result.out + result.err).constData());
	QCOMPARE(result.logOf("check").value(QLatin1String("warnings")).toInt(), 1);
}


/// Accept (12): the statistics as the dialog exports them.
void RunnerCheckTest::statisticsTest()
{
	auto const result = runJob("statistics.json", { stepOf("course-statistics", {{ QStringLiteral("out"), QStringLiteral("statistics.txt") }}) });
	QVERIFY2(result.exit_code == 0, QByteArray(result.out + result.err).constData());
	
	Map map;
	QVERIFY(loadMap(map, map_path));
	auto const expected = courseStatisticsText(courseStatistics(map, map.courseData())).toUtf8();
	QCOMPARE(readFile("statistics.txt"), expected);
	auto const log = result.logOf("course-statistics");
	QCOMPARE(log.value(QLatin1String("courses")).toInt(), 2);
	QCOMPARE(log.value(QLatin1String("controls")).toInt(), 2);
}


QTEST_MAIN(RunnerCheckTest)
#include "runner_check_t.moc"  // IWYU pragma: keep
