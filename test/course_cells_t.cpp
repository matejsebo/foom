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


#include <initializer_list>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QIcon>
#include <QLineEdit>
#include <QListView>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

#include "global.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_edit.h"
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

/// A map with a start, two controls, a marked route, a finish and a
/// description, drawn as the tools draw them, and seven description symbols.
struct CellMap
{
	Map map;
	PointSymbol* start;
	PointSymbol* control;
	PointSymbol* finish;
	PointSymbol* description;
	LineSymbol* route;
	PointSymbol* plain;
	
	CellMap()
	{
		map.setScaleDenominator(10000);
		start = addPoint(701, Symbol::CourseStart);
		control = addPoint(703, Symbol::CourseControl);
		finish = addPoint(706, Symbol::CourseFinish);
		description = addPoint(720, Symbol::CourseDescription);
		plain = addPoint(101, Symbol::NoCourseRole);
		route = new LineSymbol();
		route->setNumberComponent(0, 707);
		route->setLineWidth(0.35);
		route->setCourseRole(Symbol::CourseMarkedRoute);
		map.addSymbol(route, map.getNumSymbols());
		
		// Not in the order of their numbers: the lists put them in order.
		pictogram(12, 102, "West side", Symbol::DescriptionColumnG);
		pictogram(5, 1, "Road", Symbol::DescriptionColumnD | Symbol::DescriptionColumnE);
		pictogram(1, 10, "Knoll", Symbol::DescriptionColumnD | Symbol::DescriptionColumnE);
		pictogram(0, 101, "Middle", Symbol::DescriptionColumnC);
		pictogram(8, 11, "Ruined", Symbol::DescriptionColumnE);
		pictogram(10, 2, "Junction", Symbol::DescriptionColumnF);
		pictogram(13, 2, "Refreshment point", Symbol::DescriptionColumnH);
		
		draw(new PointObject(start), 0);        // S1
		draw(new PointObject(control), 10);     // 31
		draw(new PointObject(control), 20);     // 32
		auto* path = new PathObject(route);
		path->addCoordinate(MapCoord(25.0, 0.0));
		path->addCoordinate(MapCoord(30.0, 0.0));
		push(path);                             // M1
		draw(new PointObject(finish), 35);      // F1
		draw(new PointObject(description), 50); // D1
	}
	
	PointSymbol* addPoint(int number, Symbol::CourseRole role)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, number);
		symbol->setCourseRole(role);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	}
	
	PointSymbol* pictogram(int first, int second, const char* name, int columns)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, first);
		symbol->setNumberComponent(1, second);
		symbol->setName(QString::fromLatin1(name));
		symbol->setInnerRadius(1000);
		symbol->setCourseRole(Symbol::DescriptionSymbol, columns);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	}
	
	void draw(PointObject* object, double x)
	{
		object->setPosition(MapCoord(x, 0.0));
		push(object);
	}
	
	/// Adds an object as the drawing tools do.
	void push(Object* object)
	{
		auto const index = map.addObject(object);
		auto* step = new DeleteObjectsUndoStep(&map);
		step->addObject(index);
		map.push(step);
	}
	
	/// Puts changed course data into the map.
	void set(const CourseData& data)
	{
		changeCourseData(map, data);
	}
};

const CourseObject& objectOf(const CourseData& data, const char* code)
{
	auto const* object = data.findObject(QString::fromLatin1(code));
	Q_ASSERT(object);
	return *object;
}

CourseObject& objectOf(CourseData& data, const char* code)
{
	for (auto& object : data.objects)
	{
		if (object.code == QLatin1String(code))
			return object;
	}
	Q_UNREACHABLE();
}

/// The cells of a course object, as "0 1010 0 0 12102 0".
QString cellsOf(const CourseData& data, const char* code)
{
	QStringList cells;
	for (auto cell : objectOf(data, code).cell)
		cells.push_back(QString::number(cell));
	return cells.join(QLatin1Char(' '));
}

QString sizeOf(const CourseData& data, const char* code)
{
	return courseObjectField(objectOf(data, code), QLatin1Char('s'));
}

