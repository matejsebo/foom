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
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVariant>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_storage.h"
#include "course/course_sync.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/widgets/course_widget.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// A map with the symbols of a start, a control and a finish and one
/// symbol without a role.
struct CourseMap
{
	Map map;
	PointSymbol* start;
	PointSymbol* control;
	PointSymbol* finish;
	PointSymbol* description;
	PointSymbol* plain;
	
	CourseMap()
	{
		start = addSymbol(701, Symbol::CourseStart);
		control = addSymbol(703, Symbol::CourseControl);
		finish = addSymbol(706, Symbol::CourseFinish);
		description = addSymbol(720, Symbol::CourseDescription);
		plain = addSymbol(101, Symbol::NoCourseRole);
	}
	
	PointSymbol* addSymbol(int number, Symbol::CourseRole role)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, number);
		symbol->setCourseRole(role);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	}
	
	/// Adds a point object as the tool which draws points does.
	PointObject* draw(const Symbol* symbol)
	{
		auto* object = new PointObject(symbol);
		object->setPosition(MapCoord(10.0 * map.getNumObjects(), 5.0));
		auto const index = map.addObject(object);
		auto* step = new DeleteObjectsUndoStep(&map);
		step->addObject(index);
		map.push(step);
		return object;
	}
	
	/// Deletes an object as the editor does.
	void erase(Object* object)
	{
		map.clearObjectSelection(false);
		map.addObjectToSelection(object, false);
		map.deleteSelectedObjects();
	}
};

CourseObject courseObject(CourseObject::Kind kind, const QString& code)
{
	CourseObject object;
	object.kind = kind;
	object.code = code;
	return object;
}

CourseItem courseItem(CourseItem::Type type, const QString& code = {})
{
	CourseItem item;
	item.type = type;
	item.code = code;
	return item;
}

Course course(const QString& name, std::initializer_list<CourseItem> items)
{
	Course result;
	result.name = name;
	result.items = items;
	return result;
}

/// The codes of the course objects, as "S1 31 32 F1".
QString objectsOf(const CourseData& data)
{
	QStringList codes;
	for (auto const& object : data.objects)
		codes.push_back(object.code);
	return codes.join(QLatin1Char(' '));
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

/// The data of the cases 3 to 8: S1, 31, 32 and F1 as they were drawn,
/// the courses A and B, a class of each, and two overrides of A.
struct Event
{
	CourseMap m;
	PointObject* object_s1;
	PointObject* object_31;
	PointObject* object_32;
	PointObject* object_f1;
	
	Event()
	{
		object_s1 = m.draw(m.start);
		object_31 = m.draw(m.control);
		object_32 = m.draw(m.control);
		object_f1 = m.draw(m.finish);
		
		auto data = m.map.courseData();
		data.courses.push_back(course(QStringLiteral("A"), {
		    courseItem(CourseItem::Start, QStringLiteral("S1")),
		    courseItem(CourseItem::Control, QStringLiteral("31")),
		    courseItem(CourseItem::Control, QStringLiteral("32")),
		    courseItem(CourseItem::Finish, QStringLiteral("F1")) }));
		data.courses.push_back(course(QStringLiteral("B"), {
		    courseItem(CourseItem::Start, QStringLiteral("S1")),
		    courseItem(CourseItem::Control, QStringLiteral("32")),
		    courseItem(CourseItem::Finish, QStringLiteral("F1")) }));
		CourseClass first;
		first.name = QStringLiteral("H21");
		first.course = QStringLiteral("A");
		first.runners = 40;
		data.classes.push_back(first);
		CourseClass second;
		second.name = QStringLiteral("D21");
		second.course = QStringLiteral("B");
		data.classes.push_back(second);
		CourseOverride number;
		number.course = QStringLiteral("A");
		number.what = CourseOverride::Number;
		number.code_from = QStringLiteral("32");
		data.overrides.push_back(number);
		CourseOverride line;
		line.course = QStringLiteral("A");
		line.what = CourseOverride::Line;
		line.code_from = QStringLiteral("S1");
		line.code_to = QStringLiteral("31");
		data.overrides.push_back(line);
		changeCourseData(m.map, data);
	}
};

QTreeWidgetItem* rowWithText(QTreeWidget* tree, int column, const QString& text)
{
	for (int i = 0; i < tree->topLevelItemCount(); ++i)
	{
		if (tree->topLevelItem(i)->text(column) == text)
			return tree->topLevelItem(i);
	}
	return nullptr;
}

}  // namespace



