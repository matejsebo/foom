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
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPushButton>
#include <QPainter>
#include <QPrinter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QRegularExpression>
#include <QSize>
#include <QSpinBox>
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
#include "core/map_view.h"
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
#include "course/course_preview.h"
#include "course/course_relay.h"
#include "course/course_storage.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/map/course_canvases_dialog.h"
#include "gui/map/course_maps_dialog.h"
#include "templates/template.h"
#include "templates/template_image.h"
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
	purple->setCmykFromRgb();  // what is printed
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
	void printTest();
	void oneFileTest();
	void printOptionsTest();
	void previewTest();
	void canvasBackgroundTest();
	void canvasFrameTest();
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
		// Several courses in one file, Accept (13)
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("one") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("X"), QStringLiteral("R") } },
		                             { QStringLiteral("extent"), QStringLiteral("Sheet") },
		                             { QStringLiteral("placing"), QStringLiteral("pages") } });
		// Course only, intensity, line width and the control descriptions, Accept (19)
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("course-only") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("Plain") } },
		                             { QStringLiteral("extent"), QStringLiteral("Sheet") },
		                             { QStringLiteral("content"), QStringLiteral("course-only") },
		                             { QStringLiteral("intensity"), 60 },
		                             { QStringLiteral("line_width"), 150 },
		                             { QStringLiteral("offset_x"), 2 } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("descriptions") },
		                             { QStringLiteral("courses"), QJsonArray{ QStringLiteral("Plain") } },
		                             { QStringLiteral("content"), QStringLiteral("descriptions") } });
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
	// Several courses in one file, Accept (13)
	QVERIFY2(output.contains("\"files\":1"), output.constData());
	auto const one = QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("one")));
	QCOMPARE(int(pagesOf(one.absoluteFilePath(QStringLiteral("event.Courses.pdf"))).size()), 6);
	// Course only, intensity, line width and the control descriptions, Accept (19)
	QVERIFY(QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("course-only"))).exists(QStringLiteral("event.Plain.pdf")));
	QVERIFY(QDir(QDir(dir.path()).absoluteFilePath(QStringLiteral("descriptions"))).exists(QStringLiteral("event.Plain.Descriptions.pdf")));
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("bad") },
		                             { QStringLiteral("placing"), QStringLiteral("sideways") } });
		QFile job_file(job_path);
		QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	QVERIFY(process.waitForFinished(120000));
	output = process.readAllStandardOutput() + process.readAllStandardError();
	QVERIFY2(process.exitCode() != 0, output.constData());
	QVERIFY2(output.contains("placing"), output.constData());
	{
		QJsonArray steps;
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		steps.push_back(QJsonObject{ { QStringLiteral("step"), QStringLiteral("export-course-maps") },
		                             { QStringLiteral("folder"), QStringLiteral("bad") },
		                             { QStringLiteral("intensity"), 0 } });
		QFile job_file(job_path);
		QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
	}
	process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
	QVERIFY(process.waitForFinished(120000));
	output = process.readAllStandardOutput() + process.readAllStandardError();
	QVERIFY2(process.exitCode() != 0, output.constData());
	QVERIFY2(output.contains("intensity"), output.constData());
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
	
	// Several courses in one file, Accept (14)
	auto* placing = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-placing"));
	auto* canvases = dialog.findChild<QCheckBox*>(QStringLiteral("course-maps-canvases"));
	QVERIFY(placing && canvases);
	QCOMPARE(placing->count(), 3);
	QCOMPARE(placing->currentIndex(), 0);
	QVERIFY(dialog.options().placing == CourseMapPlacing::Separate);
	placing->setCurrentIndex(1);
	QVERIFY(dialog.options().placing == CourseMapPlacing::Pages);
	QVERIFY(!canvases->isEnabled());
	placing->setCurrentIndex(0);
	format->setCurrentIndex(2);
	QCOMPARE(placing->count(), 4);
	QCOMPARE(placing->currentIndex(), 0);
	placing->setCurrentIndex(2);
	QVERIFY(dialog.options().placing == CourseMapPlacing::Below);
	format->setCurrentIndex(0);
	QCOMPARE(placing->count(), 3);
	QCOMPARE(placing->currentIndex(), 0);
	QVERIFY(dialog.options().placing == CourseMapPlacing::Separate);
	
	// Course only, intensity, line width and the control descriptions, Accept (20)
	auto* content = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-content"));
	auto* intensity = dialog.findChild<QSpinBox*>(QStringLiteral("course-maps-intensity"));
	auto* line_width = dialog.findChild<QSpinBox*>(QStringLiteral("course-maps-line-width"));
	auto* stretch_x = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("course-maps-stretch-x"));
	auto* offset_y = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("course-maps-offset-y"));
	auto* mirror = dialog.findChild<QCheckBox*>(QStringLiteral("course-maps-mirror"));
	QVERIFY(content && intensity && line_width && stretch_x && offset_y && mirror);
	QCOMPARE(content->count(), 3);
	QVERIFY(dialog.options().content == CourseMapContent::Map);
	QVERIFY(!stretch_x->isEnabled() && !offset_y->isEnabled() && !mirror->isEnabled());
	content->setCurrentIndex(1);
	QVERIFY(dialog.options().content == CourseMapContent::CourseOnly);
	QVERIFY(stretch_x->isEnabled() && offset_y->isEnabled() && mirror->isEnabled());
	offset_y->setValue(3.0);
	mirror->setChecked(true);
	intensity->setValue(60);
	line_width->setValue(150);
	options = dialog.options();
	QCOMPARE(options.intensity, 60);
	QCOMPARE(options.line_width, 150);
	QCOMPARE(options.offset_y, 3.0);
	QVERIFY(options.mirror);
	content->setCurrentIndex(2);
	QVERIFY(dialog.options().content == CourseMapContent::Descriptions);
	QVERIFY(!stretch_x->isEnabled() && !mirror->isEnabled());
	content->setCurrentIndex(1);
	format->setCurrentIndex(2);
	QVERIFY(!content->isEnabled() && !intensity->isEnabled() && !line_width->isEnabled());
	options = dialog.options();
	QVERIFY(options.content == CourseMapContent::Map);
	QCOMPARE(options.intensity, 100);
	QCOMPARE(options.line_width, 100);
	format->setCurrentIndex(0);
	
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


