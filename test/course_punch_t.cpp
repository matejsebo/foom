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


#include <algorithm>
#include <initializer_list>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "core/map.h"
#include "course/course_data.h"
#include "course/course_punch.h"
#include "course/course_storage.h"
#include "gui/main_window.h"
#include "gui/map/course_punch_dialog.h"
#include "gui/map/map_editor.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

QStringList names(std::initializer_list<const char*> list)
{
	QStringList result;
	for (auto const* name : list)
		result.push_back(QString::fromLatin1(name));
	return result;
}

CourseObject courseObject(CourseObject::Kind kind, const char* code, std::initializer_list<const char*> ids = {})
{
	CourseObject object;
	object.kind = kind;
	object.code = QString::fromLatin1(code);
	object.punch_ids = names(ids);
	return object;
}

/// The data of the cases 4 to 6.
CourseData punchData()
{
	CourseData data;
	data.objects.push_back(courseObject(CourseObject::Start, "S1", { "900" }));
	data.objects.push_back(courseObject(CourseObject::Control, "31"));
	data.objects.push_back(courseObject(CourseObject::Control, "40a"));
	data.objects.push_back(courseObject(CourseObject::Control, "32", { "132", "232" }));
	data.objects.push_back(courseObject(CourseObject::Control, "33"));
	data.objects.push_back(courseObject(CourseObject::Finish, "F1"));
	return data;
}

/// The ids of a course object, joined by semicolons; "?" when it is not there.
QString idsOf(const CourseData& data, const char* code)
{
	auto const* object = data.findObject(QString::fromLatin1(code));
	return object ? object->punch_ids.join(QLatin1Char(';')) : QStringLiteral("?");
}

/// The row of the table whose first cell is the code; -1 when there is none.
int rowOf(const QTableWidget& table, const char* code)
{
	for (int row = 0; row < table.rowCount(); ++row)
	{
		if (table.item(row, 0)->text() == QLatin1String(code))
			return row;
	}
	return -1;
}

QString cellOf(const QTableWidget& table, const char* code)
{
	auto const row = rowOf(table, code);
	return row < 0 ? QStringLiteral("?") : table.item(row, 1)->text();
}

void setCell(QTableWidget& table, const char* code, const char* text)
{
	auto const row = rowOf(table, code);
	QVERIFY(row >= 0);
	table.item(row, 1)->setText(QString::fromLatin1(text));
}

}  // namespace



/**
 * @test Tests the punching unit ids: their rules and their dialog.
 *
 * See the design notes, "Punching unit ids".
 */
class CoursePunchTest : public QObject
{
Q_OBJECT
private slots:
	void parseTest();
	void joinTest();
	void offeredTest();
	void defaultsTest();
	void dialogTest();
	void refuseTest();
	void editorTest();
};


/// Accept (1)
void CoursePunchTest::parseTest()
{
	auto const expected = names({ "131", "200" });
	for (auto const* text : { "131;200", "200,131", "131 200", " 131 ; 200 ;", "200;131;200" })
	{
		QStringList ids;
		QVERIFY2(parsePunchIds(QString::fromLatin1(text), &ids), text);
		QCOMPARE(ids, expected);
	}
	
	QStringList ids;
	QVERIFY(parsePunchIds(QStringLiteral("007"), &ids));
	QCOMPARE(ids, names({ "7" }));
	QVERIFY(parsePunchIds(QString{}, &ids));
	QVERIFY(ids.isEmpty());
	QVERIFY(parsePunchIds(QStringLiteral("  "), &ids));
	QVERIFY(ids.isEmpty());
	
	for (auto const* text : { "abc", "13a", "-5", "1.5" })
		QVERIFY2(!parsePunchIds(QString::fromLatin1(text), &ids), text);
}


/// Accept (2)
void CoursePunchTest::joinTest()
{
	QCOMPARE(joinPunchIds(names({ "131", "200" })), QStringLiteral("131;200"));
	QCOMPARE(addPunchId(names({ "131", "200" }), QStringLiteral("40")), names({ "40", "131", "200" }));
	QCOMPARE(addPunchId(names({ "131", "200" }), QStringLiteral("131")), names({ "131", "200" }));
	QCOMPARE(addPunchId(names({ "A1", "131" }), QStringLiteral("50")), names({ "50", "131", "A1" }));
}


/// Accept (3)
void CoursePunchTest::offeredTest()
{
	auto const offered = offeredPunchIds();
	QCOMPARE(int(offered.size()), 237);
	QCOMPARE(offered.front(), 31);
	QCOMPARE(offered.back(), 276);
	auto const has = [&offered](int id) { return std::find(begin(offered), end(offered), id) != end(offered); };
	for (int id : { 68, 86, 89, 98, 99, 186, 189, 198, 199 })
		QVERIFY2(!has(id), qPrintable(QString::number(id)));
	for (int id : { 66, 69, 96, 168, 196 })
		QVERIFY2(has(id), qPrintable(QString::number(id)));
}