bool hasField(const CourseData& data, const char* code, char letter)
{
	for (auto const& field : objectOf(data, code).other)
	{
		if (field.letter == QLatin1Char(letter))
			return true;
	}
	return false;
}

Course courseOf(const char* name, std::initializer_list<const char*> items)
{
	Course result;
	result.name = QString::fromLatin1(name);
	for (auto const* text : items)
	{
		CourseItem item;
		switch (text[0])
		{
		case 's': item.type = CourseItem::Start; break;
		case 'c': item.type = CourseItem::Control; break;
		case 'm': item.type = CourseItem::MarkedRoute; break;
		case 'f': item.type = CourseItem::Finish; break;
		case 't': item.type = CourseItem::TextBlock; break;
		default: Q_UNREACHABLE();
		}
		item.code = QString::fromLatin1(text + 1);
		result.items.push_back(item);
	}
	return result;
}

/// The numbers of the entries of a list of cells, as "0 1010 5001".
QString entriesOf(const QComboBox* list)
{
	QStringList numbers;
	for (int i = 0; i < list->count(); ++i)
		numbers.push_back(list->itemData(i, CourseWidget::CellNumberRole).toString());
	return numbers.join(QLatin1Char(' '));
}

int currentNumber(const QComboBox* list)
{
	return list->currentData(CourseWidget::CellNumberRole).toInt();
}

/// Chooses an entry of a list as a click in its popup does.
bool choose(QComboBox* list, int number)
{
	auto const index = list->findData(number, CourseWidget::CellNumberRole);
	if (index < 0)
		return false;
	list->setCurrentIndex(index);
	emit list->activated(index);
	return true;
}

/// Enters a text into a field as typing it and leaving the field does.
void enter(QLineEdit* field, const QString& text)
{
	field->setText(text);
	emit field->editingFinished();
}

void selectRows(QTreeWidget* list, const QStringList& codes)
{
	list->clearSelection();
	for (int i = 0; i < list->topLevelItemCount(); ++i)
	{
		auto* row = list->topLevelItem(i);
		if (codes.contains(row->text(0)))
			row->setSelected(true);
	}
}

QString selectedRows(const QTreeWidget* list)
{
	QStringList codes;
	for (int i = 0; i < list->topLevelItemCount(); ++i)
	{
		if (list->topLevelItem(i)->isSelected())
			codes.push_back(list->topLevelItem(i)->text(0));
	}
	return codes.join(QLatin1Char(' '));
}

/// The parts of the editor in the dock.
struct Editor
{
	QTreeWidget* objects;
	QComboBox* cell[CourseObject::ColumnCount];
	QLineEdit* size;
	QCheckBox* funnel;
	QLineEdit* text;
	
	explicit Editor(const CourseWidget& widget)
	{
		objects = widget.findChild<QTreeWidget*>(QStringLiteral("course-objects"));
		auto column = 0;
		for (auto letter : { 'c', 'd', 'e', 'f', 'g', 'h' })
			cell[column++] = widget.findChild<QComboBox*>(QStringLiteral("course-cell-%1").arg(QLatin1Char(letter)));
		size = widget.findChild<QLineEdit*>(QStringLiteral("course-size"));
		funnel = widget.findChild<QCheckBox*>(QStringLiteral("course-funnel"));
		text = widget.findChild<QLineEdit*>(QStringLiteral("course-text"));
	}
	
	bool complete() const
	{
		for (auto const* list : cell)
		{
			if (!list)
				return false;
		}
		return objects && size && funnel && text;
	}
	
	/// Which parts are on, as "cells size funnel text".
	QString on() const
	{
		QStringList parts;
		auto lists = 0;
		for (auto const* list : cell)
			lists += list->isEnabled() ? 1 : 0;
		if (lists == CourseObject::ColumnCount)
			parts.push_back(QStringLiteral("cells"));
		else if (lists > 0)
			parts.push_back(QStringLiteral("some cells"));
		if (size->isEnabled())
			parts.push_back(QStringLiteral("size"));
		if (funnel->isEnabled())
			parts.push_back(QStringLiteral("funnel"));
		if (text->isEnabled())
			parts.push_back(QStringLiteral("text"));
		return parts.join(QLatin1Char(' '));
	}
};

}  // namespace



