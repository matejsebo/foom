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


#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QRadioButton>
#include <QSize>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QVariant>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_bulk.h"
#include "course/course_compose.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_storage.h"
#include "course/course_symbols.h"
#include "gui/main_window.h"
#include "gui/map/course_bulk_dialog.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/course_widget.h"
#include "tools/compose_course_tool.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// Upstream's symbol set in the source folder.
QString setPath()
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("../symbol sets/10000/Course_Design_10000.omap"));
}

/// The reach of a click in the cases which have no screen, in mm.
constexpr double reach = 0.5;

CourseObject courseObject(CourseObject::Kind kind, const QString& code)
{
	CourseObject object;
	object.kind = kind;
	object.code = code;
	return object;
}

/// Adds a course whose items are the codes of the text, as "S1 31 F1".
void addCourseOf(CourseData& data, const QString& name, const QString& codes)
{
	QVERIFY(addCourse(data, name));
	auto index = 0;
	for (auto const& code : codes.split(QLatin1Char(' '), QString::SkipEmptyParts))
		QVERIFY(insertCourseItem(data, name, index++, code));
}

/// The codes of the items of a course, as "S1 31 32 F1"; "?" for a
/// course which is not there, and "-" for an item without a code.
QString itemsOf(const CourseData& data, const QString& name)
{
	auto const* found = data.findCourse(name);
	if (!found)
		return QStringLiteral("?");
	QStringList codes;
	for (auto const& item : found->items)
		codes.push_back(item.code.isEmpty() ? QStringLiteral("-") : item.code);
	return codes.join(QLatin1Char(' '));
}

/// The items of a course as itemsOf() has them, the markers by the
/// letters of the OCD records: k, w, g and x.
QString itemTextOf(const CourseData& data, const QString& name)
{
	auto const* found = data.findCourse(name);
	if (!found)
		return QStringLiteral("?");
	QStringList codes;
	for (auto const& item : found->items)
	{
		switch (item.type)
		{
		case CourseItem::CrossingPoint: codes.push_back(QStringLiteral("k")); break;
		case CourseItem::Passage:       codes.push_back(QStringLiteral("w")); break;
		case CourseItem::MapExchange:   codes.push_back(QStringLiteral("g")); break;
		case CourseItem::MapFlip:       codes.push_back(QStringLiteral("x")); break;
		default:                        codes.push_back(item.code.isEmpty() ? QStringLiteral("-") : item.code);
		}
	}
	return codes.join(QLatin1Char(' '));
}

/// The codes of the course objects, as "S1 31 32 F1".
QString objectsOf(const CourseData& data)
{
	QStringList codes;
	for (auto const& object : data.objects)
		codes.push_back(object.code);
	return codes.join(QLatin1Char(' '));
}

/// The data of the cases 1, 2 and 9.
CourseData bulkData()
{
	CourseData data;
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S1")));
	for (int code = 31; code <= 35; ++code)
		data.objects.push_back(courseObject(CourseObject::Control, QString::number(code)));
	data.objects.push_back(courseObject(CourseObject::Finish, QStringLiteral("F1")));
	data.objects.push_back(courseObject(CourseObject::Description, QStringLiteral("D1")));
	addCourseOf(data, QStringLiteral("A"), QStringLiteral("S1 31 32 33 F1"));
	addCourseOf(data, QStringLiteral("B"), QStringLiteral("S1 32 33 F1"));
	addCourseOf(data, QStringLiteral("C"), QStringLiteral("S1 31 32 31 33 F1"));
	addCourseOf(data, QStringLiteral("D"), QStringLiteral("S1 F1"));
	return data;
}

CourseCondition condition(CourseCondition::Relation relation, const QString& first = {}, const QString& second = {})
{
	CourseCondition result;
	result.relation = relation;
	result.first = first;
	result.second = second;
	return result;
}

QStringList names(std::initializer_list<const char*> list)
{
	QStringList result;
	for (auto const* name : list)
		result.push_back(QString::fromLatin1(name));
	return result;
}

/// The data of the case 3.
CourseData renumberData()
{
	CourseData data;
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S1")));
	for (auto const* code : { "31", "32", "35", "40", "31a" })
		data.objects.push_back(courseObject(CourseObject::Control, QString::fromLatin1(code)));
	data.objects.push_back(courseObject(CourseObject::Finish, QStringLiteral("F1")));
	addCourseOf(data, QStringLiteral("A"), QStringLiteral("S1 31 32 35 40 F1"));
	CourseOverride number;
	number.course = QStringLiteral("A");
	number.what = CourseOverride::Number;
	number.code_from = QStringLiteral("35");
	data.overrides.push_back(number);
	return data;
}

/// Fills a map of 1:10,000 with FOOM's course symbols and an empty course A.
bool fillComposeMap(Map& map)
{
	map.setScaleDenominator(10000);
	if (!addCourseSymbols(map, setPath()).loaded)
		return false;
	auto data = map.courseData();
	if (!addCourse(data, QStringLiteral("A")))
		return false;
	changeCourseData(map, data);
	return true;
}

/// The map object of a course object, or nullptr.
const Object* mapObjectOf(const Map& map, const QString& code)
{
	auto const* object = map.courseData().findObject(code);
	return object ? map.findObject(object->object) : nullptr;
}

