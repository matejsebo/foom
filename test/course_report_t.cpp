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
#include <initializer_list>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QUuid>
#include <QVector>

#include "global.h"
#include "check/issue.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_report.h"
#include "course/course_report_print.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/main_window.h"
#include "gui/map/course_report_dialog.h"
#include "gui/map/course_statistics_dialog.h"
#include "gui/map/map_editor.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

using namespace OpenOrienteering;


namespace {

QStringList names(std::initializer_list<const char*> list)
{
	QStringList result;
	for (auto const* name : list)
		result.push_back(QString::fromLatin1(name));
	return result;
}

PointSymbol* addSymbol(Map& map, int number, Symbol::CourseRole role)
{
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

/// Adds a point object as the tool which draws points does.
void draw(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

void addCourseOf(CourseData& data, const QString& name, const QString& codes, int climb)
{
	QVERIFY(addCourse(data, name));
	auto index = 0;
	for (auto const& code : codes.split(QLatin1Char(' '), QString::SkipEmptyParts))
		QVERIFY(insertCourseItem(data, name, index++, code));
	data.courses.back().climb_m = climb;
	data.courses.back().climb_manual = true;
}

void addClass(CourseData& data, const QString& name, const QString& course, int runners)
{
	CourseClass course_class;
	course_class.name = name;
	course_class.course = course;
	course_class.runners = runners;
	data.classes.push_back(course_class);
}

CourseItem item(CourseItem::Type type, const QString& code = {}, int count = 0)
{
	CourseItem result;
	result.type = type;
	result.code = code;
	result.count = count;
	return result;
}

CourseObject& objectOf(CourseData& data, const QString& code)
{
	for (auto& object : data.objects)
	{
		if (object.code == code)
			return object;
	}
	Q_UNREACHABLE();
}

/// The map of the acceptance criteria.
void fillReportMap(Map& map, unsigned int scale = 10000)
{
	map.setScaleDenominator(scale);
	auto const* start = addSymbol(map, 701, Symbol::CourseStart);
	auto const* control = addSymbol(map, 703, Symbol::CourseControl);
	auto const* finish = addSymbol(map, 706, Symbol::CourseFinish);
	draw(map, start, 0.0, 0.0);
	draw(map, control, 10.0, 0.0);
	draw(map, control, 20.0, 0.0);
	draw(map, control, 20.0, 10.0);
	draw(map, control, 22.0, 10.0);
	draw(map, control, 26.5, 10.0);
	draw(map, finish, 30.0, 0.0);
	
	auto data = map.courseData();
	addCourseOf(data, QStringLiteral("A"), QStringLiteral("S1 31 32 33 F1"), 50);
	addCourseOf(data, QStringLiteral("B"), QStringLiteral("S1 32 31 34 F1"), 0);
	addCourseOf(data, QStringLiteral("C"), QStringLiteral("S1 31 32 33 F1"), 20);
	addCourseOf(data, QStringLiteral("D"), QStringLiteral("31 32"), 10);
	Course relay;
	relay.name = QStringLiteral("R");
	relay.type = Course::Relay;
	relay.legs = 2;
	relay.items = {
	    item(CourseItem::Start, QStringLiteral("S1")),
	    item(CourseItem::TeamForkBegin, {}, 2),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("31")),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("32")),
	    item(CourseItem::TeamForkEnd),
	    item(CourseItem::Control, QStringLiteral("33")),
	    item(CourseItem::Finish, QStringLiteral("F1")),
	};
	data.courses.push_back(relay);
	addClass(data, QStringLiteral("H21"), QStringLiteral("A"), 40);
	addClass(data, QStringLiteral("H20"), QStringLiteral("A"), 10);
	addClass(data, QStringLiteral("D21"), QStringLiteral("B"), 30);
	
	using Column = CourseObject::Column;
	objectOf(data, QStringLiteral("32")).cell[Column::ColumnD] = 1010;
	objectOf(data, QStringLiteral("32")).cell[Column::ColumnE] = 5001;
	objectOf(data, QStringLiteral("33")).cell[Column::ColumnD] = 1010;
	objectOf(data, QStringLiteral("33")).cell[Column::ColumnF] = 10001;
	objectOf(data, QStringLiteral("34")).cell[Column::ColumnD] = 1010;
	objectOf(data, QStringLiteral("34")).cell[Column::ColumnE] = 8011;
	objectOf(data, QStringLiteral("35")).cell[Column::ColumnD] = 2004;
	objectOf(data, QStringLiteral("35")).cell[Column::ColumnE] = 3001;
	objectOf(data, QStringLiteral("35")).cell[Column::ColumnG] = 12014;
	objectOf(data, QStringLiteral("31")).punch_ids = names({ "131", "200" });
	objectOf(data, QStringLiteral("32")).punch_ids = names({ "131" });
	objectOf(data, QStringLiteral("33")).punch_ids = names({ "200" });
	objectOf(data, QStringLiteral("34")).punch_ids = names({ "131" });
	changeCourseData(map, data);
}

std::vector<Issue> issuesOf(const std::vector<Issue>& issues, const char* rule)
{
	std::vector<Issue> result;
	for (auto const& issue : issues)
	{
		if (issue.rule == rule)
			result.push_back(issue);
	}
	return result;
}

QUuid uuidOf(const Map& map, const QString& code)
{
	auto const* object = map.courseData().findObject(code);
	return object ? object->object : QUuid{};
}

}  // namespace