/**
 * @test Tests the course dock and what it rests on: the next code, the
 * rule which keeps the course objects in step with the objects of the
 * map, and the changes of codes, courses, items and classes.
 *
 * See the design notes, "Course dock".
 */
class CourseDockTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void codeTest();
	void drawTest();
	void deleteTest();
	void roleTest();
	void renameTest();
	void courseTest();
	void itemTest();
	void dockTest();
};


void CourseDockTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseDockTest::codeTest()
{
	CourseData data;
	QCOMPARE(nextCourseCode(data, CourseObject::Control), QStringLiteral("31"));
	QCOMPARE(nextCourseCode(data, CourseObject::Start), QStringLiteral("S1"));
	QCOMPARE(nextCourseCode(data, CourseObject::MarkedRoute), QStringLiteral("M1"));
	QCOMPARE(nextCourseCode(data, CourseObject::Finish), QStringLiteral("F1"));
	QCOMPARE(nextCourseCode(data, CourseObject::Description), QStringLiteral("D1"));
	QCOMPARE(nextCourseCode(data, CourseObject::CourseTitle), QStringLiteral("N1"));
	QCOMPARE(nextCourseCode(data, CourseObject::VariantCode), QStringLiteral("V1"));
	QCOMPARE(nextCourseCode(data, CourseObject::StartNumber), QStringLiteral("U1"));
	QCOMPARE(nextCourseCode(data, CourseObject::TextBlock), QStringLiteral("T1"));
	
	// The highest code plus one; a gap is not filled.
	for (int code = 31; code <= 69; ++code)
		data.objects.push_back(courseObject(CourseObject::Control, QString::number(code)));
	data.objects.push_back(courseObject(CourseObject::Control, QStringLiteral("71")));
	QCOMPARE(nextCourseCode(data, CourseObject::Control), QStringLiteral("72"));
	
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S1")));
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S2")));
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S4")));
	QCOMPARE(nextCourseCode(data, CourseObject::Start), QStringLiteral("S5"));
	QCOMPARE(nextCourseCode(data, CourseObject::Finish), QStringLiteral("F1"));
	
	// Codes which are no letter and number, or no number, count for nothing.
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("S")));
	data.objects.push_back(courseObject(CourseObject::Start, QStringLiteral("Sx")));
	data.objects.push_back(courseObject(CourseObject::Control, QStringLiteral("31a")));
	data.objects.push_back(courseObject(CourseObject::Control, QStringLiteral("F9")));
	QCOMPARE(nextCourseCode(data, CourseObject::Start), QStringLiteral("S5"));
	QCOMPARE(nextCourseCode(data, CourseObject::Control), QStringLiteral("72"));
	// A code is taken whatever the kind of its object.
	QCOMPARE(nextCourseCode(data, CourseObject::Finish), QStringLiteral("F10"));
	
	QVERIFY(isValidCourseCode(QStringLiteral("31")));
	QVERIFY(isValidCourseCode(QStringLiteral("S1")));
	QVERIFY(!isValidCourseCode(QString{}));
	QVERIFY(!isValidCourseCode(QStringLiteral("3 1")));
	QVERIFY(!isValidCourseCode(QStringLiteral("31\t")));
}


