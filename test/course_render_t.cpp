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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QColor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_data.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/widgets/course_widget.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// A place given with the y axis up, as the specification speaks of directions.
MapCoord at(double x, double y_up)
{
	return MapCoord(x, -y_up);
}

/// The sizes which a number has without a text symbol: Arial of 4 mm.
constexpr double number_em = 4.0;
constexpr double number_digit = 0.556 * number_em;
constexpr double number_below = 0.212 * number_em;
constexpr double number_above = 1.023 * number_em;

constexpr double control_extent = 3.175;
constexpr double control_gap = 3.0;
constexpr double start_gap = 3.71;
constexpr double finish_gap = 3.5;

/// A map of the scale 1:10,000 with the symbols of start, control, finish
/// and marked route, the objects of the cases 2 to 5, and their course objects.
struct RenderMap
{
	Map map;
	MapColor* purple;
	PointSymbol* start_symbol;
	PointSymbol* control_symbol;
	PointSymbol* finish_symbol;
	LineSymbol* route_symbol;
	CourseData data;
	
	RenderMap()
	{
		map.setScaleDenominator(10000);
		purple = new MapColor(QStringLiteral("Purple"), 0);
		map.addColor(purple, 0);
		
		// The line of 0.35 mm has its middle on a circle of 6.00 mm.
		control_symbol = new PointSymbol();
		control_symbol->setNumberComponent(0, 703);
		control_symbol->setInnerRadius(2825);
		control_symbol->setOuterWidth(350);
		control_symbol->setOuterColor(purple);
		control_symbol->setCourseRole(Symbol::CourseControl);
		map.addSymbol(control_symbol, map.getNumSymbols());
		
		start_symbol = triangle(3.71, 0.35);
		start_symbol->setNumberComponent(0, 701);
		start_symbol->setCourseRole(Symbol::CourseStart);
		map.addSymbol(start_symbol, map.getNumSymbols());
		
		// Circles of 7.00 and 5.00 mm.
		finish_symbol = new PointSymbol();
		finish_symbol->setNumberComponent(0, 706);
		finish_symbol->setInnerRadius(3325);
		finish_symbol->setOuterWidth(350);
		finish_symbol->setOuterColor(purple);
		auto* inner = new PointSymbol();
		inner->setInnerRadius(2325);
		inner->setOuterWidth(350);
		inner->setOuterColor(purple);
		finish_symbol->addElement(0, new PointObject(inner), inner);
		finish_symbol->setCourseRole(Symbol::CourseFinish);
		map.addSymbol(finish_symbol, map.getNumSymbols());
		
		route_symbol = new LineSymbol();
		route_symbol->setNumberComponent(0, 707);
		route_symbol->setLineWidth(0.35);
		route_symbol->setColor(purple);
		route_symbol->setCourseRole(Symbol::CourseMarkedRoute);
		map.addSymbol(route_symbol, map.getNumSymbols());
		
		point(CourseObject::Start, "S1", start_symbol, 0, 0);
		point(CourseObject::Control, "31", control_symbol, 20, 0);
		point(CourseObject::Control, "32", control_symbol, 40, 0);
		point(CourseObject::Control, "33", control_symbol, 45, 0);
		route("M1", { at(45, 3), at(45, 13) });
		point(CourseObject::Control, "34", control_symbol, 65, 13);
		point(CourseObject::Finish, "F1", finish_symbol, 65, 33);
		route("M0", { at(-20, 0), at(-5, 0) });
		
		// The controls of the numbers.
		point(CourseObject::Control, "a", control_symbol, 80, 0);
		point(CourseObject::Control, "b", control_symbol, 100, 0);
		point(CourseObject::Control, "c", control_symbol, 100, -20);
		point(CourseObject::Control, "d", control_symbol, 120, -20);
		point(CourseObject::Control, "e", control_symbol, 140, -20);
		point(CourseObject::Control, "f", control_symbol, 140, -40);
		point(CourseObject::Control, "g", control_symbol, 140, -60);
		
		course("A", { "sS1", "c31", "c32", "c33", "mM1", "c34", "fF1" });
		course("X", { "sS1", "c31", "g", "sS1", "c32", "fF1" });
		course("L", { "mM0", "sS1", "c31", "fF1" });
		course("N", { "ca", "cb", "cc", "cd", "ce", "cf", "cg" });
		course("R", { "ca", "cb", "cc", "cb", "cd" });
	}
	