/**
 * @test Tests the statistics of the courses and the consistency check.
 *
 * See the design notes, "Statistics and the consistency check".
 */
class CourseReportTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void coursesTest();
	void objectsTest();
	void legsTest();
	void matrixTest();
	void eventTest();
	void textTest();
	void checkTest();
	void dialogTest();
	void editorTest();
	void startsAndRelaysTest();  // foom
	void printTest();
	void printDialogTest();
};


void CourseReportTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseReportTest::coursesTest()
{
	Map map;
	fillReportMap(map);
	auto const statistics = courseStatistics(map, map.courseData());
	auto const& courses = statistics.courses;
	QCOMPARE(int(courses.size()), 5);
	
	QCOMPARE(courses[0].name, QStringLiteral("A"));
	QCOMPARE(courses[0].classes, names({ "H21", "H20" }));
	QCOMPARE(courses[0].runners, 50);
	QVERIFY(courses[0].measured);
	QVERIFY(std::abs(courses[0].length_m - 441.4) < 0.1);
	QCOMPARE(courses[0].climb_m, 50);
	QCOMPARE(courses[0].controls, 3);
	QVERIFY(!courses[0].has_variation);
	
	QVERIFY(std::abs(courses[1].length_m - 584.3) < 0.1);
	QCOMPARE(courses[1].runners, 30);
	QCOMPARE(courses[2].runners, 0);
	QVERIFY(courses[2].classes.isEmpty());
	QVERIFY(std::abs(courses[3].length_m - 100.0) < 0.1);
	QCOMPARE(courses[3].controls, 2);
	
	QCOMPARE(courses[4].name, QStringLiteral("R"));
	QVERIFY(!courses[4].measured);
	QVERIFY(courses[4].has_variation);
	QCOMPARE(courses[4].type, Course::Relay);
	QCOMPARE(courses[4].controls, 3);
}


/// Accept (2)
void CourseReportTest::objectsTest()
{
	Map map;
	fillReportMap(map);
	auto const statistics = courseStatistics(map, map.courseData());
	auto const& objects = statistics.objects;
	QCOMPARE(int(objects.size()), 7);
	QStringList codes;
	for (auto const& object : objects)
		codes.push_back(object.code);
	QCOMPARE(codes, names({ "S1", "31", "32", "33", "34", "35", "F1" }));
	
	QCOMPARE(objects[1].courses, names({ "A", "B", "C", "D", "R" }));
	QCOMPARE(objects[1].runners, 80);
	QCOMPARE(objects[1].kind, CourseObject::Control);
	QVERIFY(objects[5].courses.isEmpty());
	QCOMPARE(objects[5].runners, 0);
	QCOMPARE(objects[0].courses, names({ "A", "B", "C", "R" }));
	QCOMPARE(objects[4].courses, names({ "B" }));
	QCOMPARE(objects[4].runners, 30);
}


