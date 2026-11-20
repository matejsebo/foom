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
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMenu>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "global.h"
#include "check/issue.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_edit.h"
#include "course/course_parts.h"
#include "course/course_render.h"
#include "course/course_report.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/widgets/course_widget.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const char* exchange_items = "M1 S2 36 37 61 62 54 53 g S5 52 51 65 59 60 34 40 M5 g S1 33 38 58 56 57 63 M4 F3";
const char* flip_items = "M1 S2 36 37 61 62 54 53 x S5 52 51 65 59 60 34 40 35 F1";

/// A course of items which are written as the OCD records have them.
Course courseOf(const QString& name, const char* items, Course::Type type = Course::Normal, int legs = 0)
{
	Course course;
	course.name = name;
	course.type = type;
	course.legs = legs;
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		CourseItem item;
		auto const first = word.at(0);
		if (word == QLatin1String("g"))
		{
			item.type = CourseItem::MapExchange;
		}
		else if (word == QLatin1String("x"))
		{
			item.type = CourseItem::MapFlip;
		}
		else if (word == QLatin1String("k"))
		{
			item.type = CourseItem::CrossingPoint;
		}
		else if (word == QLatin1String("w"))
		{
			item.type = CourseItem::Passage;
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

/// The items as the records write them.
QString wordsOf(const std::vector<CourseItem>& items)
{
	QStringList words;
	for (auto const& item : items)
	{
		switch (item.type)
		{
		case CourseItem::MapExchange:    words.push_back(QStringLiteral("g")); break;
		case CourseItem::MapFlip:        words.push_back(QStringLiteral("x")); break;
		case CourseItem::CrossingPoint:  words.push_back(QStringLiteral("k")); break;
		case CourseItem::Passage:        words.push_back(QStringLiteral("w")); break;
		default:                         words.push_back(item.namesObject() ? item.code : QStringLiteral("?")); break;
		}
	}
	return words.join(QLatin1Char(' '));
}

PointSymbol* addSymbol(Map& map, int number, Symbol::CourseRole role)
{
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

void draw(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

/// A map with the starts S1 and S2, the controls 31 to 34, the finish F1
/// and a description object, and with courses of them.
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto const* start = addSymbol(map, 701, Symbol::CourseStart);
	auto const* control = addSymbol(map, 703, Symbol::CourseControl);
	auto const* finish = addSymbol(map, 706, Symbol::CourseFinish);
	auto const* description = addSymbol(map, 720, Symbol::CourseDescription);
	draw(map, start, 0.0, 0.0);
	draw(map, control, 10.0, 0.0);
	draw(map, control, 20.0, 0.0);
	draw(map, start, 20.0, 10.0);
	draw(map, control, 30.0, 10.0);
	draw(map, control, 40.0, 10.0);
	draw(map, finish, 50.0, 10.0);
	draw(map, description, -60.0, -60.0);
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("X"), "S1 31 32 g S2 33 34 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Twice"), "S1 31 32 g S2 31 34 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Plain"), "S1 31 32 33 F1"));
	changeCourseData(map, data);
}

QStringList numbersOf(const CourseDrawing& drawing)
{
	QStringList texts;
	for (auto const& number : drawing.numbers)
		texts.push_back(number.code + QLatin1Char('=') + number.text);
	return texts;
}

QStringList legsOf(const CourseDrawing& drawing)
{
	QStringList texts;
	for (auto const& leg : drawing.legs)
		texts.push_back(leg.from + QLatin1Char('-') + leg.to);
	return texts;
}

std::vector<Issue> issuesOf(const std::vector<Issue>& issues, const char* rule, const QString& subject)
{
	std::vector<Issue> result;
	for (auto const& issue : issues)
	{
		if (issue.rule == rule && issue.subject == subject)
			result.push_back(issue);
	}
	return result;
}

}  // namespace



/**
 * @test Tests the parts of a course: map exchange and flip.
 *
 * See the design notes, "The parts of a course: map
 * exchange and flip".
 */
class CoursePartsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void partsTest();
	void drawingTest();
	void descriptionTest();
	void markerTest();
	void checkTest();
	void dockTest();
};


void CoursePartsTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CoursePartsTest::partsTest()
{
	auto const exchange = courseOf(QStringLiteral("Map Exchange"), exchange_items);
	auto parts = courseParts(exchange);
	QCOMPARE(int(parts.size()), 3);
	QCOMPARE(parts[0].first, 0);
	QCOMPARE(parts[0].end, 8);
	QCOMPARE(parts[0].ends_with, CoursePart::MapExchange);
	QCOMPARE(parts[1].first, 9);
	QCOMPARE(parts[1].end, 18);
	QCOMPARE(parts[1].ends_with, CoursePart::MapExchange);
	QCOMPARE(parts[2].first, 19);
	QCOMPARE(parts[2].end, 28);
	QCOMPARE(parts[2].ends_with, CoursePart::CourseEnd);
	QCOMPARE(coursePartOfItem(exchange, 7), 0);
	QCOMPARE(coursePartOfItem(exchange, 8), 0);
	QCOMPARE(coursePartOfItem(exchange, 9), 1);
	QCOMPARE(coursePartOfItem(exchange, 27), 2);
	QCOMPARE(coursePartOfItem(exchange, 28), -1);
	QCOMPARE(coursePartOfItem(exchange, -1), -1);
	
	parts = courseParts(courseOf(QStringLiteral("Map Flip"), flip_items));
	QCOMPARE(int(parts.size()), 2);
	QCOMPARE(parts[0].end, 8);
	QCOMPARE(parts[0].ends_with, CoursePart::MapFlip);
	QCOMPARE(parts[1].first, 9);
	QCOMPARE(parts[1].end, 19);
	
	parts = courseParts(courseOf(QStringLiteral("Plain"), "S1 31 32 F1"));
	QCOMPARE(int(parts.size()), 1);
	QCOMPARE(parts[0].first, 0);
	QCOMPARE(parts[0].end, 4);
	QCOMPARE(parts[0].ends_with, CoursePart::CourseEnd);
	
	parts = courseParts(Course{});
	QCOMPARE(int(parts.size()), 1);
	QCOMPARE(parts[0].first, 0);
	QCOMPARE(parts[0].end, 0);
}


/// Accept (2)
void CoursePartsTest::drawingTest()
{
	Map map;
	fillMap(map);
	auto const& data = map.courseData();
	QVERIFY(data.findObject(QStringLiteral("S2")) && data.findObject(QStringLiteral("34")) && data.findObject(QStringLiteral("D1")));
	
	auto drawing = drawCourse(map, data, QStringLiteral("X"));
	QVERIFY(drawing.drawn);
	QCOMPARE(legsOf(drawing).join(QLatin1Char(' ')), QStringLiteral("S1-31 31-32 S2-33 33-34 34-F1"));
	QCOMPARE(numbersOf(drawing).join(QLatin1Char(' ')), QStringLiteral("31=1 32=2 33=3 34=4"));
	QCOMPARE(int(drawing.starts.size()), 2);
	// The same as the whole.
	auto const whole = drawCourse(map, data, QStringLiteral("X"), -1);
	QCOMPARE(legsOf(whole), legsOf(drawing));
	QCOMPARE(numbersOf(whole), numbersOf(drawing));
	
	drawing = drawCourse(map, data, QStringLiteral("X"), 0);
	QVERIFY(drawing.drawn);
	QCOMPARE(legsOf(drawing).join(QLatin1Char(' ')), QStringLiteral("S1-31 31-32"));
	QCOMPARE(numbersOf(drawing).join(QLatin1Char(' ')), QStringLiteral("31=1 32=2"));
	QCOMPARE(int(drawing.starts.size()), 1);
	QCOMPARE(drawing.starts[0].code, QStringLiteral("S1"));
	
	drawing = drawCourse(map, data, QStringLiteral("X"), 1);
	QVERIFY(drawing.drawn);
	QCOMPARE(legsOf(drawing).join(QLatin1Char(' ')), QStringLiteral("S2-33 33-34 34-F1"));
	QCOMPARE(numbersOf(drawing).join(QLatin1Char(' ')), QStringLiteral("33=3 34=4"));
	QCOMPARE(int(drawing.starts.size()), 1);
	QCOMPARE(drawing.starts[0].code, QStringLiteral("S2"));
	// The number of 33 stands as in the whole: its neighbours are those of its part.
	QVERIFY(drawing.numbers[0].anchor == whole.numbers[2].anchor);
	
	drawing = drawCourse(map, data, QStringLiteral("X"), 2);
	QVERIFY(!drawing.drawn);
	QVERIFY(drawing.legs.empty() && drawing.numbers.empty() && drawing.starts.empty());
	
	// A control which is visited in two parts.
	QCOMPARE(numbersOf(drawCourse(map, data, QStringLiteral("Twice"))).join(QLatin1Char(' ')), QStringLiteral("31=1/3 32=2 34=4"));
	QCOMPARE(numbersOf(drawCourse(map, data, QStringLiteral("Twice"), 0)).join(QLatin1Char(' ')), QStringLiteral("31=1 32=2"));
	QCOMPARE(numbersOf(drawCourse(map, data, QStringLiteral("Twice"), 1)).join(QLatin1Char(' ')), QStringLiteral("31=3 34=4"));
	
	// A course without parts is its part 0.
	QCOMPARE(legsOf(drawCourse(map, data, QStringLiteral("Plain"), 0)), legsOf(drawCourse(map, data, QStringLiteral("Plain"))));
	QVERIFY(!drawCourse(map, data, QStringLiteral("Plain"), 1).drawn);
}