	/// A point symbol which is a triangle of line elements with its tip up.
	PointSymbol* triangle(double tip, double width)
	{
		auto* line = new LineSymbol();
		line->setLineWidth(width);
		line->setColor(purple);
		auto const x = tip * std::cos(qDegreesToRadians(30.0));
		auto const y = tip * std::sin(qDegreesToRadians(30.0));
		auto* path = new PathObject(line, { at(-x, -y), at(0, tip), at(x, -y), at(-x, -y) });
		auto* symbol = new PointSymbol();
		symbol->setRotatable(true);
		symbol->addElement(0, path, line);
		return symbol;
	}
	
	void add(CourseObject::Kind kind, const char* code, Object* object)
	{
		map.addObject(object);
		CourseObject course_object;
		course_object.kind = kind;
		course_object.code = QString::fromLatin1(code);
		course_object.object = object->uuid();
		data.objects.push_back(course_object);
	}
	
	void point(CourseObject::Kind kind, const char* code, const Symbol* symbol, double x, double y_up)
	{
		auto* object = new PointObject(symbol);
		object->setPosition(at(x, y_up));
		add(kind, code, object);
	}
	
	void route(const char* code, std::initializer_list<MapCoord> coords)
	{
		add(CourseObject::MarkedRoute, code, new PathObject(route_symbol, MapCoordVector(coords)));
	}
	
	/// A course from items as the format has them: "sS1", "c31", "g" ...
	void course(const char* name, std::initializer_list<const char*> items)
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
			case 'g': item.type = CourseItem::MapExchange; break;
			case 'k': item.type = CourseItem::CrossingPoint; break;
			case 'r': item.type = CourseItem::TeamForkBegin; break;
			default: Q_UNREACHABLE();
			}
			item.code = QString::fromLatin1(text + 1);
			result.items.push_back(item);
		}
		data.courses.push_back(result);
	}
	
	Course& find(const char* name)
	{
		for (auto& entry : data.courses)
		{
			if (entry.name == QLatin1String(name))
				return entry;
		}
		Q_UNREACHABLE();
	}
	
	/// Adds the line symbol 705 and the text symbol 704.
	void addDrawingSymbols()
	{
		auto* line = new LineSymbol();
		line->setNumberComponent(0, 705);
		line->setLineWidth(0.35);
		line->setColor(purple);
		map.addSymbol(line, map.getNumSymbols());
		auto* text = new TextSymbol();
		text->setNumberComponent(0, 704);
		text->setFontFamily(QStringLiteral("Arial"));
		text->setFontSize(4000);
		text->setColor(purple);
		map.addSymbol(text, map.getNumSymbols());
	}
};

const CourseLegDrawing* findLeg(const CourseDrawing& drawing, const char* from, const char* to, int skip = 0)
{
	for (auto const& leg : drawing.legs)
	{
		if (leg.from == QLatin1String(from) && leg.to == QLatin1String(to) && skip-- == 0)
			return &leg;
	}
	return nullptr;
}

const CourseNumberDrawing* findNumber(const CourseDrawing& drawing, const char* code)
{
	for (auto const& number : drawing.numbers)
	{
		if (number.code == QLatin1String(code))
			return &number;
	}
	return nullptr;
}

bool near(const MapCoordF& actual, double x, double y_up, double tolerance = 0.002)
{
	return std::abs(actual.x() - x) <= tolerance && std::abs(actual.y() + y_up) <= tolerance;
}