/// Accept (3)
void CourseReportTest::legsTest()
{
	Map map;
	fillReportMap(map);
	auto const statistics = courseStatistics(map, map.courseData());
	auto const& legs = statistics.legs;
	QCOMPARE(int(legs.size()), 8);
	
	QCOMPARE(legs[0].from, QStringLiteral("31"));
	QCOMPARE(legs[0].to, QStringLiteral("32"));
	QVERIFY(std::abs(legs[0].length_m - 100.0) < 0.05);
	QCOMPARE(legs[0].courses, names({ "A", "C", "D" }));
	QCOMPARE(legs[0].runners, 50);
	
	auto shared = 0;
	auto found_back = false;
	for (auto const& leg : legs)
	{
		if (leg.courses.size() > 1)
			++shared;
		if (leg.from == QLatin1String("32") && leg.to == QLatin1String("31"))
		{
			found_back = true;
			QCOMPARE(leg.courses, names({ "B" }));
			QCOMPARE(leg.runners, 30);
		}
		QVERIFY(!leg.courses.contains(QStringLiteral("R")));
	}
	QCOMPARE(shared, 4);
	QVERIFY(found_back);
	// The most used first.
	for (std::size_t i = 1; i < legs.size(); ++i)
		QVERIFY(legs[i - 1].courses.size() >= legs[i].courses.size());
}


/// Accept (4)
void CourseReportTest::matrixTest()
{
	Map map;
	fillReportMap(map);
	auto const statistics = courseStatistics(map, map.courseData());
	QCOMPARE(statistics.matrix_controls, names({ "31", "32", "33", "34", "35" }));
	QCOMPARE(statistics.matrix_courses, names({ "A", "B", "C", "D", "R" }));
	QCOMPARE(int(statistics.matrix.size()), 5);
	QCOMPARE(statistics.matrix[0], names({ "1", "2", "1", "1", "x" }));
	QCOMPARE(statistics.matrix[1], names({ "2", "1", "2", "2", "x" }));
	QCOMPARE(statistics.matrix[2], names({ "3", "", "3", "", "x" }));
	QCOMPARE(statistics.matrix[3], names({ "", "3", "", "", "" }));
	QCOMPARE(statistics.matrix[4], names({ "", "", "", "", "" }));
	
	// A control which is visited twice.
	Map empty;
	CourseData data;
	for (auto const* code : { "31", "32" })
	{
		CourseObject object;
		object.kind = CourseObject::Control;
		object.code = QString::fromLatin1(code);
		data.objects.push_back(object);
	}
	addCourseOf(data, QStringLiteral("E"), QStringLiteral("31 32 31"), 0);
	auto const twice = courseStatistics(empty, data);
	QCOMPARE(twice.matrix[0], names({ "1/3" }));
	QCOMPARE(twice.matrix[1], names({ "2" }));
	// Without their objects in the map the legs have no length.
	QCOMPARE(int(twice.legs.size()), 2);
	QVERIFY(twice.legs[0].length_m < 0.0);
	QVERIFY(!twice.courses[0].measured);
}


