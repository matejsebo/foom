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
#include <memory>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QPainter>
#include <QPrinter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_canvas.h"
#include "course/course_data.h"
#include "course/course_map.h"
#include "course/course_relay.h"
#include "course/course_storage.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/map/course_maps_dialog.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

using namespace OpenOrienteering;


namespace {

const char* exchange_items = "M1 S2 36 37 61 62 54 53 g S5 52 51 65 59 60 34 40 M5 g S1 33 38 58 56 57 63 M4 F3";
const char* flip_items = "M1 S2 36 37 61 62 54 53 x S5 52 51 65 59 60 34 40 35 F1";
const char* women_items = "M6 S4 r3 v 48 55 v 50 57 v 49 56 q 59 l b1-2 r2 v 64 v 65 q 62 b3 60 61 p 63 M4 F3";

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
			item.type = CourseItem::MapExchange;
		else if (word == QLatin1String("x"))
			item.type = CourseItem::MapFlip;
		else if (word == QLatin1String("v"))
			item.type = CourseItem::ForkBranch;
		else if (word == QLatin1String("q"))
			item.type = CourseItem::TeamForkEnd;
		else if (word == QLatin1String("l"))
			item.type = CourseItem::LegForkBegin;
		else if (word == QLatin1String("p"))
			item.type = CourseItem::LegForkEnd;
		else if (first == QLatin1Char('r'))
		{
			item.type = CourseItem::TeamForkBegin;
			item.count = word.mid(1).toInt();
		}
		else if (first == QLatin1Char('b'))
		{
			item.type = CourseItem::LegBranch;
			item.legs = word.mid(1);
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

CourseClass classOf(const char* name, const char* course, int runners = -1, int first_no = -1)
{
	CourseClass entry;
	entry.name = QString::fromLatin1(name);
	entry.course = QString::fromLatin1(course);
	entry.runners = runners;
	entry.first_no = first_no;
	return entry;
}

QStringList namesOf(const std::vector<CourseMapSheet>& sheets)
{
	QStringList names;
	for (auto const& sheet : sheets)
		names.push_back(sheet.name);
	return names;
}

template <class S>
S* addSymbol(Map& map, int number, int second, Symbol::CourseRole role)
{
	auto* symbol = new S();
	symbol->setNumberComponent(0, number);
	if (second >= 0)
		symbol->setNumberComponent(1, second);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

void push(Map& map, Object* object)
{
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	push(map, object);
}

void drawText(Map& map, const Symbol* symbol, double x, double y, const char* text)
{
	auto* object = new TextObject(symbol);
	object->setAnchorPosition(MapCoordF(x, y));
	object->setText(QString::fromLatin1(text));
	push(map, object);
}

/// A map with the starts S1 and S2, the controls 31 to 34, the finish F1,
/// a description, a title and a variant code, and with courses of them.
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* purple = new MapColor(QStringLiteral("Purple"), 0);
	purple->setRgb(QColor(160, 51, 240));
	map.addColor(purple, 0);
	
	auto* start = addSymbol<PointSymbol>(map, 701, -1, Symbol::CourseStart);
	start->setRotatable(true);
	start->setInnerColor(purple);
	start->setInnerRadius(1000);
	auto* control = addSymbol<PointSymbol>(map, 703, -1, Symbol::CourseControl);
	control->setInnerColor(purple);
	control->setInnerRadius(1000);
	auto* finish = addSymbol<PointSymbol>(map, 706, -1, Symbol::CourseFinish);
	finish->setInnerColor(purple);
	finish->setInnerRadius(1500);
	auto* description = addSymbol<PointSymbol>(map, 720, -1, Symbol::CourseDescription);
	auto* title = addSymbol<TextSymbol>(map, 721, -1, Symbol::CourseTitle);
	title->setFontFamily(QStringLiteral("Arial"));
	title->setFontSize(5000);
	title->setColor(purple);
	auto* variant = addSymbol<TextSymbol>(map, 722, 1, Symbol::CourseVariantCode);
	variant->setFontFamily(QStringLiteral("Arial"));
	variant->setFontSize(4000);
	variant->setColor(purple);
	auto* line = addSymbol<LineSymbol>(map, 705, -1, Symbol::NoCourseRole);
	line->setLineWidth(0.35);
	line->setColor(purple);
	auto* number = addSymbol<TextSymbol>(map, 704, -1, Symbol::NoCourseRole);
	number->setFontFamily(QStringLiteral("Arial"));
	number->setFontSize(4000);
	number->setColor(purple);
	auto* plain = addSymbol<PointSymbol>(map, 101, -1, Symbol::NoCourseRole);
	plain->setInnerColor(purple);
	plain->setInnerRadius(500);
	
	drawPoint(map, start, 0.0, 0.0);
	drawPoint(map, control, 10.0, 0.0);
	drawPoint(map, control, 20.0, 0.0);
	drawPoint(map, start, 20.0, 10.0);
	drawPoint(map, control, 30.0, 10.0);
	drawPoint(map, control, 40.0, 10.0);
	drawPoint(map, finish, 50.0, 10.0);
	drawPoint(map, description, -30.0, -30.0);
	drawText(map, title, 0.0, 30.0, "Course");
	drawText(map, variant, 0.0, 36.0, "AB");
	drawPoint(map, plain, 5.0, 5.0);
	
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("X"), "S1 31 32 g S2 33 34 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Flip"), "S1 31 32 x S2 33 34 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Plain"), "S1 31 32 33 F1"));
	data.courses.push_back(courseOf(QStringLiteral("R"), "S1 r2 v 31 v 32 q 33 F1", Course::Relay, 2));
	changeCourseData(map, data);
	
	Extent extent;
	extent.name = QStringLiteral("Sheet");
	extent.rect_mm = QRectF(-40.0, -40.0, 100.0, 80.0);
	map.setExtents({ extent });
}