/**
 * @test Tests the editor of a description: cells, size, text and tapes
 *       of a course object, text blocks, and their part of the course dock.
 *       The last case writes pictures of the dock for looking at them.
 *
 * The acceptance criteria are those of the design notes,
 * "The editor of a description".
 */
class CourseCellsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void automaticTextTest();
	void cellTest();
	void sizeTest();
	void textFollowsTest();
	void textTest();
	void tapesTest();
	void textBlockTest();
	void dockTest();
};


void CourseCellsTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseCellsTest::automaticTextTest()
{
	CellMap m;
	auto data = m.map.courseData();
	QCOMPARE(int(data.objects.size()), 6);
	
	auto& object = objectOf(data, "31");
	QCOMPARE(automaticCourseObjectText(m.map, object), QString{});
	object.cell[CourseObject::ColumnD] = 1010;
	object.cell[CourseObject::ColumnG] = 12102;
	object.cell[CourseObject::ColumnH] = 13002;
	QCOMPARE(automaticCourseObjectText(m.map, object), QStringLiteral("Knoll, West side, Refreshment point"));
	setCourseObjectField(object, QLatin1Char('s'), QStringLiteral("1.5"));
	QCOMPARE(automaticCourseObjectText(m.map, object), QStringLiteral("Knoll, 1.5m, West side, Refreshment point"));
	object.cell[CourseObject::ColumnC] = 101;
	QCOMPARE(automaticCourseObjectText(m.map, object), QStringLiteral("Middle, Knoll, 1.5m, West side, Refreshment point"));
	// A symbol which the map has not is left out.
	object.cell[CourseObject::ColumnE] = 77001;
	QCOMPARE(automaticCourseObjectText(m.map, object), QStringLiteral("Middle, Knoll, 1.5m, West side, Refreshment point"));
	
	auto& second = objectOf(data, "32");
	second.cell[CourseObject::ColumnD] = 5001;
	second.cell[CourseObject::ColumnE] = 5001;
	second.cell[CourseObject::ColumnF] = 10002;
	QCOMPARE(automaticCourseObjectText(m.map, second), QStringLiteral("Road, Road, Junction"));
	
	// The text of an object: its own, or the automatic one.
	QCOMPARE(courseObjectText(m.map, second), QStringLiteral("Road, Road, Junction"));
	second.text = QStringLiteral("Road junction");
	QCOMPARE(courseObjectText(m.map, second), QStringLiteral("Road junction"));
	QCOMPARE(automaticCourseObjectText(m.map, second), QStringLiteral("Road, Road, Junction"));
	
	// Which kinds have what.
	QVERIFY(hasDescriptionCells(CourseObject::Start));
	QVERIFY(hasDescriptionCells(CourseObject::Control));
	QVERIFY(!hasDescriptionCells(CourseObject::MarkedRoute));
	QVERIFY(!hasDescriptionCells(CourseObject::Finish));
	QVERIFY(!hasDescriptionCells(CourseObject::TextBlock));
	QVERIFY(hasDescriptionText(CourseObject::Start));
	QVERIFY(hasDescriptionText(CourseObject::Control));
	QVERIFY(hasDescriptionText(CourseObject::MarkedRoute));
	QVERIFY(hasDescriptionText(CourseObject::TextBlock));
	QVERIFY(!hasDescriptionText(CourseObject::Finish));
	QVERIFY(!hasDescriptionText(CourseObject::Description));
	
	// The symbols of a column, by number.
	auto const for_e = descriptionSymbols(m.map, CourseObject::ColumnE);
	QCOMPARE(int(for_e.size()), 3);
	QCOMPARE(descriptionCellNumber(*for_e[0]), 1010);
	QCOMPARE(descriptionCellNumber(*for_e[1]), 5001);
	QCOMPARE(descriptionCellNumber(*for_e[2]), 8011);
	QCOMPARE(int(descriptionSymbols(m.map, CourseObject::ColumnC).size()), 1);
	QCOMPARE(int(descriptionSymbols(m.map, CourseObject::ColumnD).size()), 2);
	QCOMPARE(int(descriptionSymbols(m.map, CourseObject::ColumnF).size()), 1);
	QCOMPARE(int(descriptionSymbols(m.map, CourseObject::ColumnG).size()), 1);
	QCOMPARE(int(descriptionSymbols(m.map, CourseObject::ColumnH).size()), 1);
}