QString show(const MapCoordF& coord)
{
	return QStringLiteral("(%1, %2)").arg(coord.x(), 0, 'f', 3).arg(-coord.y(), 0, 'f', 3);
}

const Symbol* symbolWithNumber(const Map& map, int number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getNumberComponent(0) == number && symbol->getNumberComponent(1) <= 0)
			return symbol;
	}
	return nullptr;
}

}  // namespace



/**
 * @test Tests the drawing of a course, its length and climb.
 *
 * See the design notes, "Drawing a course, its length and climb".
 */
class CourseRenderTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void extentTest();
	void legTest();
	void numberTest();
	void overrideTest();
	void lengthTest();
	void dockTest();
	void pictureTest();
};


void CourseRenderTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseRenderTest::extentTest()
{
	RenderMap m;
	QCOMPARE(courseSymbolExtent(m.control_symbol), 3.175);
	QCOMPARE(courseSymbolExtent(m.finish_symbol), 3.675);
	
	std::unique_ptr<PointSymbol> big_start { m.triangle(4.72, 0.52) };
	// The corners are stored to a thousandth of a millimetre.
	QVERIFY(std::abs(courseSymbolExtent(big_start.get()) - 4.98) < 0.001);
	QVERIFY(std::abs(courseSymbolExtent(m.start_symbol) - 3.885) < 0.001);
	
	PointSymbol dot;
	dot.setInnerRadius(500);
	dot.setInnerColor(m.purple);
	dot.setOuterWidth(0);
	QCOMPARE(courseSymbolExtent(&dot), 0.5);
	
	// What has no colour reaches nowhere, and there is no symbol.
	PointSymbol nothing;
	nothing.setInnerColor(nullptr);
	nothing.setOuterColor(nullptr);
	QCOMPARE(courseSymbolExtent(&nothing), 0.0);
	QCOMPARE(courseSymbolExtent(nullptr), 0.0);
	// A line reaches half its width.
	QCOMPARE(courseSymbolExtent(m.route_symbol), 0.175);
}