/// Accept (5)
void CourseReportTest::eventTest()
{
	Map map;
	fillReportMap(map);
	auto const statistics = courseStatistics(map, map.courseData());
	QCOMPARE(statistics.control_count, 5);
	QCOMPARE(statistics.course_count, 5);
	QCOMPARE(statistics.class_count, 3);
	
	QCOMPARE(statisticsControlDistance(map), 60.0);
	QCOMPARE(checkControlDistance(map), 30.0);
	QCOMPARE(statistics.close_limit_m, 60.0);
	QCOMPARE(int(statistics.close_pairs.size()), 2);
	QCOMPARE(statistics.close_pairs[0].first, QStringLiteral("33"));
	QCOMPARE(statistics.close_pairs[0].second, QStringLiteral("34"));
	QVERIFY(std::abs(statistics.close_pairs[0].distance_m - 20.0) < 0.05);
	QCOMPARE(statistics.close_pairs[1].first, QStringLiteral("34"));
	QCOMPARE(statistics.close_pairs[1].second, QStringLiteral("35"));
	QVERIFY(std::abs(statistics.close_pairs[1].distance_m - 45.0) < 0.05);
	QCOMPARE(int(courseStatistics(map, map.courseData(), 30.0).close_pairs.size()), 1);
	
	QCOMPARE(int(statistics.opposite.size()), 1);
	QCOMPARE(statistics.opposite[0].first, QStringLiteral("31"));
	QCOMPARE(statistics.opposite[0].second, QStringLiteral("32"));
	QCOMPARE(statistics.opposite[0].forward, names({ "A", "C", "D" }));
	QCOMPARE(statistics.opposite[0].backward, names({ "B" }));
	
	QCOMPARE(int(statistics.identical.size()), 1);
	QCOMPARE(statistics.identical[0], names({ "A", "C" }));
	QCOMPARE(statistics.without_start_or_finish, names({ "D" }));
	QCOMPARE(statistics.not_verified, names({ "R" }));
	
	Map sprint;
	fillReportMap(sprint, 4000);
	QCOMPARE(statisticsControlDistance(sprint), 30.0);
	QCOMPARE(checkControlDistance(sprint), 25.0);
	// 33 and 34 are 8 m apart there, 34 and 35 18 m, 33 and 35 26 m.
	QCOMPARE(int(courseStatistics(sprint, sprint.courseData()).close_pairs.size()), 3);
}


/// Accept (6)
void CourseReportTest::textTest()
{
	Map map;
	fillReportMap(map);
	auto const text = courseStatisticsText(courseStatistics(map, map.courseData()));
	auto const lines = text.split(QLatin1Char('\n'));
	QVERIFY(lines.contains(QStringLiteral("31\t32\t100 m\t3\t50\tA, C, D")));
	QVERIFY(lines.contains(QStringLiteral("32\t31\t100 m\t1\t30\tB")));
	QVERIFY(lines.contains(QStringLiteral("33\t34\t20 m")));
	QVERIFY(lines.contains(QStringLiteral("34\t35\t45 m")));
	QVERIFY(lines.contains(QStringLiteral("A\tH21, H20\t50\t0.440 km\t50 m\t3")));
	QVERIFY(lines.contains(QStringLiteral("31\t1\t2\t1\t1\tx")));
	QVERIFY(lines.contains(QStringLiteral("35\t\t\t\t\t")));
	QVERIFY(lines.contains(QStringLiteral("31\t5\t80\tA, B, C, D, R")));
	QVERIFY(lines.contains(QStringLiteral("31\t32\tA, C, D\tB")));
	QVERIFY(lines.contains(QStringLiteral("A, C")));
	QVERIFY(lines.contains(QStringLiteral("D")));
	QVERIFY(lines.contains(QStringLiteral("R")));
	QVERIFY(!text.contains(QLatin1Char('\r')));
}