std::vector<QSize> pagesOf(const QString& path)
{
	std::vector<QSize> pages;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return pages;
	auto const pdf = file.readAll();
	if (!pdf.startsWith("%PDF"))
		return pages;
	QRegularExpression box(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]"));
	auto matches = box.globalMatch(QString::fromLatin1(pdf.constData(), pdf.size()));
	while (matches.hasNext())
	{
		auto const match = matches.next();
		pages.emplace_back(qRound(match.captured(1).toDouble()), qRound(match.captured(2).toDouble()));
	}
	return pages;
}

/// The objects of a map whose symbol has the number, in all map parts.
std::vector<const Object*> objectsOf(const Map& map, int number, int second = -1)
{
	std::vector<const Object*> found;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			auto const* symbol = object->getSymbol();
			if (symbol && symbol->getNumberComponent(0) == number && (second < 0 || symbol->getNumberComponent(1) == second))
				found.push_back(object);
		}
	}
	return found;
}

QStringList textsOf(const Map& map, int number)
{
	QStringList texts;
	for (auto const* object : objectsOf(map, number))
	{
		if (object->getType() == Object::Text)
			texts.push_back(object->asText()->getText());
	}
	return texts;
}

QStringList hiddenSymbols(const Map& map)
{
	QStringList numbers;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (map.getSymbol(i)->isHidden())
			numbers.push_back(map.getSymbol(i)->getNumberAsString());
	}
	return numbers;
}

}  // namespace



/**
 * @test Tests the maps of the courses: their sheets, what a side draws,
 * and the PDF files.
 *
 * See the design notes, "The maps of the courses as PDF files".
 */
class CourseMapTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void sheetsTest();
	void textsTest();
	void sideTest();
	void printerTest();
	void exportTest();
	void runnerTest();
	void canvasExportTest();
	void dialogTest();
	void mapFilesTest();
};


void CourseMapTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseMapTest::sheetsTest()
{
	CourseData data;
	data.courses.push_back(courseOf(QStringLiteral("Easy Course"), "S1 31 32 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Map Exchange"), exchange_items));
	data.courses.push_back(courseOf(QStringLiteral("Map Flip"), flip_items));
	data.courses.push_back(courseOf(QStringLiteral("Mixed"), "S1 31 g S2 32 x S3 33 F1"));
	data.courses.push_back(courseOf(QStringLiteral("R"), "S1 r2 v 31 v 32 q 33 F1", Course::Relay, 2));
	data.courses.push_back(courseOf(QStringLiteral("O"), "S1 r2 v 31 v 32 q 33 F1", Course::OneManRelay, 2));
	data.courses.push_back(courseOf(QStringLiteral("Broken"), "S1 r2 v 31 v 32 33 F1", Course::Relay, 2));
	data.classes.push_back(classOf("Teams", "R", 3, 101));
	
	auto sheets = courseMapSheets(data, QStringLiteral("Easy Course"));
	QCOMPARE(namesOf(sheets), QStringList{ QStringLiteral("Easy Course") });
	QCOMPARE(int(sheets[0].sides.size()), 1);
	QCOMPARE(sheets[0].sides[0].course, QStringLiteral("Easy Course"));
	QCOMPARE(sheets[0].sides[0].part, -1);
	QCOMPARE(sheets[0].sides[0].variant.leg, 0);
	
	sheets = courseMapSheets(data, QStringLiteral("Map Exchange"));
	QCOMPARE(namesOf(sheets), (QStringList{ QStringLiteral("Map Exchange_1"), QStringLiteral("Map Exchange_2"), QStringLiteral("Map Exchange_3") }));
	for (std::size_t s = 0; s < sheets.size(); ++s)
	{
		QCOMPARE(int(sheets[s].sides.size()), 1);
		QCOMPARE(sheets[s].sides[0].part, int(s));
	}
	
	sheets = courseMapSheets(data, QStringLiteral("Map Flip"));
	QCOMPARE(namesOf(sheets), QStringList{ QStringLiteral("Map Flip") });
	QCOMPARE(int(sheets[0].sides.size()), 2);
	QCOMPARE(sheets[0].sides[0].part, 0);
	QCOMPARE(sheets[0].sides[1].part, 1);
	
	sheets = courseMapSheets(data, QStringLiteral("Mixed"));
	QCOMPARE(namesOf(sheets), (QStringList{ QStringLiteral("Mixed_1"), QStringLiteral("Mixed_2") }));
	QCOMPARE(int(sheets[0].sides.size()), 1);
	QCOMPARE(int(sheets[1].sides.size()), 2);
	QCOMPARE(sheets[1].sides[0].part, 1);
	QCOMPARE(sheets[1].sides[1].part, 2);
	
	sheets = courseMapSheets(data, QStringLiteral("R"));
	QCOMPARE(namesOf(sheets), (QStringList{ QStringLiteral("R.1A"), QStringLiteral("R.1B"), QStringLiteral("R.2A"), QStringLiteral("R.2B") }));
	QCOMPARE(sheets[1].sides[0].variant.leg, 1);
	QCOMPARE(sheets[1].sides[0].variant.branches, std::vector<int>{ 1 });
	QVERIFY(sheets[1].sides[0].start_number.isEmpty());
	
	sheets = courseMapSheets(data, QStringLiteral("R"), RelayMaps::Runners);
	QCOMPARE(namesOf(sheets), (QStringList{ QStringLiteral("R.101.1"), QStringLiteral("R.101.2"), QStringLiteral("R.102.1"),
	                                        QStringLiteral("R.102.2"), QStringLiteral("R.103.1"), QStringLiteral("R.103.2") }));
	QCOMPARE(sheets[2].sides[0].start_number, QStringLiteral("102.1"));
	QCOMPARE(sheets[2].sides[0].variant.leg, 1);
	QCOMPARE(sheets[3].sides[0].variant.leg, 2);
	// A team runs both branches.
	QVERIFY(sheets[2].sides[0].variant.branches != sheets[3].sides[0].variant.branches);
	
	sheets = courseMapSheets(data, QStringLiteral("O"));
	QCOMPARE(namesOf(sheets), (QStringList{ QStringLiteral("O.A"), QStringLiteral("O.B") }));
	
	QVERIFY(courseMapSheets(data, QStringLiteral("Broken")).empty());
	QVERIFY(courseMapSheets(data, QStringLiteral("Nobody")).empty());
}