/// Accept (2)
void CourseRenderTest::legTest()
{
	RenderMap m;
	auto drawing = drawCourse(m.map, m.data, QStringLiteral("A"));
	QVERIFY(drawing.drawn);
	QVERIFY(drawing.objects.empty());
	QCOMPARE(int(drawing.legs.size()), 6);
	
	auto const* leg = findLeg(drawing, "S1", "31");
	QVERIFY(leg && leg->has_line && !leg->overridden);
	QVERIFY2(near(leg->begin, start_gap, 0), qPrintable(show(leg->begin)));
	QVERIFY2(near(leg->end, 20 - control_gap, 0), qPrintable(show(leg->end)));
	leg = findLeg(drawing, "31", "32");
	QVERIFY(leg && leg->has_line);
	QVERIFY2(near(leg->begin, 23, 0), qPrintable(show(leg->begin)));
	QVERIFY2(near(leg->end, 37, 0), qPrintable(show(leg->end)));
	// Two controls 5 mm apart, and a route which begins on the circle.
	leg = findLeg(drawing, "32", "33");
	QVERIFY(leg && !leg->has_line);
	leg = findLeg(drawing, "33", "M1");
	QVERIFY(leg && !leg->has_line);
	// The line from the end of the route begins at its last point.
	leg = findLeg(drawing, "M1", "34");
	QVERIFY(leg && leg->has_line);
	QVERIFY2(near(leg->begin, 45, 13), qPrintable(show(leg->begin)));
	QVERIFY2(near(leg->end, 62, 13), qPrintable(show(leg->end)));
	leg = findLeg(drawing, "34", "F1");
	QVERIFY(leg && leg->has_line);
	QVERIFY2(near(leg->begin, 65, 16), qPrintable(show(leg->begin)));
	QVERIFY2(near(leg->end, 65, 33 - finish_gap), qPrintable(show(leg->end)));
	
	// The start is turned to the first control, which lies in the east.
	QCOMPARE(int(drawing.starts.size()), 1);
	QCOMPARE(drawing.starts[0].code, QStringLiteral("S1"));
	QVERIFY(std::abs(drawing.starts[0].angle + 90.0) < 1e-6);
	
	// The distance from circle to line.
	m.data.options.setValue(QLatin1Char('l'), QStringLiteral("0.50"));
	drawing = drawCourse(m.map, m.data, QStringLiteral("A"));
	leg = findLeg(drawing, "31", "32");
	QVERIFY2(near(leg->begin, 23.5, 0), qPrintable(show(leg->begin)));
	QVERIFY2(near(leg->end, 36.5, 0), qPrintable(show(leg->end)));
	leg = findLeg(drawing, "M1", "34");
	QVERIFY2(near(leg->begin, 45.5, 13), qPrintable(show(leg->begin)));
	m.data.options.setValue(QLatin1Char('l'), QStringLiteral("0.00"));
	
	// A map exchange: no line over it, and a start behind it.
	drawing = drawCourse(m.map, m.data, QStringLiteral("X"));
	QVERIFY(drawing.drawn);
	QVERIFY(findLeg(drawing, "S1", "31")->has_line);
	QVERIFY(!findLeg(drawing, "31", "S1"));
	leg = findLeg(drawing, "S1", "32");
	QVERIFY(leg && leg->has_line);
	QVERIFY2(near(leg->begin, start_gap, 0), qPrintable(show(leg->begin)));
	QCOMPARE(int(drawing.starts.size()), 2);
	
	// With the two symbols the lines and numbers are objects.
	m.addDrawingSymbols();
	drawing = drawCourse(m.map, m.data, QStringLiteral("A"));
	QCOMPARE(int(drawing.objects.size()), 4 + 4);
	auto paths = 0;
	auto texts = 0;
	for (auto const& object : drawing.objects)
	{
		if (object->getType() == Object::Path)
		{
			++paths;
			QCOMPARE(object->getSymbol()->getNumberComponent(0), 705);
		}
		else if (object->getType() == Object::Text)
		{
			++texts;
			QCOMPARE(object->getSymbol()->getNumberComponent(0), 704);
		}
	}
	QCOMPARE(paths, 4);
	QCOMPARE(texts, 4);
	
	// A course which is not there, and one with a variation, are not drawn.
	QVERIFY(!drawCourse(m.map, m.data, QStringLiteral("Z")).drawn);
	m.course("V", { "sS1", "r", "c31", "fF1" });
	QVERIFY(!drawCourse(m.map, m.data, QStringLiteral("V")).drawn);
}