/// Printing the course maps, Accept (9)
void CourseMapTest::printTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	map.getSymbol(8)->setHidden(true);  // the plain symbol 101
	map.setHasUnsavedChanges(false);
	
	// A printer which writes a PDF file stands for the printer of the print dialog.
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("printed.pdf"));
	QPrinter printer(QPrinter::HighResolution);
	printer.setOutputFormat(QPrinter::PdfFormat);
	printer.setOutputFileName(path);
	printer.setPageSize(QPageSize(QPageSize::A4));
	printer.setPageOrientation(QPageLayout::Portrait);
	
	CourseMapExportOptions options;
	options.courses = QStringList{ QStringLiteral("Flip"), QStringLiteral("Nobody") };
	options.extent = QStringLiteral("Sheet");
	auto report = printCourseMaps(map, options, &printer);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files, QStringList{ QStringLiteral("Flip") });
	QCOMPARE(report.skipped, QStringList{ QStringLiteral("Nobody") });
	// The paper is that of the printer, a side a page.
	QCOMPARE(pagesOf(path), (std::vector<QSize>{ QSize(595, 842), QSize(595, 842) }));
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	QVERIFY(!map.hasUnsavedChanges());
	
	// A course with a map exchange is two print jobs, one for every sheet; the file has the last.
	options.courses = QStringList{ QStringLiteral("X") };
	report = printCourseMaps(map, options, &printer);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files, (QStringList{ QStringLiteral("X_1"), QStringLiteral("X_2") }));
	QCOMPARE(pagesOf(path), std::vector<QSize>{ QSize(595, 842) });
	
	// Landscape paper, and the canvases.
	auto data = map.courseData();
	CourseCanvas canvas;
	canvas.name = QStringLiteral("A");
	canvas.extent = QStringLiteral("Sheet");
	CanvasCourse entry;
	entry.course = QStringLiteral("Plain");
	canvas.courses.push_back(entry);
	data.canvases.canvases = { canvas };
	changeCourseData(map, data);
	printer.setPageOrientation(QPageLayout::Landscape);
	options.courses = QStringList{ QStringLiteral("Plain"), QStringLiteral("X") };
	options.use_canvases = true;
	report = printCourseMaps(map, options, &printer);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files, QStringList{ QStringLiteral("A.Plain") });
	QCOMPARE(report.skipped, (QStringList{ QStringLiteral("X_1"), QStringLiteral("X_2") }));
	QCOMPARE(pagesOf(path), std::vector<QSize>{ QSize(842, 595) });
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	
	// The dialog for printing asks for no folder and no kind of file.
	CourseMapsDialog dialog(&map, QString{}, nullptr, CourseMapsDialog::Print);
	auto* folder = dialog.findChild<QLineEdit*>(QStringLiteral("course-maps-folder"));
	auto* format = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-format"));
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	QVERIFY(folder && format && buttons);
	QVERIFY(!folder->isVisibleTo(&dialog));
	QVERIFY(!format->isVisibleTo(&dialog));
	QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
	QVERIFY(dialog.options().map_format.isEmpty());
	QCOMPARE(dialog.windowTitle(), CourseMapsDialog::tr("Print course maps"));
	// Several courses in one file, Accept (14)
	auto* placing = dialog.findChild<QComboBox*>(QStringLiteral("course-maps-placing"));
	QVERIFY(placing && placing->isVisibleTo(&dialog));
	QCOMPARE(placing->count(), 3);
}