/// Composes the course A of the case 4: S1 at (10, 10), 31 at (30, 10),
/// 32 at (50, 10) and F1 at (70, 10).
void composeA(CourseComposer& composer)
{
	composer.setCourse(QStringLiteral("A"));
	QCOMPARE(composer.click(MapCoordF(10.0, 10.0), reach).outcome, CourseComposer::Added);
	QCOMPARE(composer.click(MapCoordF(30.0, 10.0), reach).outcome, CourseComposer::Added);
	QCOMPARE(composer.click(MapCoordF(50.0, 10.0), reach).outcome, CourseComposer::Added);
	QCOMPARE(composer.click(MapCoordF(70.0, 10.0), reach).outcome, CourseComposer::Added);
	QCOMPARE(composer.doubleClick(MapCoordF(70.0, 10.0), reach).outcome, CourseComposer::Added);
}

}  // namespace



/**
 * @test Tests the commands which change many courses at once, the
 * renumbering of the controls, and the composing of a course by clicks.
 *
 * See the design notes, "Composing a course and the bulk
 * commands".
 */
class CourseComposeTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void insertTest();
	void removeTest();
	void renumberTest();
	void composeTest();
	void existingTest();
	void insertModeTest();
	void removeAtTest();
	void refusedTest();
	void dialogTest();
	void editorTest();
	void markerTest();
};


void CourseComposeTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseComposeTest::insertTest()
{
	auto const all = names({ "A", "B", "C", "D" });
	
	auto data = bulkData();
	auto report = insertIntoCourses(data, QStringLiteral("34"), all, condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 32 34 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 34 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 31 32 34 31 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("D")), QStringLiteral("S1 F1"));
	QCOMPARE(int(report.changed.size()), 3);
	QCOMPARE(report.changed[0].first, QStringLiteral("A"));
	QCOMPARE(report.changed[0].second, 1);
	QCOMPARE(report.changed[2].first, QStringLiteral("C"));
	QCOMPARE(report.unchanged, names({ "D" }));
	QCOMPARE(report.items(), 3);
	QCOMPARE(data.findCourse(QStringLiteral("A"))->items[3].type, CourseItem::Control);
	
	// A second time: the object is not put beside itself.
	auto const once = data;
	report = insertIntoCourses(data, QStringLiteral("34"), all, condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QVERIFY(report.changed.empty());
	QCOMPARE(report.unchanged, all);
	QVERIFY(data == once);
	
	// Before every visit, and only in the chosen courses.
	data = bulkData();
	report = insertIntoCourses(data, QStringLiteral("34"), names({ "A", "C" }), condition(CourseCondition::Before, QStringLiteral("31")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 34 31 32 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 34 31 32 34 31 33 F1"));
	QCOMPARE(report.changed[1].second, 2);
	
	data = bulkData();
	report = insertIntoCourses(data, QStringLiteral("34"), names({ "A", "B", "C" }), condition(CourseCondition::Between, QStringLiteral("32"), QStringLiteral("33")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 32 34 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 34 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 31 32 31 33 F1"));
	QCOMPARE(report.unchanged, names({ "C" }));
	
	// What is refused changes nothing.
	auto const original = bulkData();
	struct Refusal
	{
		QString code;
		QStringList courses;
		CourseCondition condition;
		CourseBulkReport::Error error;
	};
	auto const after_32 = condition(CourseCondition::After, QStringLiteral("32"));
	const std::vector<Refusal> refusals = {
	    { QStringLiteral("D1"), all, after_32, CourseBulkReport::NoObject },
	    { QStringLiteral("99"), all, after_32, CourseBulkReport::NoObject },
	    { QStringLiteral("34"), {}, after_32, CourseBulkReport::NoCourses },
	    { QStringLiteral("34"), names({ "A", "X" }), after_32, CourseBulkReport::NoCourses },
	    { QStringLiteral("34"), all, condition(CourseCondition::After), CourseBulkReport::NoCondition },
	    { QStringLiteral("34"), all, condition(CourseCondition::None, QStringLiteral("32")), CourseBulkReport::NoCondition },
	    { QStringLiteral("34"), all, condition(CourseCondition::After, QStringLiteral("99")), CourseBulkReport::NoCondition },
	    { QStringLiteral("34"), all, condition(CourseCondition::Between, QStringLiteral("32")), CourseBulkReport::NoCondition },
	    { QStringLiteral("34"), all, condition(CourseCondition::All), CourseBulkReport::NoCondition },
	    { QStringLiteral("34"), all, condition(CourseCondition::After, QStringLiteral("34")), CourseBulkReport::InvalidOrder },
	    { QStringLiteral("34"), all, condition(CourseCondition::Between, QStringLiteral("32"), QStringLiteral("32")), CourseBulkReport::InvalidOrder },
	    { QStringLiteral("34"), all, condition(CourseCondition::Between, QStringLiteral("32"), QStringLiteral("34")), CourseBulkReport::InvalidOrder },
	};
	for (auto const& refusal : refusals)
	{
		data = original;
		report = insertIntoCourses(data, refusal.code, refusal.courses, refusal.condition);
		QVERIFY(!report.ok());
		QCOMPARE(report.error, refusal.error);
		QVERIFY(report.changed.empty());
		QVERIFY(data == original);
	}
}


/// Accept (2)
void CourseComposeTest::removeTest()
{
	auto data = bulkData();
	auto report = removeFromCourses(data, QStringLiteral("32"), names({ "A", "B" }), condition(CourseCondition::All));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 31 32 31 33 F1"));
	QVERIFY(data.findObject(QStringLiteral("32")));
	QCOMPARE(int(report.changed.size()), 2);
	QCOMPARE(report.items(), 2);
	
	data = bulkData();
	report = removeFromCourses(data, QStringLiteral("31"), names({ "C" }), condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 31 32 33 F1"));
	
	data = bulkData();
	report = removeFromCourses(data, QStringLiteral("31"), names({ "C" }), condition(CourseCondition::Before, QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("C")), QStringLiteral("S1 32 31 33 F1"));
	
	data = bulkData();
	report = removeFromCourses(data, QStringLiteral("32"), names({ "A", "B" }), condition(CourseCondition::Between, QStringLiteral("31"), QStringLiteral("33")));
	QVERIFY(report.ok());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 33 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 33 F1"));
	QCOMPARE(report.unchanged, names({ "B" }));
	
	auto const original = bulkData();
	data = original;
	QCOMPARE(removeFromCourses(data, QStringLiteral("99"), names({ "A" }), condition(CourseCondition::All)).error, CourseBulkReport::NoObject);
	QCOMPARE(removeFromCourses(data, QStringLiteral("32"), {}, condition(CourseCondition::All)).error, CourseBulkReport::NoCourses);
	QCOMPARE(removeFromCourses(data, QStringLiteral("32"), names({ "A" }), condition(CourseCondition::None)).error, CourseBulkReport::NoCondition);
	QCOMPARE(removeFromCourses(data, QStringLiteral("32"), names({ "A" }), condition(CourseCondition::After)).error, CourseBulkReport::NoCondition);
	QCOMPARE(removeFromCourses(data, QStringLiteral("32"), names({ "A" }), condition(CourseCondition::After, QStringLiteral("32"))).error, CourseBulkReport::InvalidOrder);
	QVERIFY(data == original);
}


/// Accept (3)
void CourseComposeTest::renumberTest()
{
	auto const original = renumberData();
	QCOMPARE(firstControlCode(original), 31);
	QCOMPARE(firstControlCode(CourseData{}), 0);
	
	auto data = original;
	auto report = renumberControls(data, 31, 41, false);
	QVERIFY(report.done);
	QCOMPARE(report.renumbered, 4);
	QCOMPARE(objectsOf(data), QStringLiteral("S1 41 42 45 50 31a F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 41 42 45 50 F1"));
	QCOMPARE(data.overrides[0].code_from, QStringLiteral("45"));
	
	// All at once: 31 becomes 32 while 32 becomes 33.
	data = original;
	QVERIFY(renumberControls(data, 31, 32, false).done);
	QCOMPARE(objectsOf(data), QStringLiteral("S1 32 33 36 41 31a F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 32 33 36 41 F1"));
	
	data = original;
	report = renumberControls(data, 31, 31, true);
	QVERIFY(report.done);
	QCOMPARE(report.renumbered, 2);
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 32 33 34 31a F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 32 33 34 F1"));
	QCOMPARE(data.overrides[0].code_from, QStringLiteral("33"));
	
	data = original;
	QVERIFY(renumberControls(data, 35, 50, false).done);
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 32 50 55 31a F1"));
	
	data = original;
	report = renumberControls(data, 35, 32, false);
	QVERIFY(!report.done);
	QCOMPARE(report.clash, QStringLiteral("32"));
	QVERIFY(data == original);
	
	QVERIFY(renumberControls(data, 35, 33, false).done);
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 32 33 38 31a F1"));
	
	data = original;
	QVERIFY(!renumberControls(data, 31, 0, false).done);
	QVERIFY(!renumberControls(data, 100, 200, false).done);
	QVERIFY(data == original);
	
	QVERIFY(renameCourseObject(data, QStringLiteral("F1"), QStringLiteral("45")));
	auto const with_45 = data;
	report = renumberControls(data, 31, 41, false);
	QVERIFY(!report.done);
	QCOMPARE(report.clash, QStringLiteral("45"));
	QVERIFY(data == with_45);
}


/// Accept (4)
void CourseComposeTest::composeTest()
{
	Map map;
	QVERIFY(fillComposeMap(map));
	auto& undo = map.undoManager();
	auto const steps = undo.undoStepCount();
	
	CourseComposer composer(map);
	composer.setCourse(QStringLiteral("A"));
	QCOMPARE(composer.refusal(), CourseComposer::NoReason);
	
	auto result = composer.click(MapCoordF(10.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("S1"));
	QCOMPARE(map.getNumObjects(), 1);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1"));
	QCOMPARE(map.courseData().objects[0].kind, CourseObject::Start);
	auto const* start = mapObjectOf(map, QStringLiteral("S1"));
	QVERIFY(start);
	QCOMPARE(start->getSymbol()->courseRole(), Symbol::CourseStart);
	QCOMPARE(start->getType(), Object::Point);
	QCOMPARE(MapCoordF(start->asPoint()->getCoordF()), MapCoordF(10.0, 10.0));
	QCOMPARE(undo.undoStepCount(), steps + 1);
	
	QVERIFY(undo.undo());
	QCOMPARE(map.getNumObjects(), 0);
	QVERIFY(map.courseData().objects.empty());
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QString{});
	QVERIFY(undo.redo());
	QCOMPARE(map.getNumObjects(), 1);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1"));
	
	result = composer.click(MapCoordF(30.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("31"));
	QCOMPARE(mapObjectOf(map, QStringLiteral("31"))->getSymbol()->courseRole(), Symbol::CourseControl);
	result = composer.click(MapCoordF(50.0, 10.0), reach);
	QCOMPARE(result.code, QStringLiteral("32"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32"));
	QVERIFY(!composer.isComplete());
	
	// The double click: its first click makes a control, which the finish replaces.
	result = composer.click(MapCoordF(70.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("33"));
	result = composer.doubleClick(MapCoordF(70.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("F1"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(map.getNumObjects(), 4);
	auto const* finish = mapObjectOf(map, QStringLiteral("F1"));
	QVERIFY(finish);
	QCOMPARE(finish->getSymbol()->courseRole(), Symbol::CourseFinish);
	QCOMPARE(MapCoordF(finish->asPoint()->getCoordF()), MapCoordF(70.0, 10.0));
	QCOMPARE(undo.undoStepCount(), steps + 4);
	QVERIFY(composer.isComplete());
	
	QVERIFY(undo.undo());
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32"));
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 31 32"));
	QCOMPARE(map.getNumObjects(), 3);
	QVERIFY(undo.redo());
	
	// With its finish the course takes nothing more at its end.
	auto const before = map.courseData();
	result = composer.click(MapCoordF(90.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Refused);
	QCOMPARE(result.reason, CourseComposer::Complete);
	QVERIFY(map.courseData() == before);
	QCOMPARE(map.getNumObjects(), 4);
	QCOMPARE(undo.undoStepCount(), steps + 4);
}


/// Accept (5)
void CourseComposeTest::existingTest()
{
	Map map;
	QVERIFY(fillComposeMap(map));
	CourseComposer composer(map);
	composeA(composer);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 F1"));
	
	auto data = map.courseData();
	QVERIFY(addCourse(data, QStringLiteral("B")));
	changeCourseData(map, data);
	composer.setCourse(QStringLiteral("B"));
	
	auto target = composer.targetAt(MapCoordF(12.0, 10.0), reach);
	QCOMPARE(target.what, CourseComposer::Target::Object);
	QCOMPARE(target.code, QStringLiteral("S1"));
	QCOMPARE(composer.targetAt(MapCoordF(20.0, 30.0), reach).what, CourseComposer::Target::Nothing);
	
	auto result = composer.click(MapCoordF(12.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("S1"));
	result = composer.click(MapCoordF(50.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("32"));
	auto const steps = map.undoManager().undoStepCount();
	result = composer.click(MapCoordF(50.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Nothing);
	QCOMPARE(result.reason, CourseComposer::BesideItself);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	result = composer.click(MapCoordF(70.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Added);
	QCOMPARE(result.code, QStringLiteral("F1"));
	// The second click of a double click on an object which is there adds nothing.
	QCOMPARE(composer.doubleClick(MapCoordF(70.0, 10.0), reach).outcome, CourseComposer::Nothing);
	
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(map.getNumObjects(), 4);
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 31 32 F1"));
}


/// Accept (6)
void CourseComposeTest::insertModeTest()
{
	Map map;
	QVERIFY(fillComposeMap(map));
	CourseComposer composer(map);
	composeA(composer);
	
	auto const target = composer.targetAt(MapCoordF(40.0, 10.2), reach);
	QCOMPARE(target.what, CourseComposer::Target::Leg);
	QCOMPARE(target.item, 1);
	
	auto const before = map.courseData();
	auto const steps = map.undoManager().undoStepCount();
	auto result = composer.click(MapCoordF(40.0, 10.2), reach);
	QCOMPARE(result.outcome, CourseComposer::InsertModeBegun);
	QVERIFY(composer.inInsertMode());
	QVERIFY(map.courseData() == before);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	QCOMPARE(int(composer.rubberEnds().size()), 2);
	
	result = composer.click(MapCoordF(40.0, 25.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Inserted);
	QCOMPARE(result.code, QStringLiteral("33"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 32 F1"));
	QCOMPARE(map.getNumObjects(), 5);
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QVERIFY(composer.inInsertMode());
	
	result = composer.click(MapCoordF(55.0, 25.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Inserted);
	QCOMPARE(result.code, QStringLiteral("34"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 34 32 F1"));
	
	// 32 follows the place: it is not put beside itself.
	result = composer.click(MapCoordF(50.0, 10.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Nothing);
	QCOMPARE(result.reason, CourseComposer::BesideItself);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 34 32 F1"));
	
	composer.leaveInsertMode();
	QVERIFY(!composer.inInsertMode());
	QCOMPARE(int(composer.rubberEnds().size()), 0);  // the course has its finish
	QCOMPARE(composer.click(MapCoordF(20.0, 10.0), reach).outcome, CourseComposer::InsertModeBegun);
	
	// An undo of the map takes the mode away: the place may be gone.
	QVERIFY(map.undoManager().undo());
	QVERIFY(!composer.inInsertMode());
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 32 F1"));
}


/// Accept (7)
void CourseComposeTest::removeAtTest()
{
	Map map;
	QVERIFY(fillComposeMap(map));
	CourseComposer composer(map);
	composeA(composer);
	QCOMPARE(composer.click(MapCoordF(40.0, 10.0), reach).outcome, CourseComposer::InsertModeBegun);
	QCOMPARE(composer.click(MapCoordF(40.0, 25.0), reach).code, QStringLiteral("33"));
	QCOMPARE(composer.click(MapCoordF(55.0, 25.0), reach).code, QStringLiteral("34"));
	composer.leaveInsertMode();
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 34 32 F1"));
	
	auto result = composer.removeAt(MapCoordF(40.0, 25.0), reach);
	QCOMPARE(result.outcome, CourseComposer::Removed);
	QCOMPARE(result.code, QStringLiteral("33"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 34 32 F1"));
	QVERIFY(mapObjectOf(map, QStringLiteral("33")));
	QCOMPARE(map.getNumObjects(), 6);
	
	// Not of the course any more, and free ground.
	auto const steps = map.undoManager().undoStepCount();
	QCOMPARE(composer.removeAt(MapCoordF(40.0, 25.0), reach).outcome, CourseComposer::Nothing);
	QCOMPARE(composer.removeAt(MapCoordF(90.0, 40.0), reach).outcome, CourseComposer::Nothing);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	
	// Of two visits the last one goes.
	auto data = map.courseData();
	addCourseOf(data, QStringLiteral("C"), QStringLiteral("S1 31 32 31 F1"));
	changeCourseData(map, data);
	composer.setCourse(QStringLiteral("C"));
	QCOMPARE(composer.removeAt(MapCoordF(30.0, 10.0), reach).outcome, CourseComposer::Removed);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("C")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 34 32 F1"));
}


/// Accept (8)
void CourseComposeTest::refusedTest()
{
	{
		Map map;
		QVERIFY(fillComposeMap(map));
		CourseComposer composer(map);
		composeA(composer);
		
		composer.setCourse(QStringLiteral("X"));
		QCOMPARE(composer.refusal(), CourseComposer::NoCourse);
		QCOMPARE(composer.click(MapCoordF(10.0, 30.0), reach).reason, CourseComposer::NoCourse);
		
		auto data = map.courseData();
		addCourseOf(data, QStringLiteral("V"), QStringLiteral("S1 31 32 F1"));
		CourseItem fork;
		fork.type = CourseItem::TeamForkBegin;
		fork.count = 2;
		data.courses.back().items.insert(begin(data.courses.back().items) + 1, fork);
		changeCourseData(map, data);
		auto const before = map.courseData();
		composer.setCourse(QStringLiteral("V"));
		QCOMPARE(composer.refusal(), CourseComposer::HasVariation);
		auto result = composer.click(MapCoordF(40.0, 10.0), reach);
		QCOMPARE(result.outcome, CourseComposer::Refused);
		QCOMPARE(result.reason, CourseComposer::HasVariation);
		QCOMPARE(composer.click(MapCoordF(10.0, 30.0), reach).outcome, CourseComposer::Refused);
		QCOMPARE(composer.removeAt(MapCoordF(30.0, 10.0), reach).outcome, CourseComposer::Refused);
		QVERIFY(map.courseData() == before);
		QCOMPARE(map.getNumObjects(), 4);
	}
	
	{
		// A map which has symbols for the start and the finish and none for the control.
		Map map;
		auto* start = new PointSymbol();
		start->setNumberComponent(0, 701);
		start->setCourseRole(Symbol::CourseStart);
		map.addSymbol(start, 0);
		auto* finish = new PointSymbol();
		finish->setNumberComponent(0, 706);
		finish->setCourseRole(Symbol::CourseFinish);
		map.addSymbol(finish, 1);
		auto* object = new PointObject(finish);
		object->setPosition(MapCoord(40.0, 0.0));
		auto const index = map.addObject(object);
		auto* step = new DeleteObjectsUndoStep(&map);
		step->addObject(index);
		map.push(step);
		auto data = map.courseData();
		QCOMPARE(objectsOf(data), QStringLiteral("F1"));
		QVERIFY(addCourse(data, QStringLiteral("A")));
		changeCourseData(map, data);
		
		CourseComposer composer(map);
		composer.setCourse(QStringLiteral("A"));
		QCOMPARE(composer.click(MapCoordF(0.0, 0.0), reach).code, QStringLiteral("S1"));
		auto const before = map.courseData();
		auto result = composer.click(MapCoordF(20.0, 0.0), reach);
		QCOMPARE(result.outcome, CourseComposer::Refused);
		QCOMPARE(result.reason, CourseComposer::NoSymbol);
		QCOMPARE(result.kind, CourseObject::Control);
		QVERIFY(map.courseData() == before);
		QCOMPARE(map.getNumObjects(), 2);
		// A double click finds nothing which its first click made.
		QCOMPARE(composer.doubleClick(MapCoordF(20.0, 0.0), reach).outcome, CourseComposer::Nothing);
		
		result = composer.click(MapCoordF(40.2, 0.0), reach);
		QCOMPARE(result.outcome, CourseComposer::Added);
		QCOMPARE(result.code, QStringLiteral("F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 F1"));
	}
}


/// Accept (9)
void CourseComposeTest::dialogTest()
{
	{
		Map map;
		changeCourseData(map, bulkData());
		auto const steps = map.undoManager().undoStepCount();
		
		CourseBulkDialog dialog(&map, CourseBulkDialog::Insert, QStringLiteral("34"));
		auto* objects = dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-object"));
		auto* courses = dialog.findChild<QListWidget*>(QStringLiteral("course-bulk-courses"));
		auto* after = dialog.findChild<QRadioButton*>(QStringLiteral("course-bulk-after"));
		auto* first = dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-first"));
		QVERIFY(objects && courses && after && first);
		QVERIFY(dialog.findChild<QRadioButton*>(QStringLiteral("course-bulk-before")));
		QVERIFY(dialog.findChild<QRadioButton*>(QStringLiteral("course-bulk-between")));
		QVERIFY(dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-second")));
		QVERIFY(!dialog.findChild<QRadioButton*>(QStringLiteral("course-bulk-all")));
		
		// The seven objects and the four markers.
		QCOMPARE(objects->count(), 11);
		QCOMPARE(objects->itemData(7, CourseBulkDialog::markerRole()).toInt(), int(CourseItem::CrossingPoint));
		QCOMPARE(objects->itemData(10, CourseBulkDialog::markerRole()).toInt(), int(CourseItem::MapFlip));
		QVERIFY(!objects->itemData(6, CourseBulkDialog::markerRole()).isValid());
		QCOMPARE(objects->currentData().toString(), QStringLiteral("34"));
		QVERIFY(objects->findData(QStringLiteral("S1")) >= 0);
		QVERIFY(objects->findData(QStringLiteral("D1")) < 0);
		QCOMPARE(courses->count(), 4);
		for (int i = 0; i < courses->count(); ++i)
			QCOMPARE(courses->item(i)->checkState(), Qt::Unchecked);
		QCOMPARE(courses->item(0)->text(), QStringLiteral("A"));
		QVERIFY(after->isChecked());
		first->setCurrentIndex(first->findData(QStringLiteral("32")));
		
		// No course is checked.
		QString message;
		QVERIFY(!dialog.apply(&message));
		QVERIFY(!message.isEmpty());
		QCOMPARE(map.undoManager().undoStepCount(), steps);
		
		courses->item(0)->setCheckState(Qt::Checked);
		courses->item(1)->setCheckState(Qt::Checked);
		QVERIFY(dialog.apply(&message));
		QVERIFY(!message.isEmpty());
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 34 33 F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 34 33 F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("C")), QStringLiteral("S1 31 32 31 33 F1"));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QVERIFY(map.undoManager().undo());
		QVERIFY(map.courseData() == bulkData());
	}
	
	{
		Map map;
		changeCourseData(map, bulkData());
		auto const steps = map.undoManager().undoStepCount();
		
		CourseBulkDialog dialog(&map, CourseBulkDialog::Remove, QStringLiteral("32"));
		auto* all = dialog.findChild<QRadioButton*>(QStringLiteral("course-bulk-all"));
		auto* courses = dialog.findChild<QListWidget*>(QStringLiteral("course-bulk-courses"));
		QVERIFY(all && courses);
		QVERIFY(all->isChecked());
		QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-object"))->currentData().toString(), QStringLiteral("32"));
		courses->item(0)->setCheckState(Qt::Checked);
		courses->item(3)->setCheckState(Qt::Checked);
		QString message;
		QVERIFY(dialog.apply(&message));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 33 F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 33 F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("D")), QStringLiteral("S1 F1"));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	}
	
	{
		Map map;
		changeCourseData(map, renumberData());
		auto const steps = map.undoManager().undoStepCount();
		
		CourseRenumberDialog dialog(&map);
		auto* old_first = dialog.findChild<QSpinBox*>(QStringLiteral("course-renumber-old"));
		auto* new_first = dialog.findChild<QSpinBox*>(QStringLiteral("course-renumber-new"));
		auto* gaps = dialog.findChild<QCheckBox*>(QStringLiteral("course-renumber-gaps"));
		QVERIFY(old_first && new_first && gaps);
		QCOMPARE(old_first->value(), 31);
		QCOMPARE(new_first->value(), 31);
		QVERIFY(!gaps->isChecked());
		
		// A clash is refused and said.
		old_first->setValue(35);
		new_first->setValue(32);
		QString message;
		QVERIFY(!dialog.apply(&message));
		QVERIFY(message.contains(QStringLiteral("32")));
		QCOMPARE(map.undoManager().undoStepCount(), steps);
		
		old_first->setValue(31);
		new_first->setValue(41);
		gaps->setChecked(true);
		QVERIFY(dialog.apply(&message));
		QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 41 42 43 44 31a F1"));
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 41 42 43 44 F1"));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QVERIFY(map.undoManager().undo());
		QVERIFY(map.courseData() == renumberData());
	}
	
	// The markers.
	{
		Map map;
		changeCourseData(map, bulkData());
		auto const steps = map.undoManager().undoStepCount();
	
		CourseBulkDialog dialog(&map, CourseBulkDialog::Insert, QStringLiteral("34"));
		auto* objects = dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-object"));
		auto* courses = dialog.findChild<QListWidget*>(QStringLiteral("course-bulk-courses"));
		auto* first = dialog.findChild<QComboBox*>(QStringLiteral("course-bulk-first"));
		QVERIFY(objects && courses && first);
		QCOMPARE(first->count(), 7);
		objects->setCurrentIndex(objects->findData(int(CourseItem::MapExchange), CourseBulkDialog::markerRole()));
		first->setCurrentIndex(first->findData(QStringLiteral("32")));
		courses->item(0)->setCheckState(Qt::Checked);
		QString message;
		QVERIFY(dialog.apply(&message));
		QVERIFY(message.contains(objects->currentText()));
		QCOMPARE(itemTextOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 g 33 F1"));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	
		// A second time there is no such place.
		QVERIFY(!dialog.apply(&message));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	
		CourseBulkDialog remove(&map, CourseBulkDialog::Remove, QString{});
		auto* remove_objects = remove.findChild<QComboBox*>(QStringLiteral("course-bulk-object"));
		auto* remove_courses = remove.findChild<QListWidget*>(QStringLiteral("course-bulk-courses"));
		QVERIFY(remove_objects && remove_courses);
		remove_objects->setCurrentIndex(remove_objects->findData(int(CourseItem::MapExchange), CourseBulkDialog::markerRole()));
		for (int i = 0; i < remove_courses->count(); ++i)
			remove_courses->item(i)->setCheckState(Qt::Checked);
		QVERIFY(remove.apply(&message));
		QVERIFY(map.courseData() == bulkData());
		QCOMPARE(map.undoManager().undoStepCount(), steps + 2);
	}
}


/// Accept (10)
void CourseComposeTest::editorTest()
{
	auto* map = new Map();
	QVERIFY(fillComposeMap(*map));
	
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	auto* widget = editor->getMainWidget();
	
	for (auto const* id : { "compose-course", "course-insert-into-courses", "course-delete-from-courses", "course-renumber" })
		QVERIFY2(editor->getAction(id), id);
	
	// The button of the toolbar, with an icon of its own.
	auto* toolbar = window->findChild<QToolBar*>(QStringLiteral("Course setting toolbar"));
	QVERIFY(toolbar);
	QVERIFY(toolbar->actions().contains(editor->getAction("compose-course")));
	QVERIFY(!editor->getAction("compose-course")->icon().isNull());
	QVERIFY(QIcon(QStringLiteral(":/images/compose-course.png")).availableSizes().contains(QSize(32, 32)));
	
	editor->getAction("course-dock")->trigger();
	auto* dock = window->findChild<CourseWidget*>();
	QVERIFY(dock);
	dock->setCurrentCourse(QStringLiteral("A"));
	QCOMPARE(dock->currentCourse(), QStringLiteral("A"));
	
	editor->getAction("compose-course")->trigger();
	auto* tool = dynamic_cast<ComposeCourseTool*>(editor->getTool());
	QVERIFY(tool);
	
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const items = [map]() { return itemsOf(map->courseData(), QStringLiteral("A")); };
	
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(-30.0, 0.0));
	QCOMPARE(items(), QStringLiteral("S1"));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(-10.0, 0.0));
	QCOMPARE(items(), QStringLiteral("S1 31"));
	
	// A double click: press and release, the second press as a double click, release.
	auto const at_finish = viewport(10.0, 0.0);
	QTest::mouseClick(widget, Qt::LeftButton, {}, at_finish);
	QCOMPARE(items(), QStringLiteral("S1 31 32"));
	{
		QMouseEvent event(QEvent::MouseButtonDblClick, at_finish, widget->mapToGlobal(at_finish), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	}
	QTest::mouseRelease(widget, Qt::LeftButton, {}, at_finish);
	QCOMPARE(items(), QStringLiteral("S1 31 F1"));
	QCOMPARE(map->getNumObjects(), 3);
	
	// A click on the leg from 31 to the finish, and a click beside it.
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	QCOMPARE(items(), QStringLiteral("S1 31 F1"));
	QVERIFY(tool->composer().inInsertMode());
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 15.0));
	QCOMPARE(items(), QStringLiteral("S1 31 32 F1"));
	QVERIFY(tool->composer().inInsertMode());
	
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QVERIFY(tool->keyPressEvent(&escape));
	QVERIFY(!tool->composer().inInsertMode());
	QCOMPARE(editor->getTool(), static_cast<MapEditorTool*>(tool));
	
	QTest::mouseClick(widget, Qt::LeftButton, Qt::ControlModifier, viewport(0.0, 15.0));
	QCOMPARE(items(), QStringLiteral("S1 31 F1"));
	QCOMPARE(map->getNumObjects(), 4);
	
	editor->setTool(nullptr);
	window->deleteLater();
}


/// Accept (11)
void CourseComposeTest::markerTest()
{
	auto const all = names({ "A", "B", "C", "D" });
	auto const items = [](const CourseData& data, const char* name) { return itemTextOf(data, QString::fromLatin1(name)); };
	
	QVERIFY(isCourseMarker(CourseItem::CrossingPoint));
	QVERIFY(isCourseMarker(CourseItem::Passage));
	QVERIFY(isCourseMarker(CourseItem::MapExchange));
	QVERIFY(isCourseMarker(CourseItem::MapFlip));
	QVERIFY(!isCourseMarker(CourseItem::Control));
	QVERIFY(!isCourseMarker(CourseItem::TeamForkBegin));
	
	// A crossing point after 32, in every visit.
	auto data = bulkData();
	auto report = insertMarkerIntoCourses(data, CourseItem::CrossingPoint, all, condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 32 k 33 F1"));
	QCOMPARE(items(data, "B"), QStringLiteral("S1 32 k 33 F1"));
	QCOMPARE(items(data, "C"), QStringLiteral("S1 31 32 k 31 33 F1"));
	QCOMPARE(items(data, "D"), QStringLiteral("S1 F1"));
	QCOMPARE(report.items(), 3);
	QCOMPARE(report.unchanged, names({ "D" }));
	
	// Not beside one of its kind.
	auto const once = data;
	report = insertMarkerIntoCourses(data, CourseItem::CrossingPoint, all, condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QVERIFY(report.changed.empty());
	QVERIFY(data == once);
	
	// A passage beside the crossing point is something else.
	report = insertMarkerIntoCourses(data, CourseItem::Passage, names({ "A" }), condition(CourseCondition::Before, QStringLiteral("33")));
	QVERIFY(report.ok());
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 32 k w 33 F1"));
	
	// Between: only where the leg is, in its direction.
	data = bulkData();
	report = insertMarkerIntoCourses(data, CourseItem::Passage, names({ "A", "B", "C" }), condition(CourseCondition::Between, QStringLiteral("31"), QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 w 32 33 F1"));
	QCOMPARE(items(data, "B"), QStringLiteral("S1 32 33 F1"));
	QCOMPARE(items(data, "C"), QStringLiteral("S1 31 w 32 31 33 F1"));
	QCOMPARE(report.unchanged, names({ "B" }));
	
	// A map exchange, and no flip beside it.
	data = bulkData();
	report = insertMarkerIntoCourses(data, CourseItem::MapExchange, names({ "A" }), condition(CourseCondition::After, QStringLiteral("32")));
	QVERIFY(report.ok());
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 32 g 33 F1"));
	report = insertMarkerIntoCourses(data, CourseItem::MapFlip, names({ "A" }), condition(CourseCondition::Before, QStringLiteral("33")));
	QVERIFY(report.ok());
	QVERIFY(report.changed.empty());
	report = insertMarkerIntoCourses(data, CourseItem::MapFlip, names({ "B" }), condition(CourseCondition::Before, QStringLiteral("33")));
	QCOMPARE(items(data, "B"), QStringLiteral("S1 32 x 33 F1"));
	
	// Taking out.
	data = bulkData();
	insertMarkerIntoCourses(data, CourseItem::CrossingPoint, all, condition(CourseCondition::After, QStringLiteral("32")));
	insertMarkerIntoCourses(data, CourseItem::CrossingPoint, names({ "C" }), condition(CourseCondition::After, QStringLiteral("33")));
	QCOMPARE(items(data, "C"), QStringLiteral("S1 31 32 k 31 33 k F1"));
	auto with_markers = data;
	report = removeMarkerFromCourses(data, CourseItem::CrossingPoint, names({ "A", "C" }), condition(CourseCondition::All));
	QVERIFY(report.ok());
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 32 33 F1"));
	QCOMPARE(items(data, "B"), QStringLiteral("S1 32 k 33 F1"));
	QCOMPARE(items(data, "C"), QStringLiteral("S1 31 32 31 33 F1"));
	QCOMPARE(report.items(), 3);
	data = with_markers;
	report = removeMarkerFromCourses(data, CourseItem::CrossingPoint, names({ "C" }), condition(CourseCondition::Before, QStringLiteral("F1")));
	QCOMPARE(items(data, "C"), QStringLiteral("S1 31 32 k 31 33 F1"));
	data = with_markers;
	report = removeMarkerFromCourses(data, CourseItem::CrossingPoint, names({ "A", "B" }), condition(CourseCondition::Between, QStringLiteral("32"), QStringLiteral("33")));
	QCOMPARE(items(data, "A"), QStringLiteral("S1 31 32 33 F1"));
	QCOMPARE(items(data, "B"), QStringLiteral("S1 32 33 F1"));
	data = with_markers;
	report = removeMarkerFromCourses(data, CourseItem::Passage, all, condition(CourseCondition::All));
	QVERIFY(report.ok());
	QVERIFY(report.changed.empty());
	QVERIFY(data == with_markers);
	
	// Refused, with nothing changed.
	auto const original = bulkData();
	struct Refusal
	{
		CourseItem::Type type;
		QStringList courses;
		CourseCondition condition;
		CourseBulkReport::Error error;
	};
	auto const after_32 = condition(CourseCondition::After, QStringLiteral("32"));
	const std::vector<Refusal> refusals = {
	    { CourseItem::Control, all, after_32, CourseBulkReport::NoObject },
	    { CourseItem::TeamForkBegin, all, after_32, CourseBulkReport::NoObject },
	    { CourseItem::CrossingPoint, {}, after_32, CourseBulkReport::NoCourses },
	    { CourseItem::CrossingPoint, names({ "X" }), after_32, CourseBulkReport::NoCourses },
	    { CourseItem::CrossingPoint, all, condition(CourseCondition::All), CourseBulkReport::NoCondition },
	    { CourseItem::CrossingPoint, all, condition(CourseCondition::After, QStringLiteral("99")), CourseBulkReport::NoCondition },
	    { CourseItem::MapExchange, all, condition(CourseCondition::Between, QStringLiteral("32"), QStringLiteral("32")), CourseBulkReport::InvalidOrder },
	};
	for (auto const& refusal : refusals)
	{
		data = original;
		report = insertMarkerIntoCourses(data, refusal.type, refusal.courses, refusal.condition);
		QCOMPARE(report.error, refusal.error);
		QVERIFY(data == original);
	}
	data = original;
	report = removeMarkerFromCourses(data, CourseItem::Start, all, condition(CourseCondition::All));
	QCOMPARE(report.error, CourseBulkReport::NoObject);
	QVERIFY(data == original);
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


QTEST_MAIN(CourseComposeTest)
#include "course_compose_t.moc"  // IWYU pragma: keep