/// Accept (3)
void CourseRenderTest::numberTest()
{
	RenderMap m;
	auto drawing = drawCourse(m.map, m.data, QStringLiteral("N"));
	QVERIFY(drawing.drawn);
	QCOMPARE(int(drawing.numbers.size()), 7);
	auto const rho = control_extent + 1.0;
	auto const diagonal = rho * std::sqrt(0.5);
	
	// b: neighbours in the west and the south, the number in the north-east.
	auto const* number = findNumber(drawing, "b");
	QVERIFY(number);
	QCOMPARE(number->text, QStringLiteral("2"));
	QVERIFY(std::abs(number->direction - 45.0) < 1e-6);
	QVERIFY2(near(number->anchor, 100 + diagonal, diagonal + number_below), qPrintable(show(number->anchor)));
	QVERIFY(std::abs(number->box.left() - (100 + diagonal)) < 0.002);
	QVERIFY(std::abs(number->box.width() - number_digit) < 0.002);
	QVERIFY(std::abs(number->box.height() - (number_below + number_above)) < 0.002);
	// The lower side of the box, which is the greater y of the map.
	QVERIFY(std::abs(number->box.bottom() + diagonal) < 0.002);
	
	// c: neighbours in the north and the east, the number in the south-west:
	// the upper right corner of the box at P.
	number = findNumber(drawing, "c");
	QVERIFY(std::abs(number->direction + 135.0) < 1e-6);
	QVERIFY(std::abs(number->box.right() - (100 - diagonal)) < 0.002);
	QVERIFY(std::abs(number->box.top() - (20 + diagonal)) < 0.002);
	
	// d: neighbours in the west and the east: above or below, the middle of the side at P.
	number = findNumber(drawing, "d");
	QVERIFY(std::abs(std::abs(number->direction) - 90.0) < 1e-6);
	QVERIFY(std::abs(number->box.center().x() - 120.0) < 0.002);
	if (number->direction < 0)
		QVERIFY(std::abs(number->box.top() - (20 + rho)) < 0.002);
	else
		QVERIFY(std::abs(number->box.bottom() - (20 - rho)) < 0.002);
	
	// f: neighbours in the north and the south: at the side, 1.5 mm from P.
	number = findNumber(drawing, "f");
	QVERIFY(std::abs(std::abs(number->direction) - 180.0) < 1e-6 || std::abs(number->direction) < 1e-6);
	QVERIFY(std::abs(number->box.center().y() - 40.0) < 0.002);
	if (std::abs(number->direction) > 90)
		QVERIFY(std::abs(number->box.right() - (140 - rho - 1.5)) < 0.002);
	else
		QVERIFY(std::abs(number->box.left() - (140 + rho + 1.5)) < 0.002);
	
	// a: one neighbour, in the east: the number in the west. g: one in the north.
	number = findNumber(drawing, "a");
	QCOMPARE(number->text, QStringLiteral("1"));
	QVERIFY(std::abs(std::abs(number->direction) - 180.0) < 1e-6);
	QVERIFY(std::abs(number->box.right() - (80 - rho - 1.5)) < 0.002);
	number = findNumber(drawing, "g");
	QCOMPARE(number->text, QStringLiteral("7"));
	QVERIFY(std::abs(number->direction + 90.0) < 1e-6);
	QVERIFY(std::abs(number->box.top() - (60 + rho)) < 0.002);
	QVERIFY(std::abs(number->box.center().x() - 140.0) < 0.002);
	
	// A control which is visited twice has one number.
	drawing = drawCourse(m.map, m.data, QStringLiteral("R"));
	QCOMPARE(int(drawing.numbers.size()), 4);
	number = findNumber(drawing, "b");
	QCOMPARE(number->text, QStringLiteral("2/4"));
	QVERIFY(std::abs(number->box.width() - 3 * number_digit) < 0.01);
	// The neighbours of the first visit count: a in the west, c in the south.
	QVERIFY(std::abs(number->direction - 45.0) < 1e-6);
	QCOMPARE(findNumber(drawing, "d")->text, QStringLiteral("5"));
	m.data.options.setValue(QLatin1Char('p'), QStringLiteral("1"));
	drawing = drawCourse(m.map, m.data, QStringLiteral("R"));
	QCOMPARE(findNumber(drawing, "b")->text, QStringLiteral("2./4."));
	QCOMPARE(findNumber(drawing, "a")->text, QStringLiteral("1."));
	m.data.options.setValue(QLatin1Char('p'), QStringLiteral("0"));
	
	// The distance to the number.
	m.data.options.setValue(QLatin1Char('n'), QStringLiteral("2.00"));
	drawing = drawCourse(m.map, m.data, QStringLiteral("N"));
	QVERIFY(std::abs(findNumber(drawing, "g")->box.top() - (60 + rho + 1.0)) < 0.002);
	m.data.options.setValue(QLatin1Char('n'), QStringLiteral("1.00"));
	
	// Behind a map exchange the controls are counted on, and the control
	// before it has the start for its only neighbour.
	drawing = drawCourse(m.map, m.data, QStringLiteral("X"));
	QCOMPARE(int(drawing.numbers.size()), 2);
	QCOMPARE(findNumber(drawing, "31")->text, QStringLiteral("1"));
	QVERIFY(std::abs(std::abs(findNumber(drawing, "31")->direction)) < 1e-6);
	QCOMPARE(findNumber(drawing, "32")->text, QStringLiteral("2"));
}


