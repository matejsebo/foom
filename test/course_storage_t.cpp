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


#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QIODevice>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QXmlStreamReader>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/ocd_course_records.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "undo/map_data_undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// Course data which use every member of the model.
CourseData sampleData(const QUuid& control_object, const QUuid& number_object)
{
	CourseData data;
	data.options.fields = { { QLatin1Char('t'), QStringLiteral("1") },
	                        { QLatin1Char('s'), QStringLiteral("6.00") },
	                        { QLatin1Char('e'), QStringLiteral("Autumn <Cup> & \"Final\"") },
	                        { QLatin1Char('B'), QStringLiteral("0") } };
	
	CourseObject start;
	start.kind = CourseObject::Start;
	start.code = QStringLiteral("S1");
	data.objects.push_back(start);
	
	CourseObject control;
	control.kind = CourseObject::Control;
	control.code = QStringLiteral("31");
	control.object = control_object;
	control.cell[CourseObject::ColumnD] = 1010;
	control.cell[CourseObject::ColumnG] = 12102;
	control.cell[CourseObject::ColumnH] = 13002;
	control.text = QStringLiteral("Knoll, West Side");
	control.punch_ids = QStringList{ QStringLiteral("131"), QStringLiteral("931") };
	control.locked = true;
	control.other.push_back({ QLatin1Char('z'), QStringLiteral("9") });
	data.objects.push_back(control);
	
	CourseObject block;
	block.kind = CourseObject::TextBlock;
	block.code = QStringLiteral("T1");
	block.text = QStringLiteral("Attention:\n\tDangerous Road!  ");
	data.objects.push_back(block);
	
	CourseObject description;
	description.kind = CourseObject::Description;
	description.code = QStringLiteral("D1");
	description.other.push_back({ QLatin1Char('p'), QStringLiteral("0") });
	description.other.push_back({ QLatin1Char('o'), QStringLiteral("t") });
	data.objects.push_back(description);
	
	Course relay;
	OcdCourse::parseCourse(QStringLiteral("Relay\tYs\tL3\tsS1\tr3\tv\tc31\tv\tc32\tv\tc33\tq\tl\tb1-2\tc34\tb3\tc35\tp\tw\tk\tg\tx\ttT1\tmM1\tfF1\tC50\tE-0.14\tZ7"), relay);
	data.courses.push_back(relay);
	Course plain;
	plain.name = QStringLiteral("Easy");
	OcdCourse::parseCourse(QStringLiteral("Easy\tsS1\tc31\tfF1"), plain);
	data.courses.push_back(plain);
	Course one_man;
	OcdCourse::parseCourse(QStringLiteral("Loops\tYo\tL2\tsS1\tc31\tfF1\tE0.00"), one_man);
	data.courses.push_back(one_man);
	
	CourseClass open;
	open.name = QStringLiteral("Open");
	open.course = QStringLiteral("Easy");
	data.classes.push_back(open);
	CourseClass teams;
	teams.name = QStringLiteral("Teams");
	teams.course = QStringLiteral("Relay");
	teams.runners = 50;
	teams.first_no = 101;
	teams.last_no = 150;
	teams.comment = QStringLiteral("Start at 10:00\nin waves");
	teams.other.push_back({ QLatin1Char('d'), QString{} });
	data.classes.push_back(teams);
	
	CourseOverride number;
	number.course = QStringLiteral("Easy");
	number.what = CourseOverride::Number;
	number.code_from = QStringLiteral("31");
	number.object = number_object;
	data.overrides.push_back(number);
	CourseOverride line;
	line.course = QStringLiteral("Easy");
	line.what = CourseOverride::Line;
	line.code_from = QStringLiteral("S1");
	line.code_to = QStringLiteral("31");
	data.overrides.push_back(line);
	
	return data;
}

void setupMap(Map& map, QUuid* first, QUuid* second)
{
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* symbol = new PointSymbol();
	symbol->setInnerRadius(300);
	symbol->setInnerColor(black);
	map.addSymbol(symbol, 0);
	auto* one = new PointObject(symbol);
	one->setPosition(MapCoord(10.0, 10.0));
	map.addObject(one);
	auto* two = new PointObject(symbol);
	two->setPosition(MapCoord(20.0, 10.0));
	map.addObject(two);
	*first = one->uuid();
	*second = two->uuid();
}