/// Accept (2)
void CourseMapTest::textsTest()
{
	CourseData data;
	data.courses.push_back(courseOf(QStringLiteral("Easy Course"), "S1 31 32 F1"));
	data.courses.push_back(courseOf(QStringLiteral("Map Exchange"), exchange_items));
	data.courses.push_back(courseOf(QStringLiteral("Relay Women"), women_items, Course::Relay, 3));
	data.courses.push_back(courseOf(QStringLiteral("O"), "S1 r2 v 31 v 32 q 33 F1", Course::OneManRelay, 2));
	data.classes.push_back(classOf("M12", "Easy Course"));
	data.classes.push_back(classOf("W12", "Easy Course"));
	data.classes.push_back(classOf("Relay Women", "Relay Women"));
	data.options.setValue(QLatin1Char('t'), QStringLiteral("1"));
	
	CourseMapSide easy;
	easy.course = QStringLiteral("Easy Course");
	QCOMPARE(courseTitleText(data, easy), QStringLiteral("Easy Course\nM12\nW12"));
	QVERIFY(courseVariantText(data, easy).isEmpty());
	// A course without parts has no number, whatever the side says.
	easy.part = 0;
	QCOMPARE(courseTitleText(data, easy), QStringLiteral("Easy Course\nM12\nW12"));
	
	CourseMapSide exchange;
	exchange.course = QStringLiteral("Map Exchange");
	QCOMPARE(courseTitleText(data, exchange), QStringLiteral("Map Exchange"));
	exchange.part = 0;
	QCOMPARE(courseTitleText(data, exchange), QStringLiteral("Map Exchange (1)"));
	exchange.part = 2;
	QCOMPARE(courseTitleText(data, exchange), QStringLiteral("Map Exchange (3)"));
	
	auto const women_sheets = courseMapSheets(data, QStringLiteral("Relay Women"));
	QCOMPARE(int(women_sheets.size()), 15);
	QCOMPARE(women_sheets[0].name, QStringLiteral("Relay Women.1AA"));
	QCOMPARE(women_sheets[13].name, QStringLiteral("Relay Women.3B"));
	QCOMPARE(courseVariantText(data, women_sheets[0].sides[0]), QStringLiteral("1AA"));
	QCOMPARE(courseVariantText(data, women_sheets[13].sides[0]), QStringLiteral("3B"));
	QCOMPARE(courseTitleText(data, women_sheets[0].sides[0]), QStringLiteral("Relay Women\nRelay Women"));
	
	auto const loops = courseMapSheets(data, QStringLiteral("O"));
	QCOMPARE(courseVariantText(data, loops[1].sides[0]), QStringLiteral("B"));
	
	easy.part = -1;
	data.options.setValue(QLatin1Char('t'), QStringLiteral("2"));
	QCOMPARE(courseTitleText(data, easy), QStringLiteral("Easy Course"));
	data.options.setValue(QLatin1Char('t'), QStringLiteral("0"));
	QCOMPARE(courseTitleText(data, easy), QStringLiteral("M12\nW12"));
	// Without a class the name.
	QCOMPARE(courseTitleText(data, exchange), QStringLiteral("Map Exchange (3)"));
}


/// Accept (3)
void CourseMapTest::sideTest()
{
	Map map;
	fillMap(map);
	auto data = map.courseData();
	data.options.setValue(QLatin1Char('t'), QStringLiteral("1"));
	QVERIFY(data.findObject(QStringLiteral("N1")) && data.findObject(QStringLiteral("V1")) && data.findObject(QStringLiteral("D1")));
	
	CourseMapSide side;
	side.course = QStringLiteral("X");
	side.part = 1;
	auto drawing = drawCourseMap(map, data, side);
	QVERIFY(drawing.drawn);
	QCOMPARE(drawing.items.join(QLatin1Char(' ')), QStringLiteral("S2 33 34 F1"));
	QCOMPARE(drawing.legs, 3);
	QCOMPARE(drawing.numbers, 2);
	QVERIFY(drawing.has_description);
	QCOMPARE(drawing.texts, QStringList{ QStringLiteral("X (2)") });
	QVERIFY(drawing.objects.size() >= 4 + 3 + 2 + 1);
	// The start is turned to its first control, which lies east of it: its tip, which points up, goes to the right.
	auto found_start = false;
	for (auto const& object : drawing.objects)
	{
		if (object->getSymbol()->getNumberComponent(0) == 701)
		{
			found_start = true;
			QVERIFY(qAbs(qAbs(object->getRotation()) - M_PI / 2) < 0.01);
		}
		QVERIFY(!object->getSymbol()->isHidden());
	}
	QVERIFY(found_start);
	
	// The whole course, and a part which is not there.
	side.part = -1;
	drawing = drawCourseMap(map, data, side);
	QCOMPARE(drawing.items.join(QLatin1Char(' ')), QStringLiteral("S1 31 32 S2 33 34 F1"));
	QCOMPARE(drawing.texts, QStringList{ QStringLiteral("X") });
	side.part = 5;
	QVERIFY(!drawCourseMap(map, data, side).drawn);
	side.course = QStringLiteral("Nobody");
	QVERIFY(!drawCourseMap(map, data, side).drawn);
	
	// A variant of a relay: its own controls, and its code.
	auto const sheets = courseMapSheets(data, QStringLiteral("R"));
	QCOMPARE(int(sheets.size()), 4);
	drawing = drawCourseMap(map, data, sheets[1].sides[0]);
	QVERIFY(drawing.drawn);
	QCOMPARE(drawing.items.join(QLatin1Char(' ')), QStringLiteral("S1 32 33 F1"));
	QCOMPARE(drawing.texts, (QStringList{ QStringLiteral("R"), QStringLiteral("1B") }));
	
	// Hiding, and what was hidden before.
	map.getSymbol(1)->setHidden(true);  // the control
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("703") });
	{
		const CourseSymbolsHider hider(map);
		QCOMPARE(hider.count(), 5);
		QCOMPARE(hiddenSymbols(map), (QStringList{ QStringLiteral("701"), QStringLiteral("703"), QStringLiteral("706"),
		                                           QStringLiteral("720"), QStringLiteral("721"), QStringLiteral("722.1") }));
		// The copies stay.
		for (auto const& object : drawing.objects)
			QVERIFY(!object->getSymbol()->isHidden());
	}
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("703") });
}