/// Several courses in one file, Accept (11) and (12)
void CourseMapTest::oneFileTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	map.getSymbol(8)->setHidden(true);  // the plain symbol 101
	map.setHasUnsavedChanges(false);
	auto const objects_before = map.getNumObjects();
	
	// PDF: a page for every map, or all maps on one page.
	CourseMapExportOptions options;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("pages"));
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ QStringLiteral("X"), QStringLiteral("Flip"), QStringLiteral("Plain"), QStringLiteral("Nobody") };
	options.extent = QStringLiteral("Sheet");
	options.placing = CourseMapPlacing::Pages;
	auto report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.skipped, QStringList{ QStringLiteral("Nobody") });
	QCOMPARE(report.files.size(), 1);
	QCOMPARE(QFileInfo(report.files[0]).fileName(), QStringLiteral("m.Courses.pdf"));
	auto const page = QSize(283, 227);
	QCOMPARE(pagesOf(report.files[0]), (std::vector<QSize>{ page, page, page, page, page }));
	
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("original"));
	options.placing = CourseMapPlacing::Original;
	report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 1);
	QCOMPARE(QFileInfo(report.files[0]).fileName(), QStringLiteral("m.Courses.pdf"));
	QCOMPARE(pagesOf(report.files[0]), std::vector<QSize>{ page });
	
	// Printed: one job of all pages.
	{
		auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("printed.pdf"));
		QPrinter printer(QPrinter::HighResolution);
		printer.setOutputFormat(QPrinter::PdfFormat);
		printer.setOutputFileName(path);
		printer.setPageSize(QPageSize(QPageSize::A4));
		printer.setPageOrientation(QPageLayout::Portrait);
		options.placing = CourseMapPlacing::Pages;
		report = printCourseMaps(map, options, &printer);
		QVERIFY2(report.ok, qPrintable(report.error));
		QCOMPARE(report.files, QStringList{ QStringLiteral("Courses") });
		QCOMPARE(int(pagesOf(path).size()), 5);
	}
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	QVERIFY(!map.hasUnsavedChanges());
	
	// What cannot be placed so.
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("refused"));
	options.placing = CourseMapPlacing::Beside;
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	options.placing = CourseMapPlacing::Pages;
	options.map_format = QStringLiteral("XML");
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	options.map_format.clear();
	options.use_canvases = true;
	report = exportCourseMaps(map, options);
	QVERIFY(!report.ok);
	options.use_canvases = false;
	QVERIFY(QDir(options.folder).entryList(QDir::Files).isEmpty());
	
	// Map files: the map once for every map, beside or below each other, or once.
	map.getSymbol(8)->setHidden(false);
	auto const box = map.calculateExtent();
	options.courses = QStringList{ QStringLiteral("X"), QStringLiteral("Plain") };
	options.map_format = QStringLiteral("XML");
	options.extent.clear();
	auto const xOf = [](const Object* object) { return object->asPoint()->getCoordF().x(); };
	auto const yOf = [](const Object* object) { return object->asPoint()->getCoordF().y(); };
	for (auto const placing : { CourseMapPlacing::Beside, CourseMapPlacing::Below })
	{
		auto const beside = placing == CourseMapPlacing::Beside;
		options.placing = placing;
		options.folder = QDir(dir.path()).absoluteFilePath(beside ? QStringLiteral("beside") : QStringLiteral("below"));
		report = exportCourseMaps(map, options);
		QVERIFY2(report.ok, qPrintable(report.error));
		QCOMPARE(report.files.size(), 1);
		QCOMPARE(QFileInfo(report.files[0]).fileName(), QStringLiteral("m.Courses.foom"));
		Map file;
		QVERIFY(file.loadFrom(report.files[0]));
		auto const plain = objectsOf(file, 101);
		QCOMPARE(int(plain.size()), 3);
		auto const along = [beside, &xOf, &yOf](const Object* object) { return beside ? xOf(object) : yOf(object); };
		auto const across = [beside, &xOf, &yOf](const Object* object) { return beside ? yOf(object) : xOf(object); };
		auto const d = along(plain[1]) - along(plain[0]);
		QVERIFY2(d >= (beside ? box.width() : box.height()) + 10.0 - 0.01, qPrintable(QString::number(d)));
		QVERIFY(qAbs(along(plain[0]) - 5.0) < 0.01);
		QVERIFY(qAbs(along(plain[2]) - along(plain[1]) - d) < 0.01);
		for (auto const* object : plain)
			QVERIFY(qAbs(across(object) - 5.0) < 0.01);
		auto const starts = objectsOf(file, 701);
		QCOMPARE(int(starts.size()), 3);
		auto const offset = [beside](double d) { return beside ? MapCoordF(d, 0.0) : MapCoordF(0.0, d); };
		auto const near = [](MapCoordF a, MapCoordF b) { return qAbs(a.x() - b.x()) < 0.01 && qAbs(a.y() - b.y()) < 0.01; };
		QVERIFY(near(starts[0]->asPoint()->getCoordF(), MapCoordF(0.0, 0.0)));
		QVERIFY(near(starts[1]->asPoint()->getCoordF(), MapCoordF(20.0, 10.0) + offset(d)));
		QVERIFY(near(starts[2]->asPoint()->getCoordF(), offset(2 * d)));
		QCOMPARE(int(objectsOf(file, 703).size()), 7);
		auto titles = textsOf(file, 721);
		titles.sort();
		QCOMPARE(titles, (QStringList{ QStringLiteral("Plain"), QStringLiteral("X (1)"), QStringLiteral("X (2)") }));
		QVERIFY(file.courseData().courses.empty());
	}
	
	options.placing = CourseMapPlacing::Original;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("files-original"));
	report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 1);
	{
		Map file;
		QVERIFY(file.loadFrom(report.files[0]));
		QCOMPARE(int(objectsOf(file, 101).size()), 1);
		QStringList starts;
		for (auto const* object : objectsOf(file, 701))
			starts.push_back(QString::fromLatin1("%1,%2").arg(qRound(xOf(object))).arg(qRound(yOf(object))));
		QCOMPARE(starts, (QStringList{ QStringLiteral("0,0"), QStringLiteral("20,10"), QStringLiteral("0,0") }));
		QCOMPARE(int(objectsOf(file, 703).size()), 7);
		QCOMPARE(textsOf(file, 721).size(), 3);
	}
	
	// The map itself is as it was.
	QVERIFY(hiddenSymbols(map).isEmpty());
	QVERIFY(!map.hasUnsavedChanges());
	QCOMPARE(map.getNumObjects(), objects_before);
	QCOMPARE(int(map.courseData().courses.size()), 4);
}