bool roundTrip(const Map& map, Map& reloaded, QByteArray* bytes = nullptr, std::vector<QString>* import_warnings = nullptr)
{
	auto const* format = FileFormats.findFormat("XML");
	if (!format)
		return false;
	QBuffer buffer;
	if (!buffer.open(QIODevice::ReadWrite))
		return false;
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return false;
	if (bytes)
		*bytes = buffer.data();
	buffer.seek(0);
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	if (!importer->doImport())
		return false;
	if (import_warnings)
		*import_warnings = importer->warnings();
	return true;
}

}  // namespace



/**
 * @test Tests the storage of the course data: the file element, the map,
 *       the undo step and the OCD import.
 *
 * the design notes, "Storage".
 */
class CourseStorageTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void elementTest();
	void tolerantLoadTest();
	void mapFileTest();
	void undoTest();
};


void CourseStorageTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseStorageTest::elementTest()
{
	auto const data = sampleData(QUuid::createUuid(), QUuid::createUuid());
	QCOMPARE(int(data.courses[0].items.size()), 22);
	auto const xml = courseDataToXml(data);
	QVERIFY(xml.startsWith("<course_data>"));
	auto const again = courseDataFromXml(xml);
	QVERIFY(again.options == data.options);
	QCOMPARE(int(again.objects.size()), int(data.objects.size()));
	for (std::size_t i = 0; i < data.objects.size(); ++i)
		QVERIFY2(again.objects[i] == data.objects[i], qPrintable(data.objects[i].code));
	QCOMPARE(int(again.courses.size()), int(data.courses.size()));
	for (std::size_t i = 0; i < data.courses.size(); ++i)
		QVERIFY2(again.courses[i] == data.courses[i], qPrintable(data.courses[i].name));
	QVERIFY(again.classes == data.classes);
	QVERIFY(again.overrides == data.overrides);
	QVERIFY(again == data);
	// The text of a block keeps its line break, its tab and its blanks.
	QCOMPARE(again.objects[2].text, QStringLiteral("Attention:\n\tDangerous Road!  "));
	QCOMPARE(again.classes[1].comment, QStringLiteral("Start at 10:00\nin waves"));
	
	// What has its default is not written.
	QVERIFY(!xml.contains("legs=\"0\""));
	QVERIFY(!xml.contains("locked=\"false\""));
	QVERIFY(!xml.contains("type=\"normal\""));
	QVERIFY(!xml.contains("runners=\"-1\""));
	QVERIFY(xml.contains("extra=\"0\""));
	
	// Nothing at all.
	auto const empty = courseDataToXml(CourseData{});
	QVERIFY(courseDataFromXml(empty).isEmpty());
	QVERIFY(courseDataFromXml(QByteArray("<other/>")).isEmpty());
	QVERIFY(courseDataFromXml(QByteArray()).isEmpty());
}


/// Accept (2)
void CourseStorageTest::tolerantLoadTest()
{
	auto const text = QByteArray(
	    "<course_data future=\"1\">"
	    "<canvases><canvas name=\"A\"/></canvases>"
	    "<course_object kind=\"control\" code=\"31\" colour=\"red\"><shape sides=\"3\"/><text>Knoll</text></course_object>"
	    "<course_object kind=\"teleporter\" code=\"X1\"/>"
	    "<course name=\"Easy\"><item type=\"start\" code=\"S1\"/><item type=\"jump\"/><item type=\"control\" code=\"31\"/></course>"
	    "<class name=\"Open\" course=\"Easy\"/>"
	    "</course_data>");
	QXmlStreamReader xml(text);
	QVERIFY(xml.readNextStartElement());
	QStringList problems;
	auto const data = loadCourseData(xml, &problems);
	QVERIFY(!xml.hasError());
	QCOMPARE(int(data.objects.size()), 1);
	QCOMPARE(data.objects[0].code, QStringLiteral("31"));
	QCOMPARE(data.objects[0].text, QStringLiteral("Knoll"));
	QCOMPARE(int(data.courses.size()), 1);
	QCOMPARE(int(data.courses[0].items.size()), 2);
	QCOMPARE(data.courses[0].items[1].code, QStringLiteral("31"));
	QCOMPARE(int(data.classes.size()), 1);
	QCOMPARE(data.classes[0].runners, -1);
	QCOMPARE(problems.size(), 2);
	QVERIFY2(problems[0].contains(QLatin1String("X1")), qPrintable(problems[0]));
	QVERIFY2(problems[1].contains(QLatin1String("Easy")), qPrintable(problems[1]));
}