/// Accept (4)
void CourseRenderTest::overrideTest()
{
	RenderMap m;
	m.addDrawingSymbols();
	auto const* line_symbol = symbolWithNumber(m.map, 705);
	auto const* text_symbol = symbolWithNumber(m.map, 704);
	auto* part = ensureCourseOverridesPart(m.map);
	auto const part_index = m.map.findPartIndex(part);
	
	auto* first = new PathObject(line_symbol, { at(23, 0), at(28, 2) });
	auto* second = new PathObject(line_symbol, { at(31, 2), at(37, 0) });
	auto* moved = new TextObject(text_symbol);
	moved->setAnchorPosition(MapCoordF(at(22, -9)));
	moved->setText(QStringLiteral("old"));
	for (auto* object : std::initializer_list<Object*>{ first, second, moved })
		m.map.addObject(object, part_index);
	
	CourseOverride override;
	override.course = QStringLiteral("A");
	override.what = CourseOverride::Line;
	override.code_from = QStringLiteral("31");
	override.code_to = QStringLiteral("32");
	override.object = first->uuid();
	m.data.overrides.push_back(override);
	override.object = second->uuid();
	m.data.overrides.push_back(override);
	override.what = CourseOverride::Number;
	override.code_from = QStringLiteral("31");
	override.code_to.clear();
	override.object = moved->uuid();
	m.data.overrides.push_back(override);
	// An override whose object is not in the map, and one of another course.
	override.code_from = QStringLiteral("32");
	override.object = QUuid::createUuid();
	m.data.overrides.push_back(override);
	override.course = QStringLiteral("X");
	override.code_from = QStringLiteral("34");
	override.object = moved->uuid();
	m.data.overrides.push_back(override);
	
	auto const drawing = drawCourse(m.map, m.data, QStringLiteral("A"));
	auto const* leg = findLeg(drawing, "31", "32");
	QVERIFY(leg && leg->overridden && !leg->has_line);
	QVERIFY(!findLeg(drawing, "S1", "31")->overridden);
	QVERIFY(findNumber(drawing, "31")->overridden);
	QVERIFY(!findNumber(drawing, "32")->overridden);
	QVERIFY(!findNumber(drawing, "34")->overridden);
	QVERIFY(near(findNumber(drawing, "31")->anchor, 22, -9));
	
	// Lines: S1-31, the two pieces, M1-34, 34-F1. Numbers: four.
	auto pieces = 0;
	auto moved_texts = 0;
	QCOMPARE(int(drawing.objects.size()), 5 + 4);
	for (auto const& object : drawing.objects)
	{
		QVERIFY(object.get() != first && object.get() != second && object.get() != moved);
		if (object->getType() == Object::Path)
		{
			auto const& coords = object->asPath()->getRawCoordinateVector();
			if (coords.size() == 2 && (coords[1] == at(28, 2) || coords[0] == at(31, 2)))
				++pieces;
		}
		else if (object->getType() == Object::Text && near(object->asText()->getAnchorCoordF(), 22, -9))
		{
			++moved_texts;
			// The text is that of the course, not that which the object had.
			QCOMPARE(object->asText()->getText(), QStringLiteral("1"));
		}
	}
	QCOMPARE(pieces, 2);
	QCOMPARE(moved_texts, 1);
	// The course X has its own lines.
	QVERIFY(!findLeg(drawCourse(m.map, m.data, QStringLiteral("X")), "S1", "31")->overridden);
}