/// Accept (2)
void CourseCellsTest::cellTest()
{
	CellMap m;
	auto data = m.map.courseData();
	
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnD, 1010));
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnE, 1010));
	QCOMPARE(cellsOf(data, "31"), QStringLiteral("0 1010 1010 0 0 0"));
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("S1"), CourseObject::ColumnD, 5001));
	QCOMPARE(cellsOf(data, "S1"), QStringLiteral("0 5001 0 0 0 0"));
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnE, 0));
	QCOMPARE(cellsOf(data, "31"), QStringLiteral("0 1010 0 0 0 0"));
	
	// Refused, and nothing is changed.
	auto const before = data;
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnD, 12102));  // a symbol of G
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnD, 77001));  // no such symbol
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnD, 703000)); // no description symbol
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("F1"), CourseObject::ColumnD, 1010));
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("M1"), CourseObject::ColumnD, 1010));
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("99"), CourseObject::ColumnD, 1010));
	QCOMPARE(addTextBlock(data, QStringLiteral("Mind the road")), QStringLiteral("T1"));
	auto const with_block = data;
	QVERIFY(!setCourseObjectCell(m.map, data, QStringLiteral("T1"), CourseObject::ColumnD, 1010));
	QVERIFY(data == with_block);
	data = before;
	
	// A symbol in F takes the size away.
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QStringLiteral("3")));
	QCOMPARE(sizeOf(data, "31"), QStringLiteral("3"));
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnF, 10002));
	QCOMPARE(cellsOf(data, "31"), QStringLiteral("0 1010 0 10002 0 0"));
	QVERIFY(!hasField(data, "31", 's'));
}


/// Accept (3)
void CourseCellsTest::sizeTest()
{
	CellMap m;
	auto data = m.map.courseData();
	
	for (auto const* size : { "1.5", "5/10", "8x4" })
	{
		QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QString::fromLatin1(size)));
		QCOMPARE(sizeOf(data, "31"), QString::fromLatin1(size));
	}
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QStringLiteral(" 3 m")));
	QCOMPARE(sizeOf(data, "31"), QStringLiteral("3"));
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("S1"), QStringLiteral("3m")));
	QCOMPARE(sizeOf(data, "S1"), QStringLiteral("3"));
	
	auto const before = data;
	QVERIFY(!setCourseObjectSize(m.map, data, QStringLiteral("31"), QStringLiteral("a b")));
	QVERIFY(!setCourseObjectSize(m.map, data, QStringLiteral("F1"), QStringLiteral("2")));
	QVERIFY(!setCourseObjectSize(m.map, data, QStringLiteral("99"), QStringLiteral("2")));
	QVERIFY(data == before);
	
	// An empty size removes the field.
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QString{}));
	QVERIFY(!hasField(data, "31", 's'));
	
	// A size takes the symbol of F away.
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnF, 10002));
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QStringLiteral("2")));
	QCOMPARE(cellsOf(data, "31"), QStringLiteral("0 0 0 0 0 0"));
	QCOMPARE(sizeOf(data, "31"), QStringLiteral("2"));
	// An empty one does not.
	QVERIFY(setCourseObjectCell(m.map, data, QStringLiteral("31"), CourseObject::ColumnF, 10002));
	QVERIFY(setCourseObjectSize(m.map, data, QStringLiteral("31"), QString{}));
	QCOMPARE(cellsOf(data, "31"), QStringLiteral("0 0 0 10002 0 0"));
}