/// Accept (3)
void CoursePartsTest::descriptionTest()
{
	Map map;
	fillMap(map);
	auto const& data = map.courseData();
	auto const description = describeCourse(map, data, QStringLiteral("X"));
	QVERIFY(description.known);
	QCOMPARE(description.parts, 2);
	
	auto const whole = drawCourseDescriptions(map, data, QStringLiteral("X"));
	QVERIFY(whole.drawn);
	QCOMPARE(int(whole.layouts.size()), 1);
	QCOMPARE(whole.layouts[0].rows.size(), layoutCourseDescription(description, data.options).rows.size());
	
	auto const second = drawCourseDescriptions(map, data, QStringLiteral("X"), 1);
	QVERIFY(second.drawn);
	QCOMPARE(int(second.layouts.size()), 1);
	QCOMPARE(second.layouts[0].rows.size(), layoutCourseDescription(description, data.options, 1).rows.size());
	QVERIFY(second.layouts[0].rows.size() < whole.layouts[0].rows.size());
	
	auto const first = drawCourseDescriptions(map, data, QStringLiteral("X"), 0);
	QCOMPARE(first.layouts[0].rows.size(), layoutCourseDescription(description, data.options, 0).rows.size());
	QVERIFY(!drawCourseDescriptions(map, data, QStringLiteral("X"), 2).drawn);
}


/// Accept (4)
void CoursePartsTest::markerTest()
{
	CourseData data;
	data.courses.push_back(courseOf(QStringLiteral("C"), "S1 31 32 F1"));
	QVERIFY(insertCourseMarker(data, QStringLiteral("C"), 2, CourseItem::MapExchange));
	QCOMPARE(wordsOf(data.courses[0].items), QStringLiteral("S1 31 g 32 F1"));
	QVERIFY(insertCourseMarker(data, QStringLiteral("C"), 5, CourseItem::MapFlip));
	QVERIFY(insertCourseMarker(data, QStringLiteral("C"), 0, CourseItem::CrossingPoint));
	QVERIFY(insertCourseMarker(data, QStringLiteral("C"), 2, CourseItem::Passage));
	QCOMPARE(wordsOf(data.courses[0].items), QStringLiteral("k S1 w 31 g 32 F1 x"));
	for (auto const& item : data.courses[0].items)
		QVERIFY(item.namesObject() || item.code.isEmpty());
	
	auto const before = data;
	QVERIFY(!insertCourseMarker(data, QStringLiteral("C"), 2, CourseItem::Control));
	QVERIFY(!insertCourseMarker(data, QStringLiteral("C"), 2, CourseItem::TeamForkBegin));
	QVERIFY(!insertCourseMarker(data, QStringLiteral("C"), 2, CourseItem::TextBlock));
	QVERIFY(!insertCourseMarker(data, QStringLiteral("C"), 9, CourseItem::MapExchange));
	QVERIFY(!insertCourseMarker(data, QStringLiteral("C"), -1, CourseItem::MapExchange));
	QVERIFY(!insertCourseMarker(data, QStringLiteral("Nobody"), 0, CourseItem::MapExchange));
	QVERIFY(data == before);
	
	// Moved and removed as the other items.
	QVERIFY(moveCourseItem(data, QStringLiteral("C"), 4, 5));
	QCOMPARE(wordsOf(data.courses[0].items), QStringLiteral("k S1 w 31 32 g F1 x"));
	QVERIFY(removeCourseItem(data, QStringLiteral("C"), 5));
	QVERIFY(removeCourseItem(data, QStringLiteral("C"), 0));
	QCOMPARE(wordsOf(data.courses[0].items), QStringLiteral("S1 w 31 32 F1 x"));
}


/// Accept (5)
void CoursePartsTest::checkTest()
{
	Map map;
	fillMap(map);
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("No start"), "S1 31 g 32 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Empty"), "S1 31 g g S2 32 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Last"), "S1 31 32 g"));
	data.courses.push_back(courseOf(QStringLiteral("Good"), "S1 31 g S2 32 F1"));
	
	auto const issues = checkCourses(map, data);
	QStringList messages;
	for (auto const* name : { "No start", "Empty", "Last" })
	{
		auto const found = issuesOf(issues, "course-part", QString::fromLatin1(name));
		QVERIFY2(found.size() == 1, name);
		QCOMPARE(found[0].severity, Issue::Warning);
		QVERIFY(!found[0].message.isEmpty());
		messages.push_back(found[0].message);
	}
	// The one says the start, the others the control.
	QVERIFY(messages[0] != messages[1]);
	for (auto const* name : { "Good", "X", "Twice", "Plain" })
		QVERIFY2(issuesOf(issues, "course-part", QString::fromLatin1(name)).empty(), name);
}