/// Accept (5)
void CourseRenderTest::lengthTest()
{
	RenderMap m;
	auto length = measureCourse(m.map, m.data, m.find("A"));
	QVERIFY(length.known);
	// 20 + 20 + 5 + 3 + the route of 10 + 20 + 20 mm at 1:10,000
	QVERIFY2(std::abs(length.measured_m - 980.0) < 0.01, qPrintable(QString::number(length.measured_m)));
	QVERIFY(std::abs(length.total_m - 980.0) < 0.01);
	
	// The straight over a map exchange counts.
	length = measureCourse(m.map, m.data, m.find("X"));
	QVERIFY(length.known);
	QVERIFY2(std::abs(length.measured_m - (20 + 20 + 40 + std::hypot(25.0, 33.0)) * 10) < 0.01, qPrintable(QString::number(length.measured_m)));
	
	// A marked route before the start counts with its path alone.
	m.find("L").extra_km = 0.2;
	m.find("L").extra_set = true;
	length = measureCourse(m.map, m.data, m.find("L"));
	QVERIFY(length.known);
	QVERIFY2(std::abs(length.measured_m - (15 + 20 + std::hypot(45.0, 33.0)) * 10) < 0.01, qPrintable(QString::number(length.measured_m)));
	QVERIFY(std::abs(length.total_m - length.measured_m - 200.0) < 1e-6);
	// Elsewhere the straight from a route to a start counts.
	m.course("L2", { "sS1", "c31", "mM0", "sS1", "fF1" });
	length = measureCourse(m.map, m.data, m.find("L2"));
	QVERIFY2(std::abs(length.measured_m - (20 + 40 + 15 + 5 + std::hypot(65.0, 33.0)) * 10) < 0.01, qPrintable(QString::number(length.measured_m)));
	// A crossing point has no place.
	m.course("K", { "sS1", "k", "c31", "fF1" });
	length = measureCourse(m.map, m.data, m.find("K"));
	QVERIFY(std::abs(length.measured_m - (20 + std::hypot(45.0, 33.0)) * 10) < 0.01);
	
	QCOMPARE(formatCourseLength(1496.4), QStringLiteral("1.500 km"));
	QCOMPARE(formatCourseLength(3302.0), QStringLiteral("3.300 km"));
	QCOMPARE(formatCourseLength(5837.5), QStringLiteral("5.840 km"));
	QCOMPARE(formatCourseLength(0.0), QStringLiteral("0.000 km"));
	
	// What cannot be measured.
	m.course("V", { "sS1", "r", "c31", "fF1" });
	QVERIFY(!measureCourse(m.map, m.data, m.find("V")).known);
	m.course("G", { "sS1", "c99", "fF1" });
	QVERIFY(!measureCourse(m.map, m.data, m.find("G")).known);
	m.data.objects[1].object = QUuid::createUuid();
	QVERIFY(!measureCourse(m.map, m.data, m.find("A")).known);
	// It is drawn as far as it can be.
	QVERIFY(drawCourse(m.map, m.data, QStringLiteral("A")).drawn);
	QVERIFY(findLeg(drawCourse(m.map, m.data, QStringLiteral("A")), "S1", "32"));
}