/// Course only, intensity, line width and the control descriptions, Accept (15) to (18)
void CourseMapTest::printOptionsTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	// The block of the description away from the course, so that its white ground covers nothing of the page.
	for (int i = 0; i < map.getPart(0)->getNumObjects(); ++i)
	{
		auto* object = map.getPart(0)->getObject(i);
		if (object->getSymbol()->getNumberComponent(0) == 720)
			object->asPoint()->setPosition(MapCoordF(-150.0, -150.0));
	}
	// An out-of-bounds point, a course symbol which is no item.
	{
		auto* forbidden = addSymbol<PointSymbol>(map, 709, -1, Symbol::NoCourseRole);
		forbidden->setInnerColor(map.getColor(0));
		forbidden->setInnerRadius(500);
		drawPoint(map, forbidden, -20.0, 20.0);
	}
	map.setHasUnsavedChanges(false);
	
	CourseMapExportOptions options;
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ QStringLiteral("Plain") };
	options.extent = QStringLiteral("Sheet");
	auto const ppm = 10.0;
	// The pixel of a point of the map on the page of the extent (-40, -40, 100, 80).
	auto const pixel = [](const QImage& image, double x, double y) {
		return QColor(image.pixel(qRound((x + 40.0) * 10.0), qRound((y + 40.0) * 10.0)));
	};
	auto const purple = QColor(160, 51, 240);
	auto const near = [](const QColor& a, const QColor& b) {
		return qAbs(a.red() - b.red()) <= 8 && qAbs(a.green() - b.green()) <= 8 && qAbs(a.blue() - b.blue()) <= 8;
	};
	auto const white = QColor(Qt::white);
	auto const imageOf = [&map, ppm](const CourseMapExportOptions& options) {
		CourseMapExportReport report;
		auto images = courseMapImages(map, options, ppm, &report);
		if (!report.ok)
			qInfo("%s", qPrintable(report.error));
		return images.empty() ? QImage{} : images.front();
	};
	
	// Accept (15): with the map, course only, intensity.
	auto image = imageOf(options);
	QCOMPARE(image.size(), QSize(1000, 800));
	QVERIFY2(near(pixel(image, 5.0, 5.0), purple), qPrintable(pixel(image, 5.0, 5.0).name()));
	QVERIFY(near(pixel(image, 10.0, 0.0), purple));
	options.content = CourseMapContent::CourseOnly;
	image = imageOf(options);
	QVERIFY(!image.isNull());
	QVERIFY(near(pixel(image, 5.0, 5.0), white));
	QVERIFY(near(pixel(image, 10.0, 0.0), purple));
	QVERIFY(near(pixel(image, -20.0, 20.0), purple));
	options.content = CourseMapContent::Map;
	options.intensity = 50;
	image = imageOf(options);
	QVERIFY(!image.isNull());
	auto const half = QColor(255 - (255 - 160) / 2, 255 - (255 - 51) / 2, 255 - (255 - 240) / 2);
	QVERIFY2(near(pixel(image, 10.0, 0.0), half), qPrintable(pixel(image, 10.0, 0.0).name()));
	QVERIFY(near(pixel(image, 5.0, 5.0), purple));
	options.intensity = 100;
	
	// Accept (16): the adjustments of course only.
	options.content = CourseMapContent::CourseOnly;
	options.offset_y = 5.0;
	image = imageOf(options);
	QVERIFY(near(pixel(image, 10.0, -5.0), purple));
	QVERIFY(near(pixel(image, 10.0, 0.0), white));
	options.offset_y = 0.0;
	options.stretch_y = 110.0;
	image = imageOf(options);
	QVERIFY(near(pixel(image, 10.0, -4.0), purple));
	QVERIFY(near(pixel(image, 10.0, 0.0), white));
	options.stretch_y = 100.0;
	options.mirror = true;
	image = imageOf(options);
	QVERIFY(near(pixel(image, -10.0, 10.0), purple));
	QVERIFY(near(pixel(image, 30.0, 10.0), white));
	options.mirror = false;
	for (auto const& bad : { std::make_pair(&CourseMapExportOptions::offset_x, 26.0), std::make_pair(&CourseMapExportOptions::stretch_x, 120.0) })
	{
		auto wrong = options;
		wrong.*(bad.first) = bad.second;
		CourseMapExportReport report;
		QVERIFY(courseMapImages(map, wrong, ppm, &report).empty());
		QVERIFY(!report.ok && !report.error.isEmpty());
	}
	for (auto const& bad : { std::make_pair(&CourseMapExportOptions::intensity, 0), std::make_pair(&CourseMapExportOptions::line_width, 300) })
	{
		auto wrong = options;
		wrong.*(bad.first) = bad.second;
		CourseMapExportReport report;
		QVERIFY(courseMapImages(map, wrong, ppm, &report).empty());
		QVERIFY(!report.ok && !report.error.isEmpty());
	}
	
	// Accept (17): the line widths of the copies.
	{
		CourseMapSide side;
		side.course = QStringLiteral("Plain");
		auto drawing = drawCourseMap(map, map.courseData(), side);
		QVERIFY(drawing.drawn);
		scaleCourseLineWidths(drawing, 1.5);
		auto found = false;
		for (auto const& symbol : drawing.symbols)
		{
			if (symbol->getType() == Symbol::Line && symbol->getNumberComponent(0) == 705)
			{
				found = true;
				QCOMPARE(symbol->asLine()->getLineWidth(), 525);
			}
		}
		QVERIFY(found);
		QCOMPARE(map.getSymbol(6)->asLine()->getLineWidth(), 350);
	}
	
	// Accept (18): the control descriptions.
	options.content = CourseMapContent::Descriptions;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("descriptions"));
	auto report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 1);
	QCOMPARE(QFileInfo(report.files[0]).fileName(), QStringLiteral("m.Plain.Descriptions.pdf"));
	{
		CourseMapSide side;
		side.course = QStringLiteral("Plain");
		auto const drawing = drawCourseMap(map, map.courseData(), side);
		QRectF box;
		for (auto const& layout : drawing.description.layouts)
		{
			for (auto const& block : layout.blocks)
				box = box.isValid() ? box.united(block) : block;
		}
		QVERIFY(box.isValid());
		auto const pages = pagesOf(report.files[0]);
		QCOMPARE(int(pages.size()), 1);
		auto const points = 72.0 / 25.4;
		QVERIFY2(qAbs(pages[0].width() - (box.width() + 10.0) * points) <= 1.0, qPrintable(QString::number(pages[0].width())));
		QVERIFY2(qAbs(pages[0].height() - (box.height() + 10.0) * points) <= 1.0, qPrintable(QString::number(pages[0].height())));
		image = imageOf(options);
		QVERIFY(!image.isNull());
		auto dark = false;
		for (int y = 0; y < image.height() && !dark; ++y)
		{
			for (int x = 0; x < image.width() && !dark; ++x)
				dark = qGray(image.pixel(x, y)) < 128;
		}
		QVERIFY(dark);
	}
	options.map_format = QStringLiteral("XML");
	QVERIFY(!exportCourseMaps(map, options).ok);
	options.map_format.clear();
	options.placing = CourseMapPlacing::Original;
	QVERIFY(!exportCourseMaps(map, options).ok);
	options.placing = CourseMapPlacing::Separate;
	options.content = CourseMapContent::Map;
	options.intensity = 50;
	options.map_format = QStringLiteral("XML");
	QVERIFY(!exportCourseMaps(map, options).ok);
	
	QVERIFY(hiddenSymbols(map).isEmpty());
	QVERIFY(!map.hasUnsavedChanges());
}