/// Accept (4)
void CourseCellsTest::textFollowsTest()
{
	CellMap m;
	auto const code = QStringLiteral("31");
	
	// The automatic text follows.
	auto data = m.map.courseData();
	objectOf(data, "31").cell[CourseObject::ColumnD] = 1010;
	objectOf(data, "31").text = QStringLiteral("Knoll");
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnG, 12102));
	QCOMPARE(objectOf(data, "31").text, QStringLiteral("Knoll, West side"));
	QVERIFY(setCourseObjectSize(m.map, data, code, QStringLiteral("2")));
	QCOMPARE(objectOf(data, "31").text, QStringLiteral("Knoll, 2m, West side"));
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnD, 0));
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnG, 0));
	QVERIFY(setCourseObjectSize(m.map, data, code, QString{}));
	QCOMPARE(objectOf(data, "31").text, QString{});
	
	// A text which was written by hand stays.
	data = m.map.courseData();
	objectOf(data, "31").cell[CourseObject::ColumnD] = 1010;
	objectOf(data, "31").text = QStringLiteral("Big knoll");
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnG, 12102));
	QVERIFY(setCourseObjectSize(m.map, data, code, QStringLiteral("2")));
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnD, 0));
	QCOMPARE(objectOf(data, "31").text, QStringLiteral("Big knoll"));
	
	// An object without a text stays without one.
	data = m.map.courseData();
	objectOf(data, "31").cell[CourseObject::ColumnD] = 1010;
	QVERIFY(setCourseObjectCell(m.map, data, code, CourseObject::ColumnG, 12102));
	QVERIFY(setCourseObjectSize(m.map, data, code, QStringLiteral("2")));
	QCOMPARE(objectOf(data, "31").text, QString{});
	QCOMPARE(courseObjectText(m.map, objectOf(data, "31")), QStringLiteral("Knoll, 2m, West side"));
}


/// Accept (5)
void CourseCellsTest::textTest()
{
	CellMap m;
	auto data = m.map.courseData();
	QCOMPARE(addTextBlock(data, QString{}), QStringLiteral("T1"));
	
	QVERIFY(setCourseObjectText(data, QStringLiteral("31"), QStringLiteral(" Big\tknoll ")));
	QCOMPARE(objectOf(data, "31").text, QStringLiteral("Big knoll"));
	QVERIFY(setCourseObjectText(data, QStringLiteral("31"), QStringLiteral("Big knoll,\n  north side")));
	QCOMPARE(objectOf(data, "31").text, QStringLiteral("Big knoll, north side"));
	
	for (auto const* code : { "S1", "31", "M1", "T1" })
	{
		QVERIFY2(setCourseObjectText(data, QString::fromLatin1(code), QStringLiteral("A text")), code);
		QCOMPARE(objectOf(data, code).text, QStringLiteral("A text"));
	}
	auto const before = data;
	QVERIFY(!setCourseObjectText(data, QStringLiteral("F1"), QStringLiteral("A text")));
	QVERIFY(!setCourseObjectText(data, QStringLiteral("D1"), QStringLiteral("A text")));
	QVERIFY(!setCourseObjectText(data, QStringLiteral("99"), QStringLiteral("A text")));
	QVERIFY(data == before);
	
	QVERIFY(setCourseObjectText(data, QStringLiteral("31"), QString{}));
	QCOMPARE(objectOf(data, "31").text, QString{});
}