/// Accept (2)
void CourseDockTest::drawTest()
{
	CourseMap m;
	auto& undo = m.map.undoManager();
	QVERIFY(m.map.courseData().isEmpty());
	
	auto* first = m.draw(m.control);
	auto const uuid = first->uuid();
	QCOMPARE(int(m.map.courseData().objects.size()), 1);
	QCOMPARE(m.map.courseData().objects[0].kind, CourseObject::Control);
	QCOMPARE(m.map.courseData().objects[0].code, QStringLiteral("31"));
	QCOMPARE(m.map.courseData().objects[0].object, uuid);
	QCOMPARE(undo.undoStepCount(), 1);
	QCOMPARE(undo.nextUndoStep()->getType(), UndoStep::CombinedUndoStepType);
	
	QVERIFY(undo.undo());
	QVERIFY(!m.map.findObject(uuid));
	QVERIFY(m.map.courseData().isEmpty());
	QVERIFY(!undo.canUndo());
	
	QVERIFY(undo.redo());
	QVERIFY(m.map.findObject(uuid));
	QCOMPARE(objectsOf(m.map.courseData()), QStringLiteral("31"));
	QCOMPARE(m.map.courseData().objects[0].object, uuid);
	
	m.draw(m.control);
	m.draw(m.start);
	m.draw(m.finish);
	m.draw(m.description);
	QCOMPARE(objectsOf(m.map.courseData()), QStringLiteral("31 32 S1 F1 D1"));
	QCOMPARE(m.map.courseData().objects[2].kind, CourseObject::Start);
	QCOMPARE(m.map.courseData().objects[3].kind, CourseObject::Finish);
	QCOMPARE(m.map.courseData().objects[4].kind, CourseObject::Description);
	
	// A symbol without a role: nothing changes, and the step is the tool's own.
	auto const before = m.map.courseData();
	QSignalSpy changes(&m.map, &Map::courseDataChanged);
	m.draw(m.plain);
	QVERIFY(m.map.courseData() == before);
	QCOMPARE(changes.count(), 0);
	QCOMPARE(undo.nextUndoStep()->getType(), UndoStep::DeleteObjectsUndoStepType);
	
	// The objects of the overrides are no course objects.
	auto* part = ensureCourseOverridesPart(m.map);
	auto* hidden = new PointObject(m.control);
	auto const part_index = m.map.findPartIndex(part);
	auto const index = m.map.addObject(hidden, part_index);
	Q_UNUSED(index)
	auto data = m.map.courseData();
	QVERIFY(!syncCourseObjects(m.map, data));
	QVERIFY(data == before);
}