/// Accept (4)
void CourseMapTest::printerTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::pdfTarget());
	printer.setMode(MapPrinterOptions::Vector);
	printer.setPrintArea(QRectF(-40.0, -40.0, 100.0, 80.0));
	printer.setCustomPageSize(printer.getPrintAreaPaperSize());
	std::vector<int> sides;
	printer.setOverlay([&sides](QPainter* painter, const RenderConfig& config, int side) {
		QVERIFY(painter && painter->isActive());
		QVERIFY(config.scaling > 0.0);
		sides.push_back(side);
	}, 2);
	QCOMPARE(printer.overlaySides(), 2);
	
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("two.pdf"));
	auto qprinter = printer.makePrinter();
	QVERIFY(bool(qprinter));
	qprinter->setOutputFormat(QPrinter::PdfFormat);
	qprinter->setOutputFileName(path);
	QVERIFY(printer.printMap(qprinter.get()));
	qprinter.reset();
	QCOMPARE(sides, (std::vector<int>{ 0, 1 }));
	QCOMPARE(int(pagesOf(path).size()), 2);
	
	// Without an overlay as ever: one page.
	sides.clear();
	printer.setOverlay({}, 1);
	auto const single = QDir(dir.path()).absoluteFilePath(QStringLiteral("one.pdf"));
	qprinter = printer.makePrinter();
	qprinter->setOutputFormat(QPrinter::PdfFormat);
	qprinter->setOutputFileName(single);
	QVERIFY(printer.printMap(qprinter.get()));
	qprinter.reset();
	QVERIFY(sides.empty());
	QCOMPARE(int(pagesOf(single).size()), 1);
}


/// Accept (5)
void CourseMapTest::exportTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	map.getSymbol(8)->setHidden(true);  // the plain symbol 101
	map.setHasUnsavedChanges(false);
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	
	CourseMapExportOptions options;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("out"));
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ QStringLiteral("X"), QStringLiteral("Flip"), QStringLiteral("Plain"), QStringLiteral("Nobody") };
	options.extent = QStringLiteral("Sheet");
	auto report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.skipped, QStringList{ QStringLiteral("Nobody") });
	QStringList names;
	for (auto const& file : report.files)
		names.push_back(QFileInfo(file).fileName());
	QCOMPARE(names, (QStringList{ QStringLiteral("m.X_1.pdf"), QStringLiteral("m.X_2.pdf"), QStringLiteral("m.Flip.pdf"), QStringLiteral("m.Plain.pdf") }));
	auto const page = QSize(283, 227);
	QCOMPARE(pagesOf(report.files[0]), std::vector<QSize>{ page });
	QCOMPARE(pagesOf(report.files[1]), std::vector<QSize>{ page });
	QCOMPARE(pagesOf(report.files[2]), (std::vector<QSize>{ page, page }));
	QCOMPARE(pagesOf(report.files[3]), std::vector<QSize>{ page });
	// The parts differ in what is drawn.
	QVERIFY(QFileInfo(report.files[0]).size() > 500);
	
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	QVERIFY(!map.hasUnsavedChanges());
	
	// All courses, the relay by its variants and by its runners.
	options.courses.clear();
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("all"));
	report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 4 + 4);
	QVERIFY(QFileInfo::exists(QDir(options.folder).absoluteFilePath(QStringLiteral("m.R.2B.pdf"))));
	
	// Without an extent the drawn map, and an extent which is not there.
	options.extent.clear();
	options.courses = QStringList{ QStringLiteral("Plain") };
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("drawn"));
	report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(int(pagesOf(report.files[0]).size()), 1);
	options.extent = QStringLiteral("None");
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	
	QCOMPARE(courseMapFileName(QStringLiteral("m"), QStringLiteral("A/B: C")), QStringLiteral("m.A_B_ C.pdf"));
}