/// Accept (7)
void CourseReportTest::checkTest()
{
	Map map;
	fillReportMap(map);
	auto const issues = checkCourses(map, map.courseData());
	
	auto found = issuesOf(issues, rule::course_start_missing);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].severity, Issue::Error);
	QCOMPARE(found[0].subject, QStringLiteral("D"));
	found = issuesOf(issues, rule::course_finish_missing);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].severity, Issue::Error);
	QCOMPARE(found[0].subject, QStringLiteral("D"));
	
	found = issuesOf(issues, rule::course_climb_zero);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("B"));
	QCOMPARE(found[0].severity, Issue::Warning);
	
	found = issuesOf(issues, rule::course_identical);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(found[0].message.contains(QStringLiteral("A, C")));
	
	found = issuesOf(issues, rule::course_not_checked);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("R"));
	QCOMPARE(found[0].severity, Issue::Info);
	
	found = issuesOf(issues, rule::control_unused);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("35"));
	QCOMPARE(found[0].objects, QVector<QUuid>{ uuidOf(map, QStringLiteral("35")) });
	
	found = issuesOf(issues, rule::control_too_close);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].objects, (QVector<QUuid>{ uuidOf(map, QStringLiteral("33")), uuidOf(map, QStringLiteral("34")) }));
	QVERIFY(std::abs(found[0].measured - 20.0) < 0.05);
	QCOMPARE(found[0].limit, 30.0);
	QVERIFY(std::abs(found[0].where.x() - 21.0) < 0.01);
	QVERIFY(std::abs(found[0].where.y() - 10.0) < 0.01);
	
	found = issuesOf(issues, rule::leg_opposite);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].objects, (QVector<QUuid>{ uuidOf(map, QStringLiteral("31")), uuidOf(map, QStringLiteral("32")) }));
	QVERIFY(found[0].message.contains(QStringLiteral("A, C, D")));
	
	found = issuesOf(issues, rule::description_d_empty);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("31"));
	found = issuesOf(issues, rule::description_de_missing);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("33"));
	found = issuesOf(issues, rule::description_fg_missing);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("32"));
	QCOMPARE(found[0].objects, QVector<QUuid>{ uuidOf(map, QStringLiteral("32")) });
	
	found = issuesOf(issues, rule::punch_consecutive);
	QCOMPARE(int(found.size()), 5);
	QStringList courses;
	for (auto const& issue : found)
	{
		QCOMPARE(issue.severity, Issue::Error);
		courses.push_back(issue.subject);
	}
	for (int i = 0; i < 4; ++i)
		QVERIFY(found[std::size_t(i)].message.contains(QStringLiteral("131")));
	// foom: the design notes, "The consistency check: starts and relays":
	// the relay's variant through 31 has 200 at 31 and 33.
	QVERIFY(found[4].message.contains(QStringLiteral("200")));
	QVERIFY(found[4].message.contains(QStringLiteral("31-33")));
	QCOMPARE(courses, names({ "A", "B", "C", "D", "R" }));
	found = issuesOf(issues, rule::punch_repeated);
	QCOMPARE(int(found.size()), 2);
	QCOMPARE(found[0].severity, Issue::Warning);
	QVERIFY(found[0].message.contains(QStringLiteral("200")));
	QCOMPARE(found[0].subject, QStringLiteral("A"));
	QCOMPARE(found[1].subject, QStringLiteral("C"));
	
	QCOMPARE(int(issues.size()), 18);
	
	// Another limit.
	QCOMPARE(int(issuesOf(checkCourses(map, map.courseData(), 50.0), rule::control_too_close).size()), 2);
	
	// A map without course data has no issues.
	Map empty;
	QVERIFY(checkCourses(empty, empty.courseData()).empty());
}