/// Accept (3)
void CourseStorageTest::mapFileTest()
{
	Map map;
	QUuid first;
	QUuid second;
	setupMap(map, &first, &second);
	QVERIFY(map.courseData().isEmpty());
	
	// A map without course data has no element for it.
	{
		Map reloaded;
		QByteArray bytes;
		QVERIFY(roundTrip(map, reloaded, &bytes));
		QVERIFY(!bytes.contains("course_data"));
		QVERIFY(reloaded.courseData().isEmpty());
	}
	
	QSignalSpy changes(&map, &Map::courseDataChanged);
	auto const data = sampleData(first, second);
	map.setCourseData(data);
	QCOMPARE(changes.count(), 1);
	QVERIFY(map.courseData() == data);
	
	Map reloaded;
	QByteArray bytes;
	std::vector<QString> warnings;
	QVERIFY(roundTrip(map, reloaded, &bytes, &warnings));
	QVERIFY(bytes.contains("<course_data>"));
	for (auto const& warning : warnings)
		QVERIFY2(!warning.contains(QLatin1String("not known")), qPrintable(warning));
	QVERIFY(reloaded.courseData() == data);
	// The course object still names its map object.
	auto const* control = reloaded.courseData().findObject(QStringLiteral("31"));
	QVERIFY(control);
	QCOMPARE(control->object, first);
	auto found = 0;
	for (int i = 0; i < reloaded.getPart(0)->getNumObjects(); ++i)
		found += reloaded.getPart(0)->getObject(i)->uuid() == first;
	QCOMPARE(found, 1);
}


/// Accept (4)
void CourseStorageTest::undoTest()
{
	Map map;
	QUuid first;
	QUuid second;
	setupMap(map, &first, &second);
	map.setHasUnsavedChanges(false);
	QVERIFY(!map.undoManager().canUndo());
	QSignalSpy changes(&map, &Map::courseDataChanged);
	
	auto const data = sampleData(first, second);
	changeCourseData(map, data);
	QVERIFY(map.courseData() == data);
	QCOMPARE(changes.count(), 1);
	QVERIFY(map.undoManager().canUndo());
	QVERIFY(map.hasUnsavedChanges());
	
	// The same data again: no step.
	auto const steps = map.undoManager().undoStepCount();
	changeCourseData(map, data);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	QCOMPARE(changes.count(), 1);
	
	auto changed = data;
	changed.courses.pop_back();
	changed.classes[0].runners = 12;
	changeCourseData(map, changed);
	QVERIFY(map.courseData() == changed);
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	
	map.undoManager().undo();
	QVERIFY(map.courseData() == data);
	map.undoManager().undo();
	QVERIFY(map.courseData().isEmpty());
	QVERIFY(!map.undoManager().canUndo());
	map.undoManager().redo();
	QVERIFY(map.courseData() == data);
	map.undoManager().redo();
	QVERIFY(map.courseData() == changed);
	QVERIFY(!map.undoManager().canRedo());
	QCOMPARE(changes.count(), 6);
	
	// The step is kept in the file with the undo history.
	MapDataUndoStep step(&map, MapDataUndoStep::Courses, MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses));
	QVERIFY(step.isValid());
	QVERIFY(courseDataFromXml(step.xml()) == changed);
}



QTEST_MAIN(CourseStorageTest)
#include "course_storage_t.moc"  // IWYU pragma: keep