/// Accept (6)
void CourseMapTest::runnerTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	{
		Map map;
		fillMap(map);
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	
	auto const job_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("job.json"));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("maps") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("X"), QStringLiteral("R") } },
		                             { QStringLiteral("relay"), QStringLiteral("variants") },
		                             { QStringLiteral("extent"), QStringLiteral("Sheet") } });
		// Course maps as map files, Accept (8)
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("files") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("X") } },
		                             { QStringLiteral("format"), QStringLiteral("ocd") } });
		QFile job_file(job_path);
		QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	QProcess process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process.setProcessEnvironment(env);
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	QVERIFY(process.waitForFinished(120000));
	QByteArray output = process.readAllStandardOutput();
	output += process.readAllStandardError();
	QVERIFY2(process.exitCode() == 0, output.constData());
	QVERIFY2(output.contains("\"files\":6"), output.constData());
	
	auto const maps = QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("maps")));
	for (auto const* name : { "event.X_1.pdf", "event.X_2.pdf", "event.R.1A.pdf", "event.R.1B.pdf", "event.R.2A.pdf", "event.R.2B.pdf" })
		QVERIFY2(maps.exists(QString::fromLatin1(name)), name);
	QCOMPARE(pagesOf(maps.absoluteFilePath(QStringLiteral("event.X_1.pdf"))), std::vector<QSize>{ QSize(283, 227) });
	auto const files = QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("files")));
	QVERIFY2(output.contains("\"files\":2"), output.constData());
	for (auto const* name : { "event.X.1.ocd", "event.X.2.ocd" })
		QVERIFY2(files.exists(QString::fromLatin1(name)), name);
}


/// Canvases, Accept (5) and (6)
void CourseMapTest::canvasExportTest()
{
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);

	auto data = map.courseData();
	data.canvases.active = true;
	data.canvases.used_symbols = QStringList{ QStringLiteral("101.000") };
	CourseCanvas a;
	a.name = QStringLiteral("A");
	a.extent = QStringLiteral("Sheet");
	a.scale = 5000;
	a.box_size = 5.0;
	a.hidden_symbols = QStringList{ QStringLiteral("101.000") };
	CanvasCourse entry;
	entry.course = QStringLiteral("X");
	entry.part = 0;
	a.courses.push_back(entry);
	entry.course = QStringLiteral("Flip");
	entry.flip = true;
	a.courses.push_back(entry);
	entry.part = 1;
	a.courses.push_back(entry);
	CourseCanvas b;
	b.name = QStringLiteral("B");
	b.extent = QStringLiteral("Sheet");
	entry = {};
	entry.course = QStringLiteral("Plain");
	b.courses.push_back(entry);
	data.canvases.canvases = { a, b };
	changeCourseData(map, data);
	map.setHasUnsavedChanges(false);

	CourseMapExportOptions options;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("out"));
	options.base_name = QStringLiteral("m");
	options.use_canvases = true;
	auto report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QStringList names;
	for (auto const& file : report.files)
		names.push_back(QFileInfo(file).fileName());
	QCOMPARE(names, (QStringList{ QStringLiteral("m.A.X_1.pdf"), QStringLiteral("m.A.Flip.pdf"), QStringLiteral("m.B.Plain.pdf") }));
	QCOMPARE(report.skipped, (QStringList{ QStringLiteral("X_2"), QStringLiteral("R.1A"), QStringLiteral("R.1B"), QStringLiteral("R.2A"), QStringLiteral("R.2B") }));
	auto const large = QSize(567, 454);
	QCOMPARE(pagesOf(report.files[0]), std::vector<QSize>{ large });
	QCOMPARE(pagesOf(report.files[1]), (std::vector<QSize>{ large, large }));
	QCOMPARE(pagesOf(report.files[2]), std::vector<QSize>{ QSize(283, 227) });
	QVERIFY(hiddenSymbols(map).isEmpty());
	QVERIFY(!map.hasUnsavedChanges());

	// What a canvas hides is not drawn by the sheet either: the numbers are objects of the symbol 704.
	{
		auto const count = [](const CourseMapDrawing& drawing, int number) {
			auto found = 0;
			for (auto const& object : drawing.objects)
				found += object->getSymbol()->getNumberComponent(0) == number ? 1 : 0;
			return found;
		};
		CourseMapSide side;
		side.course = QStringLiteral("Plain");
		auto const shown = drawCourseMap(map, data, side);
		auto const hidden = drawCourseMap(map, data, side, QStringList{ QStringLiteral("704.000"), QStringLiteral("706.000") });
		QCOMPARE(count(shown, 704), 3);
		QCOMPARE(count(hidden, 704), 0);
		QCOMPARE(count(shown, 706), 1);
		QCOMPARE(count(hidden, 706), 0);
		QCOMPARE(count(hidden, 705), count(shown, 705));
		QCOMPARE(count(hidden, 703), 3);
	}
	
	// A canvas whose extent the map has not.
	data.canvases.canvases[1].extent = QStringLiteral("Gone");
	changeCourseData(map, data);
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	QVERIFY2(report.error.contains(QLatin1Char('B')), qPrintable(report.error));
	data.canvases.canvases[1].extent = QStringLiteral("Sheet");
	changeCourseData(map, data);

	// The runner, from the map file.
	auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("event.foom"));
	{
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	auto const job_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("job.json"));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("maps") },
		                             { QStringLiteral("canvases"), true } });
		QFile job_file(job_path);
		QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	QProcess process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process.setProcessEnvironment(env);
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	QVERIFY(process.waitForFinished(120000));
	QByteArray output = process.readAllStandardOutput();
	output += process.readAllStandardError();
	QVERIFY2(process.exitCode() == 0, output.constData());
	QVERIFY2(output.contains("\"files\":3"), output.constData());
	auto const maps = QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("maps")));
	for (auto const* name : { "event.A.X_1.pdf", "event.A.Flip.pdf", "event.B.Plain.pdf" })
		QVERIFY2(maps.exists(QString::fromLatin1(name)), name);
	QCOMPARE(pagesOf(maps.absoluteFilePath(QStringLiteral("event.A.Flip.pdf"))), (std::vector<QSize>{ large, large }));
}