/// The preview of a course map, Accept (10)
void CourseMapTest::previewTest()
{
	Map map;
	fillMap(map);
	map.getSymbol(8)->setHidden(true);  // the plain symbol 101
	map.setHasUnsavedChanges(false);
	auto data = map.courseData();
	auto const course_symbols = QStringList{ QStringLiteral("701"), QStringLiteral("703"), QStringLiteral("706"),
	                                         QStringLiteral("720"), QStringLiteral("721"), QStringLiteral("722.1"), QStringLiteral("101") };
	{
		CourseMapPreview preview(map);
		QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
		QVERIFY(!preview.draw().drawn);

		// The second part of X alone, as its map shows it.
		QVERIFY(preview.setSide(data, QStringLiteral("X"), 1));
		QCOMPARE(hiddenSymbols(map), course_symbols);
		auto drawing = preview.draw();
		QVERIFY(drawing.drawn);
		QCOMPARE(drawing.items.join(QLatin1Char(' ')), QStringLiteral("S2 33 34 F1"));
		QCOMPARE(drawing.texts, QStringList{ QStringLiteral("X (2)") });
		for (auto const& object : drawing.objects)
			QVERIFY(!object->getSymbol()->isHidden());
		// The same again changes nothing which is hidden; another course neither.
		QVERIFY(!preview.setSide(data, QStringLiteral("X"), 1));
		QVERIFY(!preview.setSide(data, QStringLiteral("Plain"), -1));
		QCOMPARE(preview.course(), QStringLiteral("Plain"));
		QCOMPARE(preview.draw().items.join(QLatin1Char(' ')), QStringLiteral("S1 31 32 33 F1"));

		// No course, or one which is not there: the map as it is.
		QVERIFY(preview.setSide(data, QString{}, -1));
		QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
		QVERIFY(!preview.setSide(data, QStringLiteral("Nobody"), -1));
		QVERIFY(preview.course().isEmpty());
		QVERIFY(!preview.draw().drawn);

		// With the canvases active, what the canvas hides is hidden too and not drawn.
		CourseCanvas canvas;
		canvas.name = QStringLiteral("A");
		canvas.hidden_symbols = QStringList{ QStringLiteral("704.000") };
		CanvasCourse entry;
		entry.course = QStringLiteral("Plain");
		canvas.courses.push_back(entry);
		data.canvases.canvases = { canvas };
		QVERIFY(preview.setSide(data, QStringLiteral("Plain"), -1));
		QVERIFY(!preview.canvas());
		QVERIFY(!hiddenSymbols(map).contains(QStringLiteral("704")));
		data.canvases.active = true;
		QVERIFY(preview.setSide(data, QStringLiteral("Plain"), -1));
		QVERIFY(preview.canvas());
		QCOMPARE(preview.canvas()->name, QStringLiteral("A"));
		QCOMPARE(preview.canvasSymbols(), QStringList{ QStringLiteral("704.000") });
		QVERIFY(hiddenSymbols(map).contains(QStringLiteral("704")));
		drawing = preview.draw();
		for (auto const& object : drawing.objects)
			QVERIFY(object->getSymbol()->getNumberComponent(0) != 704);
		// A course which no canvas has: the course symbols alone.
		QVERIFY(preview.setSide(data, QStringLiteral("X"), -1));
		QVERIFY(!preview.canvas());
		QCOMPARE(hiddenSymbols(map), course_symbols);
	}
	// Afterwards as before.
	QCOMPARE(hiddenSymbols(map), QStringList{ QStringLiteral("101") });
	QVERIFY(!map.hasUnsavedChanges());
}