/// Accept (8)
void CourseReportTest::dialogTest()
{
	Map map;
	fillReportMap(map);
	CourseStatisticsDialog dialog(&map);
	auto* courses = dialog.findChild<QTableWidget*>(QStringLiteral("course-stat-courses"));
	auto* objects = dialog.findChild<QTableWidget*>(QStringLiteral("course-stat-objects"));
	auto* legs = dialog.findChild<QTableWidget*>(QStringLiteral("course-stat-legs"));
	auto* matrix = dialog.findChild<QTableWidget*>(QStringLiteral("course-stat-matrix"));
	auto* event = dialog.findChild<QPlainTextEdit*>(QStringLiteral("course-stat-event"));
	auto* hide = dialog.findChild<QCheckBox*>(QStringLiteral("course-stat-hide-single"));
	QVERIFY(courses && objects && legs && matrix && event && hide);
	
	QCOMPARE(courses->rowCount(), 5);
	QCOMPARE(courses->item(0, 0)->text(), QStringLiteral("A"));
	QCOMPARE(objects->rowCount(), 7);
	QCOMPARE(objects->item(1, 0)->text(), QStringLiteral("31"));
	QCOMPARE(legs->rowCount(), 8);
	QCOMPARE(matrix->rowCount(), 5);
	QCOMPARE(matrix->columnCount(), 6);
	QCOMPARE(matrix->item(0, 2)->text(), QStringLiteral("2"));
	QVERIFY(!hide->isChecked());
	hide->setChecked(true);
	QCOMPARE(legs->rowCount(), 4);
	hide->setChecked(false);
	QCOMPARE(legs->rowCount(), 8);
	
	auto const event_text = event->toPlainText();
	QVERIFY(event_text.contains(QStringLiteral("33")));
	QVERIFY(event_text.contains(QStringLiteral("34")));
	QVERIFY(event_text.contains(QStringLiteral("20 m")));
	QVERIFY(event_text.contains(QStringLiteral("A, C")));
	
	auto const text = courseStatisticsText(courseStatistics(map, map.courseData()));
	QCOMPARE(dialog.text(), text);
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	auto const path = folder.filePath(QStringLiteral("statistics.txt"));
	QVERIFY(dialog.exportTo(path));
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto written = QString::fromUtf8(file.readAll());
	written.remove(QLatin1Char('\r'));
	QCOMPARE(written, text);
}


/// Accept (9)
void CourseReportTest::editorTest()
{
	auto* map = new Map();
	fillReportMap(*map);
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	
	QVERIFY(editor->getAction("course-statistics"));
	QVERIFY(editor->getAction("course-check"));
	
	auto const count = editor->runCourseCheck();
	QCOMPARE(count, 18);
	auto* model = editor->issueModel();
	QCOMPARE(model->rowCount(), 18);
	model->setRuleFilter(rule::control_too_close);
	QCOMPARE(model->rowCount(), 1);
	model->activate(0);
	QCOMPARE(map->getNumSelectedObjects(), 2);
	for (auto const* object : map->selectedObjects())
		QVERIFY(object->uuid() == uuidOf(*map, QStringLiteral("33")) || object->uuid() == uuidOf(*map, QStringLiteral("34")));
	
	// "Check again" repeats the check of the courses.
	model->setRuleFilter({});
	auto data = map->courseData();
	QVERIFY(removeCourse(data, QStringLiteral("D")));
	changeCourseData(*map, data);
	editor->checkAgain();
	QVERIFY(model->rowCount() < 18);
	QCOMPARE(int(issuesOf(model->issues(), rule::course_start_missing).size()), 0);
	
	window->deleteLater();
}