/// Accept (7)
void CourseMapTest::dialogTest()
{
	Map map;
	fillMap(map);
	CourseMapsDialog dialog(&map, QStringLiteral("C:/out"));
	auto* courses = dialog.findChild<QListWidget*>(QStringLiteral("course-maps-courses"));
	auto* relay = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-relay"));
	auto* extent = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-extent"));
	auto* folder = dialog.findChild<QLineEdit*>(QStringLiteral("course-maps-folder"));
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	QVERIFY(courses && relay && extent && folder && buttons);
	QCOMPARE(courses->count(), 4);
	QCOMPARE(extent->count(), 2);
	QCOMPARE(extent->itemText(0), CourseMapsDialog::tr("The drawn map"));
	QCOMPARE(extent->itemText(1), QStringLiteral("Sheet"));
	
	auto options = dialog.options();
	QCOMPARE(options.courses, (QStringList{ QStringLiteral("X"), QStringLiteral("Flip"), QStringLiteral("Plain"), QStringLiteral("R") }));
	QVERIFY(options.relay_maps == RelayMaps::Variants);
	QVERIFY(options.extent.isEmpty());
	QCOMPARE(options.folder, QStringLiteral("C:/out"));
	QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
	
	courses->item(1)->setCheckState(Qt::Unchecked);
	relay->setCurrentIndex(1);
	extent->setCurrentIndex(1);
	options = dialog.options();
	QCOMPARE(options.courses, (QStringList{ QStringLiteral("X"), QStringLiteral("Plain"), QStringLiteral("R") }));
	QVERIFY(options.relay_maps == RelayMaps::Runners);
	QCOMPARE(options.extent, QStringLiteral("Sheet"));
	
	folder->clear();
	QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
	folder->setText(QStringLiteral("C:/out"));
	QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
	// Course maps as map files, Accept (8)
	auto* format = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-format"));
	QVERIFY(format);
	QCOMPARE(format->count(), 3);
	QVERIFY(options.map_format.isEmpty());
	QVERIFY(extent->isEnabled());
	format->setCurrentIndex(2);
	QCOMPARE(dialog.options().map_format, QStringLiteral("OCD2018"));
	QVERIFY(!extent->isEnabled());
	format->setCurrentIndex(1);
	QCOMPARE(dialog.options().map_format, QStringLiteral("XML"));
	format->setCurrentIndex(0);
	QVERIFY(extent->isEnabled());
	
	for (int i = 0; i < courses->count(); ++i)
		courses->item(i)->setCheckState(Qt::Unchecked);
	QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
}