/// Canvases: the background maps, Accept (8)
void CourseMapTest::canvasBackgroundTest()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	fillMap(map);
	map.addTemplate(0, std::make_unique<TemplateImage>(QDir(dir.path()).absoluteFilePath(QStringLiteral("north.png")), &map));
	map.addTemplate(1, std::make_unique<TemplateImage>(QDir(dir.path()).absoluteFilePath(QStringLiteral("south.png")), &map));
	auto* north = map.getTemplate(0);
	auto* south = map.getTemplate(1);
	QCOMPARE(south->getTemplateFilename(), QStringLiteral("south.png"));
	MapView view(&map);
	view.setTemplateVisibility(north, { 1.0, true });
	view.setTemplateVisibility(south, { 0.5, true });

	CourseCanvas canvas;
	canvas.name = QStringLiteral("A");
	canvas.extent = QStringLiteral("Sheet");
	canvas.hidden_templates = QStringList{ QStringLiteral("south.png"), QStringLiteral("elsewhere.ocd") };

	// The view of the print: as the view of the editor, without what the canvas hides.
	auto print_view = canvasPrintView(map, &view, canvas);
	QVERIFY(bool(print_view));
	QVERIFY(print_view->getTemplateVisibility(north).visible);
	QVERIFY(!print_view->getTemplateVisibility(south).visible);
	QVERIFY(view.getTemplateVisibility(south).visible);
	print_view = canvasPrintView(map, nullptr, canvas);
	QVERIFY(print_view->getTemplateVisibility(north).visible);
	QVERIFY(!print_view->getTemplateVisibility(south).visible);
	// A canvas which hides no background map of the map needs no view of its own.
	CourseCanvas plain_canvas;
	plain_canvas.hidden_templates = QStringList{ QStringLiteral("elsewhere.ocd") };
	QVERIFY(!canvasPrintView(map, &view, plain_canvas));

	// The export by the canvas; the view of the editor is as it was.
	auto data = map.courseData();
	data.canvases.active = true;
	data.canvases.used_templates = QStringList{ QStringLiteral("south.png") };
	CanvasCourse entry;
	entry.course = QStringLiteral("Plain");
	canvas.courses.push_back(entry);
	data.canvases.canvases = { canvas };
	changeCourseData(map, data);
	map.setHasUnsavedChanges(false);
	CourseMapExportOptions options;
	options.folder = QDir(dir.path()).absoluteFilePath(QStringLiteral("out"));
	options.base_name = QStringLiteral("m");
	options.courses = QStringList{ QStringLiteral("Plain") };
	options.use_canvases = true;
	options.view = &view;
	auto const report = exportCourseMaps(map, options);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 1);
	QVERIFY(view.getTemplateVisibility(south).visible);

	// The preview hides it in the view while the course is shown, and puts it back as it was.
	{
		CourseMapPreview preview(map, &view);
		QVERIFY(preview.setSide(map.courseData(), QStringLiteral("Plain"), -1));
		QVERIFY(!view.getTemplateVisibility(south).visible);
		QVERIFY(view.getTemplateVisibility(north).visible);
		QVERIFY(preview.setSide(map.courseData(), QStringLiteral("X"), -1));
		QVERIFY(view.getTemplateVisibility(south).visible);
		QCOMPARE(view.getTemplateVisibility(south).opacity, 0.5);
		QVERIFY(preview.setSide(map.courseData(), QStringLiteral("Plain"), -1));
		QVERIFY(!view.getTemplateVisibility(south).visible);
	}
	QVERIFY(view.getTemplateVisibility(south).visible);
	QCOMPARE(view.getTemplateVisibility(south).opacity, 0.5);
	QVERIFY(!map.hasUnsavedChanges());

	// The dialog of the canvases lists the background maps of the map, and those which the canvases name.
	CourseCanvasesDialog dialog(&map);
	auto* templates = dialog.findChild<QListWidget*>(QStringLiteral("canvas-templates"));
	QVERIFY(templates);
	QCOMPARE(templates->count(), 3);
	QCOMPARE(templates->item(0)->text(), QStringLiteral("north.png"));
	QCOMPARE(templates->item(1)->text(), QStringLiteral("south.png"));
	QCOMPARE(templates->item(2)->text(), QStringLiteral("elsewhere.ocd"));
	QCOMPARE(dialog.currentCanvas(), 0);
	QCOMPARE(templates->item(0)->checkState(), Qt::Unchecked);
	QCOMPARE(templates->item(1)->checkState(), Qt::Checked);
	QCOMPARE(templates->item(2)->checkState(), Qt::Checked);
	templates->item(0)->setCheckState(Qt::Checked);
	templates->item(1)->setCheckState(Qt::Unchecked);
	QCOMPARE(dialog.canvases().canvases[0].hidden_templates, (QStringList{ QStringLiteral("elsewhere.ocd"), QStringLiteral("north.png") }));
	QVERIFY(dialog.canvases().used_templates.contains(QStringLiteral("north.png")));
	QVERIFY(dialog.canvases().used_templates.contains(QStringLiteral("south.png")));
}