/// Accept (6)
void CourseCellsTest::tapesTest()
{
	CellMap m;
	auto data = m.map.courseData();
	data.courses.push_back(courseOf("A", { "sS1", "c31", "c32", "mM1", "fF1" }));
	
	QVERIFY(!hasField(data, "M1", 'm'));
	auto description = describeCourse(m.map, data, QStringLiteral("A"));
	QVERIFY(description.known);
	QCOMPARE(description.rows.back().kind, CourseDescriptionRow::Finish);
	QCOMPARE(description.rows.back().tapes, CourseDescriptionRow::Taped);
	
	QVERIFY(setMarkedRouteFunnel(data, QStringLiteral("M1"), true));
	QCOMPARE(courseObjectField(objectOf(data, "M1"), QLatin1Char('m')), QStringLiteral("f"));
	description = describeCourse(m.map, data, QStringLiteral("A"));
	QCOMPARE(description.rows.back().tapes, CourseDescriptionRow::Funnel);
	
	QVERIFY(setMarkedRouteFunnel(data, QStringLiteral("M1"), false));
	QVERIFY(!hasField(data, "M1", 'm'));
	description = describeCourse(m.map, data, QStringLiteral("A"));
	QCOMPARE(description.rows.back().tapes, CourseDescriptionRow::Taped);
	
	auto const before = data;
	QVERIFY(!setMarkedRouteFunnel(data, QStringLiteral("31"), true));
	QVERIFY(!setMarkedRouteFunnel(data, QStringLiteral("99"), true));
	QVERIFY(data == before);
}


/// Accept (7)
void CourseCellsTest::textBlockTest()
{
	CellMap m;
	auto data = m.map.courseData();
	data.courses.push_back(courseOf("A", { "sS1", "c31", "c32", "fF1" }));
	
	QCOMPARE(addTextBlock(data, QStringLiteral(" Mind\tthe road ")), QStringLiteral("T1"));
	QCOMPARE(addTextBlock(data, QStringLiteral("Second\nblock")), QStringLiteral("T2"));
	QCOMPARE(objectOf(data, "T1").kind, CourseObject::TextBlock);
	QCOMPARE(objectOf(data, "T1").text, QStringLiteral("Mind the road"));
	QVERIFY(objectOf(data, "T1").object.isNull());
	QCOMPARE(objectOf(data, "T2").text, QStringLiteral("Second block"));
	
	QVERIFY(insertCourseItem(data, QStringLiteral("A"), 2, QStringLiteral("T1")));
	QCOMPARE(data.courses[0].items[2].type, CourseItem::TextBlock);
	auto const description = describeCourse(m.map, data, QStringLiteral("A"));
	QVERIFY(description.known);
	QCOMPARE(int(description.rows.size()), 5);
	QCOMPARE(description.rows[2].kind, CourseDescriptionRow::TextBlock);
	QCOMPARE(description.rows[2].text, QStringLiteral("Mind the road"));
	
	// The text blocks stay when the course data go into the map.
	m.set(data);
	QCOMPARE(objectOf(m.map.courseData(), "T2").text, QStringLiteral("Second block"));
}