/// Accept (12), the design notes, "The consistency check: starts and relays"
void CourseReportTest::startsAndRelaysTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto const* start = addSymbol(map, 701, Symbol::CourseStart);
	auto const* control = addSymbol(map, 703, Symbol::CourseControl);
	auto const* finish = addSymbol(map, 706, Symbol::CourseFinish);
	draw(map, start, -100.0, 0.0);  // S1, far from the others
	draw(map, start, 0.0, 1.0);     // S2
	draw(map, start, 0.0, 1.5);     // S3, 5 m from S2
	draw(map, control, 0.0, 0.0);   // 31, 10 m from S2 and 15 m from S3
	draw(map, control, 50.0, 0.0);  // 32
	draw(map, control, 100.0, 0.0); // 33
	draw(map, finish, 150.0, 0.0);
	
	auto data = map.courseData();
	Course relay;
	relay.name = QStringLiteral("R");
	relay.type = Course::Relay;
	relay.legs = 2;
	relay.climb_m = 10;
	relay.items = {
	    item(CourseItem::Start, QStringLiteral("S1")),
	    item(CourseItem::TeamForkBegin, {}, 2),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("31")),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("32")),
	    item(CourseItem::TeamForkEnd),
	    item(CourseItem::Control, QStringLiteral("33")),
	    item(CourseItem::Finish, QStringLiteral("F1")),
	};
	data.courses.push_back(relay);
	objectOf(data, QStringLiteral("32")).punch_ids = names({ "300" });
	objectOf(data, QStringLiteral("33")).punch_ids = names({ "300" });
	changeCourseData(map, data);
	
	auto issues = checkCourses(map, map.courseData(), 12.0);
	auto found = issuesOf(issues, rule::control_too_close);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(found[0].message.startsWith(QStringLiteral("The start S2 and the control 31")));
	QVERIFY(std::abs(found[0].measured - 10.0) < 0.05);
	found = issuesOf(issues, rule::punch_consecutive);
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].subject, QStringLiteral("R"));
	QVERIFY(found[0].message.contains(QStringLiteral("32-33")));
	QVERIFY(found[0].message.contains(QStringLiteral("B")));  // the variant of 32
	found = issuesOf(issues, rule::course_not_checked);
	QCOMPARE(int(found.size()), 1);
	QVERIFY(!found[0].message.contains(QStringLiteral("punching")));
	
	data = map.courseData();
	objectOf(data, QStringLiteral("31")).punch_ids = names({ "300" });
	objectOf(data, QStringLiteral("33")).punch_ids.clear();
	changeCourseData(map, data);
	issues = checkCourses(map, map.courseData());
	QVERIFY(issuesOf(issues, rule::punch_consecutive).empty());
	QVERIFY(issuesOf(issues, rule::punch_repeated).empty());
}


/// Accept (11)
void CourseReportTest::printTest()
{
	Map map;
	fillReportMap(map);
	auto const issues = checkCourses(map, map.courseData());
	
	// Every rule of the check has its part.
	auto const sections = courseCheckSections();
	QCOMPARE(sections.size(), 8);
	for (auto const& issue : issues)
		QVERIFY2(courseCheckSection(issue.rule) >= 0, issue.rule.constData());
	QCOMPARE(courseCheckSection(rule::course_start_missing), 0);
	QCOMPARE(courseCheckSection(rule::control_unused), 1);
	QCOMPARE(courseCheckSection(rule::description_fg_missing), 2);
	QCOMPARE(courseCheckSection(rule::punch_repeated), 3);
	QCOMPARE(courseCheckSection(rule::relay_structure), 7);
	QCOMPARE(courseCheckSection("crt-sym"), -1);
	
	CourseReportOptions options;
	options.title = QStringLiteral("Spring <Cup>");
	options.date = QDate(2026, 11, 21);
	auto const html = courseReportHtml(map, map.courseData(), options);
	QVERIFY(html.contains(QStringLiteral("<h1>Spring &lt;Cup&gt;</h1>")));
	QVERIFY(html.contains(QStringLiteral("2026-11-21")));
	QVERIFY(html.contains(QStringLiteral("Errors: 7, warnings: 10, notes: 1.")));
	for (auto const& section : sections)
		QVERIFY2(html.contains(QLatin1String("<h3>") + section.toHtmlEscaped() + QLatin1String("</h3>")), qPrintable(section));
	for (auto const& issue : issues)
		QVERIFY2(html.contains(issue.message.toHtmlEscaped()), qPrintable(issue.message));
	// The issues stand in the order of the parts: the unused control before the descriptions.
	QVERIFY(html.indexOf(QStringLiteral("The control 35 is in no course.")) < html.indexOf(QStringLiteral("Control 31: column D")));
	
	// The statistics: the tables of the dialog and the lists of the event.
	for (auto const* part : { "<h2>Statistics</h2>", "<h3>Courses</h3>", "<h3>Course objects</h3>", "<h3>Legs</h3>", "<h3>Controls and courses</h3>" })
		QVERIFY2(html.contains(QLatin1String(part)), part);
	QVERIFY(html.contains(QStringLiteral("<td>A</td><td>H21, H20</td><td>50</td>")));
	QVERIFY(html.contains(QStringLiteral("<td>31</td><td>32</td><td>100 m</td><td>3</td><td>50</td><td>A, C, D</td>")));
	QVERIFY(html.contains(QStringLiteral("<td>33</td><td>34</td><td>20 m</td>")));
	
	// Either part alone.
	options.statistics = false;
	auto const check_only = courseReportHtml(map, map.courseData(), options);
	QVERIFY(check_only.contains(QStringLiteral("<h2>Consistency check</h2>")));
	QVERIFY(!check_only.contains(QStringLiteral("<h2>Statistics</h2>")));
	options.statistics = true;
	options.check = false;
	auto const statistics_only = courseReportHtml(map, map.courseData(), options);
	QVERIFY(!statistics_only.contains(QStringLiteral("<h2>Consistency check</h2>")));
	QVERIFY(statistics_only.contains(QStringLiteral("<h2>Statistics</h2>")));
	
	// A part of the check without issues says so.
	Map clean;
	clean.setScaleDenominator(10000);
	auto const empty_html = courseReportHtml(clean, clean.courseData(), CourseReportOptions{});
	QVERIFY(empty_html.contains(QStringLiteral("<h1>Courses</h1>")));
	QVERIFY(empty_html.contains(QStringLiteral("Errors: 0, warnings: 0, notes: 0.")));
	QCOMPARE(empty_html.count(QStringLiteral("<p>None.</p>")), 8 + 8);
	
	// The PDF.
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	auto const path = folder.filePath(QStringLiteral("report.pdf"));
	QVERIFY(writeCourseReportPdf(html, path));
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto const bytes = file.readAll();
	QVERIFY(bytes.startsWith("%PDF-"));
	QVERIFY(bytes.contains("/Type /Page"));
	QVERIFY(!writeCourseReportPdf(html, folder.filePath(QStringLiteral("no folder/report.pdf"))));
}