/// Accept (3)
void CourseDockTest::deleteTest()
{
	Event event;
	auto& map = event.m.map;
	auto const before = map.courseData();
	QCOMPARE(objectsOf(before), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(itemsOf(before, QStringLiteral("A")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(coursesUsing(before, QStringLiteral("32")), (QStringList{ QStringLiteral("A"), QStringLiteral("B") }));
	QCOMPARE(coursesUsing(before, QStringLiteral("31")), QStringList{ QStringLiteral("A") });
	
	QSignalSpy removed(&map, &Map::courseObjectsRemovedFromCourses);
	auto const uuid = event.object_32->uuid();
	auto const steps = map.undoManager().undoStepCount();
	event.m.erase(event.object_32);
	QVERIFY(!map.findObject(uuid));
	
	auto const& data = map.courseData();
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 F1"));
	// The override of the number of 32 went; that of the line from S1 to 31 stays.
	QCOMPARE(int(data.overrides.size()), 1);
	QCOMPARE(data.overrides[0].what, CourseOverride::Line);
	QCOMPARE(int(data.classes.size()), 2);
	QCOMPARE(removed.count(), 1);
	QCOMPARE(removed[0][0].toStringList(), QStringList{ QStringLiteral("32") });
	QCOMPARE(removed[0][1].toStringList(), (QStringList{ QStringLiteral("A"), QStringLiteral("B") }));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	
	QVERIFY(map.undoManager().undo());
	QVERIFY(map.findObject(uuid));
	QVERIFY(map.courseData() == before);
	QCOMPARE(removed.count(), 1);
	
	// A control which no course uses goes without a word.
	auto* lone = event.m.draw(event.m.control);
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 31 32 F1 33"));
	event.m.erase(lone);
	QVERIFY(map.courseData() == before);
	QCOMPARE(removed.count(), 1);
}


/// Accept (4)
void CourseDockTest::roleTest()
{
	Event event;
	auto& map = event.m.map;
	auto const before = map.courseData();
	
	auto* step = new SwitchSymbolUndoStep(&map);
	step->addObject(map.getCurrentPart()->findObjectIndex(event.object_31), event.m.control);
	QVERIFY(event.object_31->setSymbol(event.m.finish, true));
	map.push(step);
	
	auto const& data = map.courseData();
	QCOMPARE(objectsOf(data), QStringLiteral("S1 32 F1 F2"));
	QCOMPARE(data.objects[3].kind, CourseObject::Finish);
	QCOMPARE(data.objects[3].object, event.object_31->uuid());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 32 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	
	QVERIFY(map.undoManager().undo());
	QVERIFY(map.courseData() == before);
	QCOMPARE(event.object_31->getSymbol(), static_cast<const Symbol*>(event.m.control));
	
	// A symbol without a role: the object is no course object any more.
	step = new SwitchSymbolUndoStep(&map);
	step->addObject(map.getCurrentPart()->findObjectIndex(event.object_32), event.m.control);
	QVERIFY(event.object_32->setSymbol(event.m.plain, true));
	map.push(step);
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 31 F1"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 F1"));
	QVERIFY(map.undoManager().undo());
	QVERIFY(map.courseData() == before);
	
	// A map whose symbols have no roles: the course objects are left as they are.
	Map plain_map;
	auto* symbol = new PointSymbol();
	plain_map.addSymbol(symbol, 0);
	auto* object = new PointObject(symbol);
	plain_map.addObject(object);
	CourseData plain_data;
	plain_data.objects.push_back(courseObject(CourseObject::Control, QStringLiteral("31")));
	plain_data.objects[0].object = object->uuid();
	plain_data.objects.push_back(courseObject(CourseObject::Control, QStringLiteral("32")));
	plain_data.objects[1].object = QUuid::createUuid();
	changeCourseData(plain_map, plain_data);
	// The object of 31 is there and stays; that of 32 is not in the map.
	QCOMPARE(objectsOf(plain_map.courseData()), QStringLiteral("31"));
	QVERIFY(plain_map.undoManager().undo());
	QVERIFY(plain_map.courseData().isEmpty());
}


/// Accept (5)
void CourseDockTest::renameTest()
{
	Event event;
	auto data = event.m.map.courseData();
	auto const before = data;
	
	QVERIFY(renameCourseObject(data, QStringLiteral("31"), QStringLiteral("45")));
	QCOMPARE(objectsOf(data), QStringLiteral("S1 45 32 F1"));
	QCOMPARE(data.objects[1].object, event.object_31->uuid());
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 45 32 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	QCOMPARE(data.overrides[1].code_to, QStringLiteral("45"));
	QVERIFY(renameCourseObject(data, QStringLiteral("32"), QStringLiteral("46")));
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 46 F1"));
	QCOMPARE(data.overrides[0].code_from, QStringLiteral("46"));
	
	data = before;
	QVERIFY(!renameCourseObject(data, QStringLiteral("31"), QStringLiteral("32")));
	QVERIFY(!renameCourseObject(data, QStringLiteral("31"), QStringLiteral("S1")));
	QVERIFY(!renameCourseObject(data, QStringLiteral("31"), QString{}));
	QVERIFY(!renameCourseObject(data, QStringLiteral("31"), QStringLiteral("4 5")));
	QVERIFY(!renameCourseObject(data, QStringLiteral("99"), QStringLiteral("45")));
	QVERIFY(data == before);
	// The code which the object has is no change and no mistake.
	QVERIFY(renameCourseObject(data, QStringLiteral("31"), QStringLiteral("31")));
	QVERIFY(data == before);
}


/// Accept (6)
void CourseDockTest::courseTest()
{
	Event event;
	auto data = event.m.map.courseData();
	auto const before = data;
	
	QVERIFY(addCourse(data, QStringLiteral("C")));
	QCOMPARE(int(data.courses.size()), 3);
	QCOMPARE(data.courses[2].name, QStringLiteral("C"));
	QCOMPARE(data.courses[2].type, Course::Normal);
	QVERIFY(data.courses[2].items.empty());
	
	QVERIFY(duplicateCourse(data, QStringLiteral("A"), QStringLiteral("A long")));
	QCOMPARE(int(data.courses.size()), 4);
	QCOMPARE(itemsOf(data, QStringLiteral("A long")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(int(data.classes.size()), 2);
	QCOMPARE(int(data.overrides.size()), 2);
	
	QVERIFY(renameCourse(data, QStringLiteral("A"), QStringLiteral("Long")));
	QVERIFY(!data.findCourse(QStringLiteral("A")));
	QCOMPARE(itemsOf(data, QStringLiteral("Long")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(data.courses[0].name, QStringLiteral("Long"));
	QCOMPARE(data.classes[0].course, QStringLiteral("Long"));
	QCOMPARE(data.classes[1].course, QStringLiteral("B"));
	QCOMPARE(data.overrides[0].course, QStringLiteral("Long"));
	QCOMPARE(data.overrides[1].course, QStringLiteral("Long"));
	
	QVERIFY(removeCourse(data, QStringLiteral("Long")));
	QCOMPARE(int(data.courses.size()), 3);
	QCOMPARE(int(data.classes.size()), 1);
	QCOMPARE(data.classes[0].name, QStringLiteral("D21"));
	QVERIFY(data.overrides.empty());
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 32 F1"));
	
	data = before;
	QVERIFY(!addCourse(data, QString{}));
	QVERIFY(!addCourse(data, QStringLiteral("  ")));
	QVERIFY(!addCourse(data, QStringLiteral("A\tB")));
	QVERIFY(!addCourse(data, QStringLiteral("A")));
	QVERIFY(!duplicateCourse(data, QStringLiteral("A"), QStringLiteral("B")));
	QVERIFY(!duplicateCourse(data, QStringLiteral("Z"), QStringLiteral("C")));
	QVERIFY(!renameCourse(data, QStringLiteral("A"), QStringLiteral("B")));
	QVERIFY(!renameCourse(data, QStringLiteral("Z"), QStringLiteral("C")));
	QVERIFY(!removeCourse(data, QStringLiteral("Z")));
	QVERIFY(data == before);
}


/// Accept (7)
void CourseDockTest::itemTest()
{
	Event event;
	auto data = event.m.map.courseData();
	data.objects.push_back(courseObject(CourseObject::MarkedRoute, QStringLiteral("M1")));
	data.objects.push_back(courseObject(CourseObject::TextBlock, QStringLiteral("T1")));
	data.objects.push_back(courseObject(CourseObject::Description, QStringLiteral("D1")));
	data.objects.push_back(courseObject(CourseObject::CourseTitle, QStringLiteral("N1")));
	auto const before = data;
	auto const b = QStringLiteral("B");
	
	QVERIFY(insertCourseItem(data, b, 1, QStringLiteral("31")));
	QCOMPARE(itemsOf(data, b), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(data.courses[1].items[1].type, CourseItem::Control);
	QVERIFY(insertCourseItem(data, b, 4, QStringLiteral("M1")));
	QCOMPARE(data.courses[1].items[4].type, CourseItem::MarkedRoute);
	QVERIFY(insertCourseItem(data, b, 0, QStringLiteral("S1")));
	QCOMPARE(data.courses[1].items[0].type, CourseItem::Start);
	QVERIFY(insertCourseItem(data, b, 6, QStringLiteral("F1")));
	QCOMPARE(data.courses[1].items[6].type, CourseItem::Finish);
	QVERIFY(insertCourseItem(data, b, 1, QStringLiteral("T1")));
	QCOMPARE(data.courses[1].items[1].type, CourseItem::TextBlock);
	QCOMPARE(itemsOf(data, b), QStringLiteral("S1 T1 S1 31 32 F1 M1 F1"));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 32 F1"));
	
	auto changed = data;
	QVERIFY(!insertCourseItem(data, b, 1, QStringLiteral("D1")));
	QVERIFY(!insertCourseItem(data, b, 1, QStringLiteral("N1")));
	QVERIFY(!insertCourseItem(data, b, 1, QStringLiteral("99")));
	QVERIFY(!insertCourseItem(data, b, 9, QStringLiteral("31")));
	QVERIFY(!insertCourseItem(data, b, -1, QStringLiteral("31")));
	QVERIFY(!insertCourseItem(data, QStringLiteral("Z"), 0, QStringLiteral("31")));
	QVERIFY(data == changed);
	
	data = before;
	QVERIFY(removeCourseItem(data, QStringLiteral("A"), 1));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 32 F1"));
	QVERIFY(!removeCourseItem(data, QStringLiteral("A"), 3));
	QVERIFY(!removeCourseItem(data, QStringLiteral("A"), -1));
	
	data = before;
	QVERIFY(moveCourseItem(data, QStringLiteral("A"), 1, 2));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 32 31 F1"));
	QVERIFY(moveCourseItem(data, QStringLiteral("A"), 2, 0));
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("31 S1 32 F1"));
	QVERIFY(!moveCourseItem(data, QStringLiteral("A"), 0, 4));
	QVERIFY(!moveCourseItem(data, QStringLiteral("A"), 4, 0));
	QVERIFY(!moveCourseItem(data, QStringLiteral("A"), 1, 1));
	
	// A team variation: its own items stay where they are, a control does not.
	data = before;
	auto fork = courseItem(CourseItem::TeamForkBegin);
	fork.count = 2;
	data.courses.push_back(course(QStringLiteral("Relay"), {
	    courseItem(CourseItem::Start, QStringLiteral("S1")),
	    fork,
	    courseItem(CourseItem::ForkBranch),
	    courseItem(CourseItem::Control, QStringLiteral("31")),
	    courseItem(CourseItem::ForkBranch),
	    courseItem(CourseItem::Control, QStringLiteral("32")),
	    courseItem(CourseItem::TeamForkEnd),
	    courseItem(CourseItem::Finish, QStringLiteral("F1")) }));
	auto const relay = QStringLiteral("Relay");
	for (auto index : { 1, 2, 4, 6 })
	{
		QVERIFY(isVariationItem(data.courses[2].items[std::size_t(index)].type));
		QVERIFY(!removeCourseItem(data, relay, index));
		QVERIFY(!moveCourseItem(data, relay, index, index + 1));
	}
	QVERIFY(!isVariationItem(CourseItem::Control));
	QVERIFY(!isVariationItem(CourseItem::MapExchange));
	QVERIFY(moveCourseItem(data, relay, 3, 5));
	QCOMPARE(itemsOf(data, relay), QStringLiteral("S1 - - - 32 31 - F1"));
	QVERIFY(removeCourseItem(data, relay, 5));
	QCOMPARE(itemsOf(data, relay), QStringLiteral("S1 - - - 32 - F1"));
}


/// Accept (8)
void CourseDockTest::dockTest()
{
	Event event;
	auto& map = event.m.map;
	auto* lone = event.m.draw(event.m.control);
	Q_UNUSED(lone)
	
	CourseWidget widget(&map);
	auto* objects = widget.findChild<QTreeWidget*>(QStringLiteral("course-objects"));
	auto* selector = widget.findChild<QComboBox*>(QStringLiteral("course-selector"));
	auto* items = widget.findChild<QTreeWidget*>(QStringLiteral("course-items"));
	auto* classes = widget.findChild<QTableWidget*>(QStringLiteral("course-classes"));
	QVERIFY(objects);
	QVERIFY(selector);
	QVERIFY(items);
	QVERIFY(classes);
	
	// The course objects: code, kind, courses; 33 is in no course.
	QCOMPARE(objects->topLevelItemCount(), 5);
	QStringList codes;
	for (int i = 0; i < objects->topLevelItemCount(); ++i)
		codes.push_back(objects->topLevelItem(i)->text(0));
	QCOMPARE(codes.join(QLatin1Char(' ')), QStringLiteral("S1 31 32 33 F1"));
	auto* row_31 = rowWithText(objects, 0, QStringLiteral("31"));
	auto* row_32 = rowWithText(objects, 0, QStringLiteral("32"));
	auto* row_33 = rowWithText(objects, 0, QStringLiteral("33"));
	QVERIFY(row_31 && row_32 && row_33);
	QCOMPARE(row_31->text(1), CourseWidget::tr("Control"));
	QCOMPARE(rowWithText(objects, 0, QStringLiteral("S1"))->text(1), CourseWidget::tr("Start"));
	QCOMPARE(row_31->text(2), QStringLiteral("1"));
	QCOMPARE(row_32->text(2), QStringLiteral("2"));
	QCOMPARE(row_33->text(2), QStringLiteral("0"));
	QVERIFY(row_33->data(0, CourseWidget::UnusedRole).toBool());
	QVERIFY(!row_31->data(0, CourseWidget::UnusedRole).toBool());
	
	// The selector: all course objects, then the courses.
	QCOMPARE(selector->count(), 3);
	QVERIFY(widget.currentCourse().isEmpty());
	QCOMPARE(items->topLevelItemCount(), 0);
	widget.setCurrentCourse(QStringLiteral("A"));
	QCOMPARE(selector->currentText(), QStringLiteral("A"));
	QCOMPARE(widget.currentCourse(), QStringLiteral("A"));
	QCOMPARE(items->topLevelItemCount(), 4);
	QStringList lines;
	for (int i = 0; i < items->topLevelItemCount(); ++i)
		lines.push_back(items->topLevelItem(i)->text(0) + QLatin1Char('|') + items->topLevelItem(i)->text(1));
	QCOMPARE(lines.join(QLatin1Char(' ')), QStringLiteral("|S1 1|31 2|32 |F1"));
	QCOMPARE(classes->rowCount(), 1);
	QCOMPARE(classes->item(0, 0)->text(), QStringLiteral("H21"));
	QCOMPARE(classes->item(0, 1)->text(), QStringLiteral("40"));
	QCOMPARE(classes->item(0, 2)->text(), QString{});
	
	// A row selects its object in the map, and an object its row.
	objects->clearSelection();
	objects->setCurrentItem(rowWithText(objects, 0, QStringLiteral("31")));
	QCOMPARE(map.getNumSelectedObjects(), 1);
	QCOMPARE(map.getFirstSelectedObject(), static_cast<Object*>(event.object_31));
	map.clearObjectSelection(false);
	map.addObjectToSelection(event.object_32, true);
	QCOMPARE(objects->selectedItems().size(), 1);
	QCOMPARE(objects->selectedItems().front()->text(0), QStringLiteral("32"));
	QCOMPARE(map.getFirstSelectedObject(), static_cast<Object*>(event.object_32));
	
	// A double click puts the object into the course, behind the current item.
	widget.setCurrentCourse(QStringLiteral("B"));
	QCOMPARE(items->topLevelItemCount(), 3);
	items->setCurrentItem(items->topLevelItem(0));
	auto const steps = map.undoManager().undoStepCount();
	emit objects->itemDoubleClicked(rowWithText(objects, 0, QStringLiteral("31")), 0);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 31 32 F1"));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(items->topLevelItemCount(), 4);
	QCOMPARE(items->currentItem()->text(1), QStringLiteral("31"));
	QCOMPARE(rowWithText(objects, 0, QStringLiteral("31"))->text(2), QStringLiteral("2"));
	QCOMPARE(widget.currentCourse(), QStringLiteral("B"));
	QVERIFY(map.undoManager().undo());
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	QCOMPARE(items->topLevelItemCount(), 3);
	QCOMPARE(widget.currentCourse(), QStringLiteral("B"));
	
	// Without a current item the object goes to the end.
	items->setCurrentItem(nullptr);
	objects->clearSelection();
	objects->setCurrentItem(rowWithText(objects, 0, QStringLiteral("33")));
	widget.insertSelectedObjects();
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1 33"));
	QVERIFY(!rowWithText(objects, 0, QStringLiteral("33"))->data(0, CourseWidget::UnusedRole).toBool());
	// Remove, up and down act on the current item.
	items->setCurrentItem(items->topLevelItem(3));
	widget.moveCurrentItem(-1);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 33 F1"));
	QCOMPARE(items->currentItem()->text(1), QStringLiteral("33"));
	widget.moveCurrentItem(1);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1 33"));
	widget.removeCurrentItem();
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	
	// The code of an object.
	QVERIFY(!widget.changeCode(QStringLiteral("31"), QStringLiteral("32")));
	QVERIFY(widget.changeCode(QStringLiteral("31"), QStringLiteral("45")));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 45 32 F1"));
	QVERIFY(rowWithText(objects, 0, QStringLiteral("45")));
	QVERIFY(!rowWithText(objects, 0, QStringLiteral("31")));
	
	// The classes of the course.
	widget.setCurrentCourse(QStringLiteral("A"));
	QVERIFY(!widget.addClass(QStringLiteral("H21")));
	QVERIFY(!widget.addClass(QString{}));
	QVERIFY(widget.addClass(QStringLiteral("H35")));
	QCOMPARE(classes->rowCount(), 2);
	QCOMPARE(int(map.courseData().classes.size()), 3);
	QCOMPARE(map.courseData().classes[2].name, QStringLiteral("H35"));
	QCOMPARE(map.courseData().classes[2].course, QStringLiteral("A"));
	QCOMPARE(map.courseData().classes[2].runners, -1);
	classes->item(1, 1)->setText(QStringLiteral("20"));
	QCOMPARE(map.courseData().classes[2].runners, 20);
	classes->item(1, 2)->setText(QStringLiteral("101"));
	classes->item(1, 3)->setText(QStringLiteral("120"));
	QCOMPARE(map.courseData().classes[2].first_no, 101);
	QCOMPARE(map.courseData().classes[2].last_no, 120);
	classes->item(1, 1)->setText(QString{});
	QCOMPARE(map.courseData().classes[2].runners, -1);
	// What is no number is not taken, and the cell shows what the class has.
	classes->item(1, 2)->setText(QStringLiteral("abc"));
	QCOMPARE(map.courseData().classes[2].first_no, 101);
	QCOMPARE(classes->item(1, 2)->text(), QStringLiteral("101"));
	// The name of a class: not that of another one.
	classes->item(1, 0)->setText(QStringLiteral("H21"));
	QCOMPARE(map.courseData().classes[2].name, QStringLiteral("H35"));
	classes->item(1, 0)->setText(QStringLiteral("H40"));
	QCOMPARE(map.courseData().classes[2].name, QStringLiteral("H40"));
	classes->setCurrentCell(1, 0);
	widget.removeCurrentClass();
	QCOMPARE(int(map.courseData().classes.size()), 2);
	QCOMPARE(classes->rowCount(), 1);
	
	// The courses.
	QVERIFY(widget.addCourse(QStringLiteral("C")));
	QCOMPARE(widget.currentCourse(), QStringLiteral("C"));
	QCOMPARE(selector->count(), 4);
	QVERIFY(!widget.renameCurrentCourse(QStringLiteral("A")));
	QVERIFY(widget.renameCurrentCourse(QStringLiteral("Short")));
	QCOMPARE(widget.currentCourse(), QStringLiteral("Short"));
	QVERIFY(widget.duplicateCurrentCourse(QStringLiteral("Shorter")));
	QCOMPARE(widget.currentCourse(), QStringLiteral("Shorter"));
	QCOMPARE(int(map.courseData().courses.size()), 4);
	widget.removeCurrentCourse();
	QCOMPARE(int(map.courseData().courses.size()), 3);
	QVERIFY(!map.courseData().findCourse(QStringLiteral("Shorter")));
	QVERIFY(widget.currentCourse().isEmpty());
	
	// Deleting an object from the dock deletes the map object, as one step.
	auto const uuid = event.object_32->uuid();
	auto const count = map.undoManager().undoStepCount();
	QVERIFY(widget.deleteObject(QStringLiteral("32")));
	QVERIFY(!map.findObject(uuid));
	QCOMPARE(objectsOf(map.courseData()), QStringLiteral("S1 45 F1 33"));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 F1"));
	QCOMPARE(objects->topLevelItemCount(), 4);
	QCOMPARE(map.undoManager().undoStepCount(), count + 1);
	QVERIFY(map.undoManager().undo());
	QVERIFY(map.findObject(uuid));
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("B")), QStringLiteral("S1 32 F1"));
	QCOMPARE(objects->topLevelItemCount(), 5);
}



QTEST_MAIN(CourseDockTest)
#include "course_dock_t.moc"  // IWYU pragma: keep