/// Canvases: the frame of the extent, Accept (9)
void CourseMapTest::canvasFrameTest()
{
	Map map;
	fillMap(map);
	auto data = map.courseData();
	CourseCanvas a;
	a.name = QStringLiteral("A");
	a.extent = QStringLiteral("Sheet");
	CanvasCourse entry;
	entry.course = QStringLiteral("X");
	entry.part = 0;
	a.courses.push_back(entry);
	CourseCanvas b;
	b.name = QStringLiteral("B");
	b.extent = QStringLiteral("Gone");
	entry = {};
	entry.course = QStringLiteral("Plain");
	b.courses.push_back(entry);
	data.canvases.canvases = { a, b };

	// Only with the canvases active.
	QVERIFY(!courseCanvasFrame(map, data, QStringLiteral("X"), 0).shown);
	data.canvases.active = true;
	auto frame = courseCanvasFrame(map, data, QStringLiteral("X"), 0);
	QVERIFY(frame.shown);
	QCOMPARE(frame.name, QStringLiteral("Sheet"));
	QCOMPARE(frame.canvas, QStringLiteral("A"));
	QCOMPARE(frame.rect, QRectF(-40.0, -40.0, 100.0, 80.0));
	// All parts with the canvas of the first part; a part without a canvas, an extent which the map has not, no course: none.
	QVERIFY(courseCanvasFrame(map, data, QStringLiteral("X"), -1).shown);
	QVERIFY(!courseCanvasFrame(map, data, QStringLiteral("X"), 1).shown);
	QVERIFY(!courseCanvasFrame(map, data, QStringLiteral("Plain"), -1).shown);
	QVERIFY(!courseCanvasFrame(map, data, QString{}, -1).shown);
	QVERIFY(!courseCanvasFrame(map, data, QStringLiteral("Nobody"), -1).shown);

	// Painted in red over the map: 1 pixel for a mm, the map's origin in the middle of the image.
	QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	{
		QPainter painter(&image);
		painter.translate(100.0, 100.0);
		paintCanvasFrame(&painter, frame, 1.0);
	}
	auto const left = QColor(image.pixel(60, 100));
	QVERIFY2(left.red() > 200 && left.green() < 120 && left.blue() < 120, qPrintable(left.name()));
	auto const inside = QColor(image.pixel(100, 120));
	QCOMPARE(inside, QColor(Qt::white));
	// A frame which is not shown paints nothing.
	QImage empty(200, 200, QImage::Format_ARGB32_Premultiplied);
	empty.fill(Qt::white);
	{
		QPainter painter(&empty);
		paintCanvasFrame(&painter, CanvasFrame{}, 1.0);
	}
	QCOMPARE(QColor(empty.pixel(60, 100)), QColor(Qt::white));
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(CourseMapTest)
#include "course_map_t.moc"  // IWYU pragma: keep