/// Course maps as map files, Accept (8)
void CourseMapTest::mapFilesTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	auto data = map.courseData();
	data.options.setValue(QLatin1Char('t'), QStringLiteral("2"));
	changeCourseData(map, data);
	map.setHasUnsavedChanges(false);
	auto const objects_before = map.getNumObjects();
	
	CourseMapExportOptions options;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("files"));
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ QStringLiteral("X"), QStringLiteral("Flip"), QStringLiteral("Plain"), QStringLiteral("R"), QStringLiteral("Nobody") };
	options.map_format = QStringLiteral("XML");
	auto report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.skipped, QStringList{ QStringLiteral("Nobody") });
	QStringList names;
	for (auto const& file : report.files)
		names.push_back(QFileInfo(file).fileName());
	QCOMPARE(names, (QStringList{ QStringLiteral("m.X.1.foom"), QStringLiteral("m.X.2.foom"),
	                              QStringLiteral("m.Flip.1.foom"), QStringLiteral("m.Flip.2.foom"),
	                              QStringLiteral("m.Plain.foom"),
	                              QStringLiteral("m.R.1A.foom"), QStringLiteral("m.R.1B.foom"), QStringLiteral("m.R.2A.foom"), QStringLiteral("m.R.2B.foom") }));
	
	// The second part of X: its own items, legs and numbers, and the title of the part.
	{
		Map part;
		QVERIFY(part.loadFrom(report.files[1]));
		QCOMPARE(int(objectsOf(part, 701).size()), 1);
		QCOMPARE(objectsOf(part, 701).front()->asPoint()->getCoordF(), MapCoordF(20.0, 10.0));
		QCOMPARE(int(objectsOf(part, 703).size()), 2);
		QCOMPARE(int(objectsOf(part, 706).size()), 1);
		QCOMPARE(int(objectsOf(part, 705).size()), 3);
		QCOMPARE(textsOf(part, 704), (QStringList{ QStringLiteral("3"), QStringLiteral("4") }));
		QCOMPARE(textsOf(part, 721), QStringList{ QStringLiteral("X (2)") });
		QVERIFY(objectsOf(part, 722, 1).empty());
		QVERIFY(objectsOf(part, 720).empty());
		// What is no item stays.
		QCOMPARE(int(objectsOf(part, 101).size()), 1);
		QVERIFY(part.courseData().courses.empty());
		QVERIFY(hiddenSymbols(part).isEmpty());
		QCOMPARE(part.getNumSymbols(), map.getNumSymbols());
	}
	// A variant of the relay has its code.
	{
		Map variant;
		QVERIFY(variant.loadFrom(report.files[6]));
		QCOMPARE(int(objectsOf(variant, 703).size()), 2);
		QCOMPARE(textsOf(variant, 722), QStringList{ QStringLiteral("1B") });
		QCOMPARE(textsOf(variant, 721), QStringList{ QStringLiteral("R") });
	}
	
	// The map itself is as it was.
	QVERIFY(hiddenSymbols(map).isEmpty());
	QVERIFY(!map.hasUnsavedChanges());
	QCOMPARE(map.getNumObjects(), objects_before);
	QCOMPARE(int(map.courseData().courses.size()), 4);
	
	// OCD files.
	options.courses = QStringList{ QStringLiteral("Plain") };
	options.map_format = QStringLiteral("OCD2018");
	report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 1);
	QCOMPARE(QFileInfo(report.files[0]).fileName(), QStringLiteral("m.Plain.ocd"));
	{
		Map plain;
		QVERIFY(plain.loadFrom(report.files[0]));
		QCOMPARE(int(objectsOf(plain, 703).size()), 3);
		QCOMPARE(int(objectsOf(plain, 701).size()), 1);
	}
	
	// A format which cannot be written.
	options.map_format = QStringLiteral("NoSuchFormat");
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	QCOMPARE(courseMapFileName(QStringLiteral("m"), QStringLiteral("A/B"), QStringLiteral("ocd")), QStringLiteral("m.A_B.ocd"));
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(CourseMapTest)
#include "course_map_t.moc"  // IWYU pragma: keep