/// Accept (12)
void CourseReportTest::printDialogTest()
{
	auto* map = new Map();
	fillReportMap(*map);
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	QVERIFY(editor->getAction("course-report"));
	
	CourseReportDialog dialog(map, QStringLiteral("Spring Cup"));
	auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("course-report-check"));
	auto* statistics = dialog.findChild<QCheckBox*>(QStringLiteral("course-report-statistics"));
	auto* view = dialog.findChild<QTextBrowser*>(QStringLiteral("course-report-view"));
	QVERIFY(check && statistics && view);
	QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("course-report-print")));
	QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("course-report-pdf")));
	QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("course-report-html")));
	QVERIFY(check->isChecked() && statistics->isChecked());
	
	QVERIFY(dialog.html().contains(QStringLiteral("<h1>Spring Cup</h1>")));
	QVERIFY(dialog.html().contains(QDate::currentDate().toString(Qt::ISODate)));
	QVERIFY(view->toPlainText().contains(QStringLiteral("The control 35 is in no course.")));
	statistics->setChecked(false);
	QVERIFY(!dialog.html().contains(QStringLiteral("<h2>Statistics</h2>")));
	QVERIFY(!view->toPlainText().contains(QStringLiteral("Course objects")));
	
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	auto const html_path = folder.filePath(QStringLiteral("report.html"));
	QVERIFY(dialog.saveHtml(html_path));
	QFile file(html_path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(QString::fromUtf8(file.readAll()), dialog.html());
	QVERIFY(dialog.savePdf(folder.filePath(QStringLiteral("report.pdf"))));
	QVERIFY(QFileInfo(folder.filePath(QStringLiteral("report.pdf"))).size() > 0);
	
	window->deleteLater();
}


/*
 * We don't need a real GUI window.
 *
 * But we discovered QTBUG-58768 macOS: Crash when using QPrinter
 * while running with "minimal" platform plugin.
 */
#ifndef Q_OS_MACOS
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "minimal");  // clazy:exclude=non-pod-global-static
}
#endif


QTEST_MAIN(CourseReportTest)
#include "course_report_t.moc"  // IWYU pragma: keep