/// Accept (4)
void CoursePunchTest::defaultsTest()
{
	{
		auto data = punchData();
		QCOMPARE(addDefaultPunchIds(data, PunchDefaults::OnlyWithout, PunchSystem::SportIdent), 2);
		QCOMPARE(idsOf(data, "31"), QStringLiteral("31"));
		QCOMPARE(idsOf(data, "32"), QStringLiteral("132;232"));
		QCOMPARE(idsOf(data, "33"), QStringLiteral("33"));
		QCOMPARE(idsOf(data, "40a"), QString{});
		QCOMPARE(idsOf(data, "S1"), QStringLiteral("900"));
		QCOMPARE(idsOf(data, "F1"), QString{});
	}
	{
		auto data = punchData();
		QCOMPARE(addDefaultPunchIds(data, PunchDefaults::Replace, PunchSystem::Emit), 3);
		QCOMPARE(idsOf(data, "31"), QStringLiteral("31"));
		QCOMPARE(idsOf(data, "32"), QStringLiteral("32"));
		QCOMPARE(idsOf(data, "33"), QStringLiteral("33"));
		QCOMPARE(idsOf(data, "40a"), QString{});
		QCOMPARE(idsOf(data, "S1"), QStringLiteral("900"));
	}
	{
		auto data = punchData();
		QCOMPARE(addDefaultPunchIds(data, PunchDefaults::AddKeep, PunchSystem::Other), 3);
		QCOMPARE(idsOf(data, "31"), QStringLiteral("31"));
		QCOMPARE(idsOf(data, "32"), QStringLiteral("32;132;232"));
		QCOMPARE(idsOf(data, "33"), QStringLiteral("33"));
		QCOMPARE(idsOf(data, "40a"), QString{});
		QCOMPARE(idsOf(data, "S1"), QStringLiteral("900"));
		QCOMPARE(idsOf(data, "F1"), QString{});
	}
}


/// Accept (5)
void CoursePunchTest::dialogTest()
{
	Map map;
	changeCourseData(map, punchData());
	auto const steps = map.undoManager().undoStepCount();
	
	CoursePunchDialog dialog(&map);
	auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("course-punch-table"));
	auto* list = dialog.findChild<QListWidget*>(QStringLiteral("course-punch-list"));
	auto* system = dialog.findChild<QComboBox*>(QStringLiteral("course-punch-system"));
	auto* only = dialog.findChild<QRadioButton*>(QStringLiteral("course-punch-only"));
	auto* replace = dialog.findChild<QRadioButton*>(QStringLiteral("course-punch-replace"));
	auto* keep = dialog.findChild<QRadioButton*>(QStringLiteral("course-punch-keep"));
	auto* add = dialog.findChild<QPushButton*>(QStringLiteral("course-punch-add"));
	QVERIFY(table && list && system && only && replace && keep && add);
	
	QCOMPARE(table->rowCount(), 4);
	QCOMPARE(table->item(0, 0)->text(), QStringLiteral("31"));
	QCOMPARE(table->item(1, 0)->text(), QStringLiteral("32"));
	QCOMPARE(table->item(2, 0)->text(), QStringLiteral("33"));
	QCOMPARE(table->item(3, 0)->text(), QStringLiteral("40a"));
	QCOMPARE(cellOf(*table, "32"), QStringLiteral("132;232"));
	QCOMPARE(list->count(), 237);
	QCOMPARE(system->count(), 3);
	QCOMPARE(system->currentText(), QStringLiteral("SPORTident"));
	QVERIFY(keep->isChecked());
	QVERIFY(!only->isChecked());
	QVERIFY(!replace->isChecked());
	
	add->click();
	QCOMPARE(cellOf(*table, "31"), QStringLiteral("31"));
	QCOMPARE(cellOf(*table, "32"), QStringLiteral("32;132;232"));
	QCOMPARE(cellOf(*table, "33"), QStringLiteral("33"));
	QCOMPARE(cellOf(*table, "40a"), QString{});
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	QVERIFY(map.courseData() == punchData());
	
	// A double click on a number adds it to the current control.
	table->setCurrentCell(rowOf(*table, "31"), 1);
	auto const found = list->findItems(QStringLiteral("50"), Qt::MatchExactly);
	QCOMPARE(found.size(), 1);
	dialog.addListedId(found.front());
	QCOMPARE(cellOf(*table, "31"), QStringLiteral("31;50"));
	
	QString message;
	QVERIFY(dialog.apply(&message));
	QVERIFY(!message.isEmpty());
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(idsOf(map.courseData(), "31"), QStringLiteral("31;50"));
	QCOMPARE(idsOf(map.courseData(), "32"), QStringLiteral("32;132;232"));
	QCOMPARE(idsOf(map.courseData(), "33"), QStringLiteral("33"));
	QCOMPARE(idsOf(map.courseData(), "S1"), QStringLiteral("900"));
	QVERIFY(map.undoManager().undo());
	QVERIFY(map.courseData() == punchData());
}


/// Accept (6)
void CoursePunchTest::refuseTest()
{
	Map map;
	auto data = punchData();
	// An id which is no number, as an IOF XML file may have it, stays while its cell is not changed.
	for (auto& object : data.objects)
	{
		if (object.code == QLatin1String("40a"))
			object.punch_ids = names({ "A1" });
	}
	changeCourseData(map, data);
	auto const steps = map.undoManager().undoStepCount();
	
	CoursePunchDialog dialog(&map);
	auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("course-punch-table"));
	QVERIFY(table);
	
	// Nothing changed: no undo step.
	QString message;
	QVERIFY(dialog.apply(&message));
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	
	setCell(*table, "33", "13a");
	QVERIFY(!dialog.apply(&message));
	QVERIFY(message.contains(QStringLiteral("33")));
	QVERIFY(message.contains(QStringLiteral("13a")));
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	
	setCell(*table, "33", "200, 131");
	QVERIFY(dialog.apply(&message));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(idsOf(map.courseData(), "33"), QStringLiteral("131;200"));
	QCOMPARE(idsOf(map.courseData(), "40a"), QStringLiteral("A1"));
}


/// Accept (7)
void CoursePunchTest::editorTest()
{
	auto* map = new Map();
	changeCourseData(*map, punchData());
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	
	QVERIFY(editor->getAction("course-punch-ids"));
	
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


QTEST_MAIN(CoursePunchTest)
#include "course_punch_t.moc"  // IWYU pragma: keep