/// Accept (7)
void CourseRenderTest::dockTest()
{
	RenderMap m;
	changeCourseData(m.map, m.data);
	CourseWidget widget(&m.map);
	auto* length = widget.findChild<QLabel*>(QStringLiteral("course-length"));
	auto* climb = widget.findChild<QSpinBox*>(QStringLiteral("course-climb"));
	auto* extra = widget.findChild<QDoubleSpinBox*>(QStringLiteral("course-extra"));
	QVERIFY(length);
	QVERIFY(climb);
	QVERIFY(extra);
	
	// Without a course there is nothing to show or to change.
	QVERIFY(!climb->isEnabled());
	QVERIFY(!extra->isEnabled());
	
	widget.setCurrentCourse(QStringLiteral("A"));
	QCOMPARE(length->text(), QStringLiteral("0.980 km"));
	QVERIFY(climb->isEnabled());
	QCOMPARE(climb->value(), 0);
	QCOMPARE(extra->value(), 0.0);
	
	auto const steps = m.map.undoManager().undoStepCount();
	climb->setValue(35);
	QCOMPARE(m.map.courseData().findCourse(QStringLiteral("A"))->climb_m, 35);
	QVERIFY(m.map.courseData().findCourse(QStringLiteral("A"))->climb_manual);
	QCOMPARE(m.map.undoManager().undoStepCount(), steps + 1);
	extra->setValue(0.12);
	QCOMPARE(m.map.courseData().findCourse(QStringLiteral("A"))->extra_km, 0.12);
	QVERIFY(m.map.courseData().findCourse(QStringLiteral("A"))->extra_set);
	QCOMPARE(m.map.undoManager().undoStepCount(), steps + 2);
	QCOMPARE(length->text(), QStringLiteral("1.100 km"));
	QCOMPARE(widget.currentCourse(), QStringLiteral("A"));
	
	QVERIFY(m.map.undoManager().undo());
	QCOMPARE(length->text(), QStringLiteral("0.980 km"));
	QCOMPARE(extra->value(), 0.0);
	QCOMPARE(climb->value(), 35);
	QVERIFY(m.map.undoManager().undo());
	QCOMPARE(climb->value(), 0);
	
	// The other course has its own values.
	widget.setCurrentCourse(QStringLiteral("X"));
	QCOMPARE(length->text(), formatCourseLength((20 + 20 + 40 + std::hypot(25.0, 33.0)) * 10));
	
	// A course with a variation shows a dash.
	auto data = m.map.courseData();
	Course relay;
	relay.name = QStringLiteral("V");
	CourseItem fork;
	fork.type = CourseItem::TeamForkBegin;
	relay.items.push_back(fork);
	data.courses.push_back(relay);
	changeCourseData(m.map, data);
	widget.setCurrentCourse(QStringLiteral("V"));
	QCOMPARE(length->text(), QStringLiteral("-"));
}


/// Accept (8)
void CourseRenderTest::pictureTest()
{
	RenderMap m;
	for (auto with_symbols : { false, true })
	{
		if (with_symbols)
			m.addDrawingSymbols();
		auto const drawing = drawCourse(m.map, m.data, QStringLiteral("A"));
		QCOMPARE(drawing.objects.empty(), !with_symbols);
		
		// 10 pixels for a millimetre; the map from (-30, -70) to (150, 50).
		QImage image(1800, 1200, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::white);
		{
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.scale(10.0, 10.0);
			painter.translate(30.0, 70.0);
			paintCourseDrawing(&painter, &m.map, drawing, QRectF(-30, -70, 180, 120), 10.0);
		}
		auto const ink = [&image](double x, double y_up) {
			auto const colour = QColor(image.pixel(qRound((x + 30) * 10), qRound((-y_up + 70) * 10)));
			return colour != QColor(Qt::white);
		};
		// The middle of the leg from 31 to 32, and of that from the route to 34.
		QVERIFY(ink(30, 0));
		QVERIFY(ink(55, 13));
		// No line between 32 and 33, which are too near, and none of the course N.
		QVERIFY(!ink(90, 0));
		QVERIFY(!ink(110, -20));
		// A number stands in its box.
		auto const* number = findNumber(drawing, "31");
		auto found = false;
		for (auto x = number->box.left(); x <= number->box.right() && !found; x += 0.1)
		{
			for (auto y = number->box.top(); y <= number->box.bottom() && !found; y += 0.1)
				found = ink(x, -y);
		}
		QVERIFY(found);
	}
}


QTEST_MAIN(CourseRenderTest)
#include "course_render_t.moc"  // IWYU pragma: keep