/// Accept (6)
void CoursePartsTest::dockTest()
{
	Map map;
	fillMap(map);
	CourseWidget widget(&map);
	auto* parts = widget.findChild<QComboBox*>(QStringLiteral("course-part"));
	auto* other = widget.findChild<QPushButton*>(QStringLiteral("course-item-insert-other"));
	auto* items = widget.findChild<QTreeWidget*>(QStringLiteral("course-items"));
	QVERIFY(parts && other && items);
	auto const actionOf = [&widget](const char* name) { return widget.findChild<QAction*>(QString::fromLatin1(name)); };
	for (auto const* name : { "course-insert-crossing", "course-insert-passage", "course-insert-exchange", "course-insert-flip",
	                          "course-team-variation", "course-leg-variation" })
		QVERIFY2(actionOf(name), name);
	QVERIFY(other->menu());
	QCOMPARE(other->menu()->actions().size(), 7);  // with the separator
	
	// Without a course nothing is put in.
	QVERIFY(!other->isEnabled());
	QVERIFY(!widget.insertMarker(CourseItem::MapExchange));
	QCOMPARE(widget.previewPart(), -1);
	
	widget.setCurrentCourse(QStringLiteral("Plain"));
	QVERIFY(other->isEnabled());
	QVERIFY(parts->isHidden());
	QCOMPARE(widget.previewPart(), -1);
	QVERIFY(actionOf("course-insert-exchange")->isEnabled());
	QVERIFY(!actionOf("course-team-variation")->isEnabled());
	QVERIFY(!actionOf("course-leg-variation")->isEnabled());
	
	// A map exchange behind 32.
	items->setCurrentItem(items->topLevelItem(2));
	auto const steps = map.undoManager().undoStepCount();
	QVERIFY(widget.insertMarker(CourseItem::MapExchange));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(wordsOf(map.courseData().findCourse(QStringLiteral("Plain"))->items), QStringLiteral("S1 31 32 g 33 F1"));
	QCOMPARE(items->indexOfTopLevelItem(items->currentItem()), 3);
	QCOMPARE(items->topLevelItem(3)->text(1), CourseWidget::tr("Map exchange"));
	QVERIFY(!parts->isHidden());
	QStringList texts;
	for (int i = 0; i < parts->count(); ++i)
		texts.push_back(parts->itemText(i));
	QCOMPARE(texts, (QStringList{ CourseWidget::tr("All parts"), CourseWidget::tr("Part %1").arg(1), CourseWidget::tr("Part %1").arg(2) }));
	QCOMPARE(parts->currentIndex(), 0);
	QCOMPARE(widget.previewPart(), -1);
	
	QSignalSpy changes(&widget, &CourseWidget::currentCourseChanged);
	parts->setCurrentIndex(2);
	QVERIFY(changes.count() >= 1);
	QCOMPARE(widget.previewPart(), 1);
	
	// Another course begins with all its parts.
	widget.setCurrentCourse(QStringLiteral("X"));
	QVERIFY(!parts->isHidden());
	QCOMPARE(parts->count(), 3);
	QCOMPARE(widget.previewPart(), -1);
	
	// One undo takes the exchange away, and the list with it.
	widget.setCurrentCourse(QStringLiteral("Plain"));
	QVERIFY(map.undoManager().undo());
	QCOMPARE(wordsOf(map.courseData().findCourse(QStringLiteral("Plain"))->items), QStringLiteral("S1 31 32 33 F1"));
	QVERIFY(parts->isHidden());
	QCOMPARE(widget.previewPart(), -1);
	
	// The other three.
	items->setCurrentItem(items->topLevelItem(0));
	QVERIFY(widget.insertMarker(CourseItem::CrossingPoint));
	QVERIFY(widget.insertMarker(CourseItem::Passage));
	QVERIFY(widget.insertMarker(CourseItem::MapFlip));
	QVERIFY(!widget.insertMarker(CourseItem::Control));
	QCOMPARE(wordsOf(map.courseData().findCourse(QStringLiteral("Plain"))->items), QStringLiteral("S1 k w x 31 32 33 F1"));
	QCOMPARE(parts->count(), 3);
	
	// The variations are for relays.
	QVERIFY(widget.setCourseType(Course::Relay, 2));
	QVERIFY(actionOf("course-team-variation")->isEnabled());
	QVERIFY(actionOf("course-leg-variation")->isEnabled());
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


QTEST_MAIN(CoursePartsTest)
#include "course_parts_t.moc"  // IWYU pragma: keep