/// Accept (8)
void CourseCellsTest::dockTest()
{
	CellMap m;
	auto& map = m.map;
	CourseWidget widget(&map);
	Editor editor(widget);
	QVERIFY(editor.complete());
	auto const d = CourseObject::ColumnD;
	auto const g = CourseObject::ColumnG;
	
	// Nothing is selected.
	QCOMPARE(widget.descriptionObject(), QString{});
	QCOMPARE(editor.on(), QString{});
	
	// A control.
	selectRows(editor.objects, { QStringLiteral("31") });
	QCOMPARE(widget.descriptionObject(), QStringLiteral("31"));
	QCOMPARE(editor.on(), QStringLiteral("cells size text"));
	QCOMPARE(entriesOf(editor.cell[CourseObject::ColumnC]), QStringLiteral("0 101"));
	QCOMPARE(entriesOf(editor.cell[d]), QStringLiteral("0 1010 5001"));
	QCOMPARE(entriesOf(editor.cell[CourseObject::ColumnE]), QStringLiteral("0 1010 5001 8011"));
	QCOMPARE(entriesOf(editor.cell[CourseObject::ColumnF]), QStringLiteral("0 10002"));
	QCOMPARE(entriesOf(editor.cell[g]), QStringLiteral("0 12102"));
	QCOMPARE(entriesOf(editor.cell[CourseObject::ColumnH]), QStringLiteral("0 13002"));
	QCOMPARE(editor.cell[d]->itemText(1), QStringLiteral("1.010 Knoll"));
	QCOMPARE(editor.cell[d]->itemText(2), QStringLiteral("5.001 Road"));
	QVERIFY(!editor.cell[d]->itemIcon(1).isNull());
	QVERIFY(editor.cell[d]->itemIcon(0).isNull());
	QCOMPARE(currentNumber(editor.cell[d]), 0);
	
	// A cell is chosen: one undo step.
	auto const before = map.courseData();
	QVERIFY(choose(editor.cell[d], 1010));
	QCOMPARE(cellsOf(map.courseData(), "31"), QStringLiteral("0 1010 0 0 0 0"));
	QCOMPARE(widget.descriptionObject(), QStringLiteral("31"));
	QCOMPARE(currentNumber(editor.cell[d]), 1010);
	QCOMPARE(editor.text->text(), QString{});
	QCOMPARE(editor.text->placeholderText(), QStringLiteral("Knoll"));
	QVERIFY(map.undoManager().canUndo());
	map.undoManager().undo();
	QVERIFY(map.courseData() == before);
	// The row stays selected, though the step has no map object to select.
	QCOMPARE(selectedRows(editor.objects), QStringLiteral("31"));
	QCOMPARE(widget.descriptionObject(), QStringLiteral("31"));
	QCOMPARE(m.map.getNumSelectedObjects(), 1);
	QCOMPARE(currentNumber(editor.cell[d]), 0);
	QCOMPARE(editor.text->placeholderText(), QString{});
	map.undoManager().redo();
	QCOMPARE(currentNumber(editor.cell[d]), 1010);
	
	// The size.
	enter(editor.size, QStringLiteral("1.5"));
	QCOMPARE(sizeOf(map.courseData(), "31"), QStringLiteral("1.5"));
	QCOMPARE(editor.size->text(), QStringLiteral("1.5"));
	QCOMPARE(editor.text->placeholderText(), QStringLiteral("Knoll, 1.5m"));
	// A size which is not taken leaves the field as it was.
	enter(editor.size, QStringLiteral("a b"));
	QCOMPARE(sizeOf(map.courseData(), "31"), QStringLiteral("1.5"));
	QCOMPARE(editor.size->text(), QStringLiteral("1.5"));
	
	// A text by hand stays.
	enter(editor.text, QStringLiteral("Big knoll"));
	QCOMPARE(objectOf(map.courseData(), "31").text, QStringLiteral("Big knoll"));
	QVERIFY(widget.setCell(g, 12102));
	QCOMPARE(cellsOf(map.courseData(), "31"), QStringLiteral("0 1010 0 0 12102 0"));
	QCOMPARE(objectOf(map.courseData(), "31").text, QStringLiteral("Big knoll"));
	QCOMPARE(editor.text->text(), QStringLiteral("Big knoll"));
	QCOMPARE(editor.text->placeholderText(), QStringLiteral("Knoll, 1.5m, West side"));
	// What is refused changes nothing.
	QVERIFY(!widget.setCell(d, 12102));
	QCOMPARE(cellsOf(map.courseData(), "31"), QStringLiteral("0 1010 0 0 12102 0"));
	
	// Two objects.
	selectRows(editor.objects, { QStringLiteral("31"), QStringLiteral("32") });
	QCOMPARE(widget.descriptionObject(), QString{});
	QCOMPARE(editor.on(), QString{});
	QCOMPARE(editor.text->text(), QString{});
	QCOMPARE(currentNumber(editor.cell[d]), 0);
	QVERIFY(!widget.setCell(d, 5001));
	
	// The other control has what it has.
	selectRows(editor.objects, { QStringLiteral("32") });
	QCOMPARE(currentNumber(editor.cell[d]), 0);
	QCOMPARE(editor.size->text(), QString{});
	QCOMPARE(editor.text->text(), QString{});
	QCOMPARE(editor.text->placeholderText(), QString{});
	selectRows(editor.objects, { QStringLiteral("31") });
	QCOMPARE(currentNumber(editor.cell[d]), 1010);
	QCOMPARE(currentNumber(editor.cell[g]), 12102);
	QCOMPARE(editor.size->text(), QStringLiteral("1.5"));
	
	// The start.
	selectRows(editor.objects, { QStringLiteral("S1") });
	QCOMPARE(editor.on(), QStringLiteral("cells size text"));
	
	// The marked route.
	selectRows(editor.objects, { QStringLiteral("M1") });
	QCOMPARE(widget.descriptionObject(), QStringLiteral("M1"));
	QCOMPARE(editor.on(), QStringLiteral("funnel text"));
	QVERIFY(!editor.funnel->isChecked());
	editor.funnel->click();
	QCOMPARE(courseObjectField(objectOf(map.courseData(), "M1"), QLatin1Char('m')), QStringLiteral("f"));
	QVERIFY(editor.funnel->isChecked());
	map.undoManager().undo();
	QVERIFY(!hasField(map.courseData(), "M1", 'm'));
	QVERIFY(!editor.funnel->isChecked());
	enter(editor.text, QStringLiteral("; to the finish"));
	QCOMPARE(objectOf(map.courseData(), "M1").text, QStringLiteral("; to the finish"));
	
	// The finish and the description.
	selectRows(editor.objects, { QStringLiteral("F1") });
	QCOMPARE(editor.on(), QString{});
	QVERIFY(!editor.funnel->isChecked());
	selectRows(editor.objects, { QStringLiteral("D1") });
	QCOMPARE(editor.on(), QString{});
	
	// A cell whose symbol the map has not.
	auto data = map.courseData();
	objectOf(data, "32").cell[d] = 77001;
	m.set(data);
	selectRows(editor.objects, { QStringLiteral("32") });
	QCOMPARE(entriesOf(editor.cell[d]), QStringLiteral("0 1010 5001 77001"));
	QCOMPARE(currentNumber(editor.cell[d]), 77001);
	QCOMPARE(editor.cell[d]->currentText(), QStringLiteral("77.001"));
	QVERIFY(choose(editor.cell[d], 5001));
	QCOMPARE(cellsOf(map.courseData(), "32"), QStringLiteral("0 5001 0 0 0 0"));
	QCOMPARE(entriesOf(editor.cell[d]), QStringLiteral("0 1010 5001"));
	selectRows(editor.objects, { QStringLiteral("31") });
	QCOMPARE(entriesOf(editor.cell[d]), QStringLiteral("0 1010 5001"));
	
	// A text block.
	QCOMPARE(widget.addTextBlock(QStringLiteral("Mind the road")), QStringLiteral("T1"));
	QCOMPARE(objectOf(map.courseData(), "T1").text, QStringLiteral("Mind the road"));
	QCOMPARE(selectedRows(editor.objects), QStringLiteral("T1"));
	QCOMPARE(widget.descriptionObject(), QStringLiteral("T1"));
	QCOMPARE(editor.on(), QStringLiteral("text"));
	QCOMPARE(editor.text->text(), QStringLiteral("Mind the road"));
	enter(editor.text, QStringLiteral("Mind the railway"));
	QCOMPARE(objectOf(map.courseData(), "T1").text, QStringLiteral("Mind the railway"));
	map.undoManager().undo();
	QCOMPARE(objectOf(map.courseData(), "T1").text, QStringLiteral("Mind the road"));
	map.undoManager().undo();
	QVERIFY(!map.courseData().findObject(QStringLiteral("T1")));
	
	// A symbol which is added is in the lists.
	m.pictogram(12, 101, "North side", Symbol::DescriptionColumnG);
	selectRows(editor.objects, { QStringLiteral("31") });
	QCOMPARE(entriesOf(editor.cell[g]), QStringLiteral("0 12101 12102"));
	QCOMPARE(currentNumber(editor.cell[g]), 12102);
}




QTEST_MAIN(CourseCellsTest)
#include "course_cells_t.moc"  // IWYU pragma: keep
