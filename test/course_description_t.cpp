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
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

using namespace OpenOrienteering;

using Row = CourseDescriptionRow;
using Layout = CourseDescriptionLayout;


namespace {

/// A place given with the y axis up.
MapCoord at(double x, double y_up)
{
	return MapCoord(x, -y_up);
}

/// A map of the scale 1:10,000 with course objects and courses for the rows.
struct DescriptionMap
{
	Map map;
	MapColor* black;
	MapColor* white;
	PointSymbol* point_symbol;
	LineSymbol* route_symbol;
	CourseData data;
	
	DescriptionMap()
	{
		map.setScaleDenominator(10000);
		black = new MapColor(QStringLiteral("Black"), 0);
		map.addColor(black, 0);
		white = new MapColor(QStringLiteral("White"), 1);
		white->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 0.0f));
		map.addColor(white, 1);
		
		point_symbol = new PointSymbol();
		point_symbol->setNumberComponent(0, 703);
		point_symbol->setInnerRadius(2825);
		point_symbol->setOuterWidth(350);
		point_symbol->setOuterColor(black);
		map.addSymbol(point_symbol, map.getNumSymbols());
		
		route_symbol = new LineSymbol();
		route_symbol->setNumberComponent(0, 707);
		route_symbol->setLineWidth(0.35);
		route_symbol->setColor(black);
		map.addSymbol(route_symbol, map.getNumSymbols());
		
		// Two description symbols with their names.
		pictogram(1, 10, "Knoll");
		pictogram(12, 102, "West Side");
		
		route("M0", { at(-13, 0), at(-3, 0) });
		point(CourseObject::Start, "S1", 0, 0);
		point(CourseObject::Control, "31", 20, 0);
		point(CourseObject::Control, "32", 40, 0);
		point(CourseObject::Control, "33", 60, 0);
		route("M1", { at(62, 0), at(67, 0) });
		point(CourseObject::Control, "34", 80, 0);
		point(CourseObject::Control, "35", 100, 0);
		route("M2", { at(100, 4), at(100, 10) });
		point(CourseObject::Finish, "F1", 100, 17);
		point(CourseObject::Finish, "F2", 20, 21.24);
		point(CourseObject::Finish, "F3", 100, 34.69);
		route("M3", { at(42, 0), at(47, 0) });
		point(CourseObject::Start, "S2", 50, 0);
		
		CourseObject block;
		block.kind = CourseObject::TextBlock;
		block.code = QStringLiteral("T1");
		block.text = QStringLiteral("Mind the road");
		data.objects.push_back(block);
		
		find("S1").cell[CourseObject::ColumnD] = 1010;
		find("31").text = QStringLiteral("Knoll by hand");
		auto& second = find("32");
		second.cell[CourseObject::ColumnD] = 1010;
		second.cell[CourseObject::ColumnG] = 12102;
		second.cell[CourseObject::ColumnH] = 13002;  // the map has no such symbol
		second.other.push_back({ QLatin1Char('s'), QStringLiteral("1.5") });
		
		course("A", { "mM0", "sS1", "c31", "c32", "w", "c33", "mM1", "k", "c34", "tT1", "c35", "mM2", "fF1" });
		course("B", { "sS1", "c31", "c32", "c31", "fF2" });
		course("C", { "sS1", "c35", "fF3" });
		course("X", { "sS1", "c31", "c32", "mM3", "g", "sS2", "c33", "g", "sS1", "c34", "x", "sS2", "c35", "fF3" });
		course("V", { "sS1", "r", "c31", "fF1" });
	}
	
	PointSymbol* pictogram(int first, int second, const char* name)
	{
		auto* symbol = new PointSymbol();
		symbol->setNumberComponent(0, first);
		symbol->setNumberComponent(1, second);
		symbol->setName(QString::fromLatin1(name));
		symbol->setInnerRadius(1000);
		symbol->setInnerColor(black);
		symbol->setCourseRole(Symbol::DescriptionSymbol, Symbol::DescriptionColumnD);
		map.addSymbol(symbol, map.getNumSymbols());
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
	
	void point(CourseObject::Kind kind, const char* code, double x, double y_up)
	{
		auto* object = new PointObject(point_symbol);
		object->setPosition(at(x, y_up));
		add(kind, code, object);
	}
	
	void route(const char* code, std::initializer_list<MapCoord> coords)
	{
		add(CourseObject::MarkedRoute, code, new PathObject(route_symbol, MapCoordVector(coords)));
	}
	
	CourseObject& find(const char* code)
	{
		for (auto& object : data.objects)
		{
			if (object.code == QLatin1String(code))
				return object;
		}
		Q_UNREACHABLE();
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
			case 't': item.type = CourseItem::TextBlock; break;
			case 'g': item.type = CourseItem::MapExchange; break;
			case 'x': item.type = CourseItem::MapFlip; break;
			case 'k': item.type = CourseItem::CrossingPoint; break;
			case 'w': item.type = CourseItem::Passage; break;
			case 'r': item.type = CourseItem::TeamForkBegin; break;
			default: Q_UNREACHABLE();
			}
			item.code = QString::fromLatin1(text + 1);
			result.items.push_back(item);
		}
		data.courses.push_back(result);
	}
	
	Course& findCourse(const char* name)
	{
		for (auto& entry : data.courses)
		{
			if (entry.name == QLatin1String(name))
				return entry;
		}
		Q_UNREACHABLE();
	}
	
	/// Adds a description object at a place, and when asked the symbols of rules, texts and background.
	void addDescription(double x, double y_up, bool in_text)
	{
		point(CourseObject::Description, in_text ? "D2" : "D1", x, y_up);
		if (in_text)
			data.objects.back().other.push_back({ QLatin1Char('o'), QStringLiteral("t") });
	}
	
	void addBlockSymbols()
	{
		line(18, 1, 0.5);
		line(18, 2, 0.25);
		for (auto second : { 1, 2, 3, 4, 5, 6, 7 })
		{
			auto* symbol = new TextSymbol();
			symbol->setNumberComponent(0, 19);
			symbol->setNumberComponent(1, second);
			symbol->setFontFamily(QStringLiteral("Arial"));
			symbol->setFontSize(3500);
			symbol->setColor(black);
			map.addSymbol(symbol, map.getNumSymbols());
		}
		auto* area = new AreaSymbol();
		area->setNumberComponent(0, 20);
		area->setNumberComponent(1, 1);
		area->setColor(white);
		map.addSymbol(area, map.getNumSymbols());
		pictogram(17, 1, "Start");
	}
	
	void line(int first, int second, double width)
	{
		auto* symbol = new LineSymbol();
		symbol->setNumberComponent(0, first);
		symbol->setNumberComponent(1, second);
		symbol->setLineWidth(width);
		symbol->setColor(black);
		map.addSymbol(symbol, map.getNumSymbols());
	}
};


Row makeRow(Row::Kind kind, int number = 0, const char* code = "")
{
	Row row;
	row.kind = kind;
	row.number = number;
	row.code = QString::fromLatin1(code);
	return row;
}

/// A description of a start, some controls and a finish without tapes.
CourseDescription plainDescription(int controls)
{
	CourseDescription description;
	description.known = true;
	description.title = QStringLiteral("Event");
	description.classes = QStringList{ QStringLiteral("M12"), QStringLiteral("W12") };
	description.course = QStringLiteral("Easy");
	description.length = QStringLiteral("1.5 km");
	description.climb = QStringLiteral("10 m");
	description.parts = 1;
	description.rows.push_back(makeRow(Row::Start, 0, "S1"));
	for (int i = 1; i <= controls; ++i)
		description.rows.push_back(makeRow(Row::Control, i, qPrintable(QString::number(30 + i))));
	auto finish = makeRow(Row::Finish, 0, "F1");
	finish.distance = 210;
	description.rows.push_back(finish);
	return description;
}

/// The options of a forest course.
CourseOptions forestOptions()
{
	CourseOptions options;
	options.setValue(QLatin1Char('t'), QStringLiteral("1"));
	options.setValue(QLatin1Char('h'), QStringLiteral("1"));
	options.setValue(QLatin1Char('i'), QStringLiteral("25"));
	options.setValue(QLatin1Char('s'), QStringLiteral("6.00"));
	options.setValue(QLatin1Char('b'), QStringLiteral("1"));
	options.setValue(QLatin1Char('e'), QStringLiteral("Event"));
	return options;
}

bool nearPoint(const QPointF& actual, double x, double y, double tolerance = 0.001)
{
	return std::abs(actual.x() - x) <= tolerance && std::abs(actual.y() - y) <= tolerance;
}

QString show(const QPointF& point)
{
	return QStringLiteral("(%1, %2)").arg(point.x(), 0, 'f', 3).arg(point.y(), 0, 'f', 3);
}

bool hasPictogram(const Layout& layout, int symbol, double x, double y)
{
	return std::any_of(begin(layout.pictograms), end(layout.pictograms), [&](const Layout::Pictogram& pictogram) {
		return pictogram.symbol == symbol && nearPoint(pictogram.centre, x, y);
	});
}

int countPictograms(const Layout& layout, int symbol)
{
	return int(std::count_if(begin(layout.pictograms), end(layout.pictograms), [&](const Layout::Pictogram& pictogram) {
		return pictogram.symbol == symbol;
	}));
}

const Layout::Text* findText(const Layout& layout, Layout::Text::Style style, const char* text)
{
	for (auto const& entry : layout.texts)
	{
		if (entry.style == style && entry.text == QString::fromLatin1(text))
			return &entry;
	}
	return nullptr;
}

bool hasText(const Layout& layout, Layout::Text::Style style, const char* text, double x, double y)
{
	auto const* found = findText(layout, style, text);
	return found && nearPoint(found->anchor, x, y);
}

/// The vertical rules of a row as a text: "T0.00 t6.00 ...".
QString verticalRules(const Layout& layout, const Layout::Row& row)
{
	std::vector<std::pair<double, bool>> found;
	for (auto const& rule : layout.rules)
	{
		if (std::abs(rule.begin.x() - rule.end.x()) < 0.0005
		    && std::abs(std::min(rule.begin.y(), rule.end.y()) - row.rect.top()) < 0.0005
		    && std::abs(std::max(rule.begin.y(), rule.end.y()) - row.rect.bottom()) < 0.0005
		    && rule.begin.x() >= row.rect.left() - 0.0005 && rule.begin.x() <= row.rect.right() + 0.0005)
			found.emplace_back(rule.begin.x() - row.rect.left(), rule.thick);
	}
	std::sort(begin(found), end(found));
	QStringList texts;
	for (auto const& entry : found)
		texts.push_back(QLatin1String(entry.second ? "T" : "t") + QString::number(entry.first, 'f', 2));
	return texts.join(QLatin1Char(' '));
}

/// The weights of the rules above the rows of the description (not of the head), as "TTttT".
QString ruleWeights(const Layout& layout)
{
	QString weights;
	for (auto const& row : layout.rows)
	{
		if (row.index >= 0)
			weights += QLatin1Char(row.thick_above ? 'T' : 't');
	}
	return weights;
}

}  // namespace



/**
 * @test Tests the description of a course: its rows, its texts and its block.
 *
 * See the design notes, "The description of a course".
 */
class CourseDescriptionTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void rowTest();
	void partTest();
	void headTest();
	void textTest();
	void blockTest();
	void smallBlockTest();
	void ruleTest();
	void headLayoutTest();
	void secondBlockTest();
	void pictureTest();
	void plainFitTest();
};


void CourseDescriptionTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseDescriptionTest::rowTest()
{
	DescriptionMap m;
	auto description = describeCourse(m.map, m.data, QStringLiteral("A"));
	QVERIFY(description.known);
	QCOMPARE(description.parts, 1);
	
	const Row::Kind kinds[] = {
	    Row::RouteToStart, Row::Start, Row::Control, Row::Control, Row::Passage, Row::Control,
	    Row::Route, Row::CrossingPoint, Row::Control, Row::TextBlock, Row::Control, Row::Finish
	};
	QCOMPARE(description.rows.size(), std::size_t(12));
	for (std::size_t i = 0; i < description.rows.size(); ++i)
	{
		QVERIFY2(description.rows[i].kind == kinds[i], qPrintable(QString::number(i)));
		QCOMPARE(description.rows[i].part, 0);
	}
	
	// The path of 100 m and the straight of 30 m to the start.
	QCOMPARE(description.rows[0].distance, 130);
	QCOMPARE(description.rows[0].code, QStringLiteral("M0"));
	QCOMPARE(description.rows[1].code, QStringLiteral("S1"));
	QCOMPARE(description.rows[1].cell[CourseObject::ColumnD], 1010);
	QCOMPARE(description.rows[1].distance, -1);
	QCOMPARE(description.rows[2].number, 1);
	QCOMPARE(description.rows[2].code, QStringLiteral("31"));
	QCOMPARE(description.rows[3].number, 2);
	QCOMPARE(description.rows[3].cell[CourseObject::ColumnG], 12102);
	QCOMPARE(description.rows[3].size, QStringLiteral("1.5"));
	QCOMPARE(description.rows[5].number, 3);
	// The straight of 20 m and the path of 50 m.
	QCOMPARE(description.rows[6].distance, 70);
	QCOMPARE(description.rows[6].code, QStringLiteral("M1"));
	QCOMPARE(description.rows[8].number, 4);
	QCOMPARE(description.rows[9].text, QStringLiteral("Mind the road"));
	QCOMPARE(description.rows[10].number, 5);
	// 40 m, the path of 60 m and 70 m.
	QCOMPARE(description.rows[11].distance, 170);
	QVERIFY(description.rows[11].tapes == Row::Taped);
	QCOMPARE(description.rows[11].code, QStringLiteral("F1"));
	
	m.find("M2").other.push_back({ QLatin1Char('m'), QStringLiteral("f") });
	description = describeCourse(m.map, m.data, QStringLiteral("A"));
	QVERIFY(description.rows[11].tapes == Row::Funnel);
	QCOMPARE(description.rows[11].distance, 170);
	
	// Without a marked route; a control which is visited twice.
	description = describeCourse(m.map, m.data, QStringLiteral("B"));
	QVERIFY(description.known);
	QCOMPARE(description.rows.size(), std::size_t(5));
	QCOMPARE(description.rows[1].number, 1);
	QCOMPARE(description.rows[1].code, QStringLiteral("31"));
	QCOMPARE(description.rows[3].number, 3);
	QCOMPARE(description.rows[3].code, QStringLiteral("31"));
	QVERIFY(description.rows[4].kind == Row::Finish);
	QVERIFY(description.rows[4].tapes == Row::NoTapes);
	QCOMPARE(description.rows[4].distance, 210);
	
	description = describeCourse(m.map, m.data, QStringLiteral("C"));
	QCOMPARE(description.rows.back().distance, 350);
	
	QVERIFY(!describeCourse(m.map, m.data, QStringLiteral("no such course")).known);
}


/// Accept (2)
void CourseDescriptionTest::partTest()
{
	DescriptionMap m;
	auto const description = describeCourse(m.map, m.data, QStringLiteral("X"));
	QVERIFY(description.known);
	QCOMPARE(description.parts, 4);
	
	struct Expected { Row::Kind kind; int part; int number; int distance; };
	const Expected expected[] = {
	    { Row::Start, 0, 0, -1 },
	    { Row::Control, 0, 1, -1 },
	    { Row::Control, 0, 2, -1 },
	    { Row::MapExchange, 0, 0, 70 },
	    { Row::Start, 1, 0, -1 },
	    { Row::Control, 1, 3, -1 },
	    { Row::MapExchange, 1, 0, 0 },
	    { Row::Start, 2, 0, -1 },
	    { Row::Control, 2, 4, -1 },
	    { Row::MapFlip, 2, 0, -1 },
	    { Row::Start, 3, 0, -1 },
	    { Row::Control, 3, 5, -1 },
	    { Row::Finish, 3, 0, 350 },
	};
	QCOMPARE(description.rows.size(), std::size_t(13));
	for (std::size_t i = 0; i < description.rows.size(); ++i)
	{
		auto const& row = description.rows[i];
		auto const where = QString::number(i).toLatin1();
		QVERIFY2(row.kind == expected[i].kind, where);
		QVERIFY2(row.part == expected[i].part, where);
		QVERIFY2(row.number == expected[i].number, where);
		QVERIFY2(row.distance == expected[i].distance, where);
	}
	QVERIFY(description.rows[3].tapes == Row::Taped);
	QVERIFY(description.rows[6].tapes == Row::NoTapes);
	QCOMPARE(description.rows[4].code, QStringLiteral("S2"));
	
	// A part alone, in text and as a block.
	auto const lines = courseDescriptionLines(description, 1);
	QCOMPARE(lines.size(), 3 + 3);
	auto const layout = layoutCourseDescription(description, forestOptions(), 1);
	QCOMPARE(int(layout.rows.size()), 3 + 3);
	QCOMPARE(layout.rows.back().index, 6);
	QCOMPARE(int(layoutCourseDescription(description, forestOptions()).rows.size()), 3 + 13);
	
	QVERIFY(!describeCourse(m.map, m.data, QStringLiteral("V")).known);
	auto const drawing = drawCourseDescriptions(m.map, m.data, QStringLiteral("V"));
	QVERIFY(!drawing.drawn);
}


/// Accept (3)
void CourseDescriptionTest::headTest()
{
	DescriptionMap m;
	m.data.options.setValue(QLatin1Char('e'), QStringLiteral("Evening Training"));
	for (auto const* name : { "W12", "ME", "M12" })
	{
		CourseClass entry;
		entry.name = QString::fromLatin1(name);
		entry.course = QLatin1String(name[1] == 'E' ? "C" : "B");
		m.data.classes.push_back(entry);
	}
	
	// B measures 200 + 200 + 200 + 212.4 m.
	auto& course = m.findCourse("B");
	course.extra_km = 0.684;
	course.climb_m = 10;
	auto description = describeCourse(m.map, m.data, QStringLiteral("B"));
	QCOMPARE(description.title, QStringLiteral("Evening Training"));
	QCOMPARE(description.classes, (QStringList{ QStringLiteral("W12"), QStringLiteral("M12") }));
	QCOMPARE(description.course, QStringLiteral("B"));
	QCOMPARE(description.length, QStringLiteral("1.5 km"));
	QCOMPARE(description.climb, QStringLiteral("10 m"));
	
	course.extra_km = 1.8152;
	course.climb_m = 0;
	description = describeCourse(m.map, m.data, QStringLiteral("B"));
	QCOMPARE(description.length, QStringLiteral("2.6 km"));
	QCOMPARE(description.climb, QString());
	
	description = describeCourse(m.map, m.data, QStringLiteral("A"));
	QVERIFY(description.classes.isEmpty());
	
	// An item whose object is gone: no length.
	m.map.deleteObject(m.map.findObject(m.find("33").object));
	description = describeCourse(m.map, m.data, QStringLiteral("A"));
	QVERIFY(description.known);
	QCOMPARE(description.length, QString());
}


/// Accept (4)
void CourseDescriptionTest::textTest()
{
	DescriptionMap m;
	QCOMPARE(courseObjectText(m.map, m.find("31")), QStringLiteral("Knoll by hand"));
	QCOMPARE(courseObjectText(m.map, m.find("32")), QStringLiteral("Knoll, 1.5m, West Side"));
	QCOMPARE(courseObjectText(m.map, m.find("33")), QString());
	QCOMPARE(courseObjectField(m.find("32"), QLatin1Char('s')), QStringLiteral("1.5"));
	QCOMPARE(courseObjectField(m.find("32"), QLatin1Char('m')), QString());
	
	m.data.options.setValue(QLatin1Char('e'), QStringLiteral("Evening Training"));
	m.findCourse("A").climb_m = 30;
	CourseClass entry;
	entry.name = QStringLiteral("M12");
	entry.course = QStringLiteral("A");
	m.data.classes.push_back(entry);
	entry.name = QStringLiteral("W12");
	m.data.classes.push_back(entry);
	
	auto description = describeCourse(m.map, m.data, QStringLiteral("A"));
	auto lines = courseDescriptionLines(description);
	// The course measures 100 + 200 + 200 + 200 + 20 + 50 + 130 + 200 + 40 + 60 + 70 m.
	const char* expected[] = {
	    "Evening Training",
	    "Class M12, W12",
	    "Course A, Length 1.3 km, Climb 30 m",
	    "Follow taped route 130 m to start",
	    "Start\tKnoll",
	    "1.\t31\tKnoll by hand",
	    "2.\t32\tKnoll, 1.5m, West Side",
	    "Mandatory passage through out of bounds area",
	    "3.\t33\t",
	    "Follow taped route 70 m away from control",
	    "Mandatory crossing point or points",
	    "4.\t34\t",
	    "Mind the road",
	    "5.\t35\t",
	    "170 m from last control to finish. Follow taped route",
	};
	QCOMPARE(lines.size(), int(sizeof(expected) / sizeof(expected[0])));
	for (int i = 0; i < lines.size(); ++i)
		QCOMPARE(lines[i], QString::fromLatin1(expected[i]));
	
	m.find("M2").other.push_back({ QLatin1Char('m'), QStringLiteral("f") });
	description = describeCourse(m.map, m.data, QStringLiteral("A"));
	QCOMPARE(courseDescriptionLines(description).last(),
	         QStringLiteral("170 m from last control to finish. Navigate to finish funnel, then follow tapes"));
	
	// The own text of a marked route, the semicolon standing for the distance.
	m.find("M2").text = QStringLiteral("; to Finish");
	m.find("M1").text = QStringLiteral("Along the fence");
	description = describeCourse(m.map, m.data, QStringLiteral("A"));
	lines = courseDescriptionLines(description);
	QCOMPARE(lines.last(), QStringLiteral("170 to Finish"));
	QCOMPARE(lines[9], QStringLiteral("Along the fence"));
	
	description = describeCourse(m.map, m.data, QStringLiteral("B"));
	lines = courseDescriptionLines(description);
	QCOMPARE(lines[1], QStringLiteral("Class B"));
	QCOMPARE(lines[2], QStringLiteral("Course B, Length 0.8 km"));
	QCOMPARE(lines.last(), QStringLiteral("210 m from last control to finish. Navigate to finish, no tapes"));
	
	description = describeCourse(m.map, m.data, QStringLiteral("X"));
	lines = courseDescriptionLines(description);
	QCOMPARE(lines[3 + 3], QStringLiteral("Follow taped route 70 m to map exchange"));
	QCOMPARE(lines[3 + 6], QStringLiteral("Map exchange"));
	QCOMPARE(lines[3 + 9], QStringLiteral("Map flip"));
}


/// Accept (5)
void CourseDescriptionTest::blockTest()
{
	auto description = plainDescription(5);
	description.rows[0].cell[CourseObject::ColumnD] = 5001;
	auto& first = description.rows[1];
	first.cell = { 101, 5001, 5001, 0, 12401, 13002 };
	first.size = QStringLiteral("1.5");
	description.rows[2].cell[CourseObject::ColumnF] = 10002;
	
	auto const layout = layoutCourseDescription(description, forestOptions());
	QVERIFY(!layout.in_text);
	QCOMPARE(layout.box, 6.0);
	QCOMPARE(layout.scale, 1.0);
	QCOMPARE(layout.blocks.size(), std::size_t(1));
	QCOMPARE(layout.blocks[0], QRectF(0, 0, 51, 60));
	QCOMPARE(layout.backgrounds.size(), std::size_t(1));
	QCOMPARE(layout.backgrounds[0], QRectF(-2, -2, 55, 64));
	QCOMPARE(layout.rows.size(), std::size_t(10));
	for (std::size_t i = 0; i < layout.rows.size(); ++i)
	{
		QCOMPARE(layout.rows[i].rect, QRectF(0, 6.0 * i, 51, 6));
		QCOMPARE(layout.rows[i].index, i < 3 ? -1 : int(i) - 3);
		QCOMPARE(layout.rows[i].block, 0);
	}
	
	QCOMPARE(verticalRules(layout, layout.rows[0]), QStringLiteral("T0.00 T51.00"));
	QCOMPARE(verticalRules(layout, layout.rows[1]), QStringLiteral("T0.00 T51.00"));
	QCOMPARE(verticalRules(layout, layout.rows[2]), QStringLiteral("T0.00 T19.20 T39.00 T51.00"));
	auto const columns = QStringLiteral("T0.00 t6.00 t13.20 T19.20 t25.20 t31.20 T39.00 t45.00 T51.00");
	QCOMPARE(verticalRules(layout, layout.rows[3]), columns);
	QCOMPARE(verticalRules(layout, layout.rows[4]), columns);
	QCOMPARE(verticalRules(layout, layout.rows[9]), QStringLiteral("T0.00 T51.00"));
	// A rule above every row and one below the block; 2, 2, 4, six times 9 and 2 vertical ones.
	QCOMPARE(layout.rules.size(), std::size_t(11 + 64));
	
	// The head.
	QVERIFY(hasText(layout, Layout::Text::Title, "Event", 25.2, 4.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "M12, W12", 25.2, 10.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "Easy", 9.6, 16.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "1.5 km", 29.1, 16.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "10 m", 45.0, 16.2));
	
	// The start.
	QVERIFY(hasPictogram(layout, 17001, 3, 21));
	QVERIFY(hasPictogram(layout, 5001, 22.2, 21));
	
	// The first control.
	QVERIFY(hasText(layout, Layout::Text::Number, "1", 3, 28.5));
	QVERIFY(hasText(layout, Layout::Text::Code, "31", 9.6, 28.5));
	QVERIFY(hasPictogram(layout, 101, 16.2, 27));
	QVERIFY(hasPictogram(layout, 5001, 22.2, 27));
	QVERIFY(hasPictogram(layout, 5001, 28.2, 27));
	QVERIFY(hasText(layout, Layout::Text::Size, "1.5", 35.1, 28.35));
	QVERIFY(hasPictogram(layout, 12401, 42, 27));
	QVERIFY(hasPictogram(layout, 13002, 48, 27));
	// The second.
	QVERIFY(hasText(layout, Layout::Text::Number, "2", 3, 34.5));
	QVERIFY(hasPictogram(layout, 10002, 35.1, 33));
	
	// A size of two numbers: the first above a slash, the second below it. The third control is the row from 36 mm.
	{
		auto sloped = description;
		sloped.rows[3].size = QStringLiteral("5/10");
		auto const with_slash = layoutCourseDescription(sloped, forestOptions());
		QVERIFY(hasText(with_slash, Layout::Text::Size, "5", 33.45, 38.4));
		QVERIFY(hasText(with_slash, Layout::Text::Size, "10", 36.72, 41.4));
		QVERIFY(!findText(with_slash, Layout::Text::Size, "5/10"));
		QCOMPARE(with_slash.rules.size(), layout.rules.size() + 1);
		auto const found = std::find_if(begin(with_slash.rules), end(with_slash.rules), [](const Layout::Rule& rule) {
			return std::abs(rule.begin.x() - rule.end.x()) > 0.001 && std::abs(rule.begin.y() - rule.end.y()) > 0.001;
		});
		QVERIFY(found != end(with_slash.rules));
		auto const& slash = *found;
		QVERIFY(!slash.thick);
		QVERIFY2(nearPoint(slash.begin, 31.8, 40.5), qPrintable(show(slash.begin)));
		QVERIFY2(nearPoint(slash.end, 37.8, 37.56), qPrintable(show(slash.end)));
	}
	
	// The finish.
	QVERIFY(hasPictogram(layout, 17002, 3, 57));
	QVERIFY(hasPictogram(layout, 16003, 6.6, 57));
	QVERIFY(hasPictogram(layout, 17004, 44.4, 57));
	QVERIFY(hasPictogram(layout, 17003, 48, 57));
	QVERIFY(hasText(layout, Layout::Text::Distance, "210 m", 25.2, 58.5));
	QCOMPARE(countPictograms(layout, 16001), 0);
	
	// Without the option: no background.
	auto options = forestOptions();
	options.setValue(QLatin1Char('b'), QStringLiteral("0"));
	QVERIFY(layoutCourseDescription(description, options).backgrounds.empty());
	
	// The other rows.
	description.rows.insert(begin(description.rows) + 3, makeRow(Row::Passage));
	description.rows.insert(begin(description.rows) + 4, makeRow(Row::CrossingPoint));
	description.rows.insert(begin(description.rows) + 5, makeRow(Row::MapFlip));
	auto block = makeRow(Row::TextBlock, 0, "T1");
	block.text = QStringLiteral("Mind the road");
	description.rows.insert(begin(description.rows) + 6, block);
	auto const other = layoutCourseDescription(description, forestOptions());
	// The passage is the row from 36 to 42 mm.
	QVERIFY(hasPictogram(other, 17002, 3, 39));
	QVERIFY(hasPictogram(other, 16003, 6.6, 39));
	QVERIFY(hasPictogram(other, 15004, 25.2, 39));
	QVERIFY(hasPictogram(other, 17004, 44.4, 39));
	QVERIFY(hasPictogram(other, 17002, 48, 39));
	QVERIFY(hasPictogram(other, 15003, 25.2, 45));
	QVERIFY(hasPictogram(other, 15006, 25.2, 51));
	QCOMPARE(countPictograms(other, 15006), 1);
	QVERIFY(hasText(other, Layout::Text::Distance, "Mind the road", 25.2, 58.2));
	QCOMPARE(verticalRules(other, other.rows[6]), QStringLiteral("T0.00 T51.00"));
	QCOMPARE(verticalRules(other, other.rows[9]), QStringLiteral("T0.00 T51.00"));
}


/// Accept (6)
void CourseDescriptionTest::smallBlockTest()
{
	auto description = plainDescription(5);
	auto& finish = description.rows.back();
	finish.tapes = Row::Taped;
	finish.distance = 50;
	auto route = makeRow(Row::Route, 0, "M1");
	route.distance = 80;
	route.tapes = Row::Taped;
	description.rows.insert(begin(description.rows) + 3, route);
	auto to_start = makeRow(Row::RouteToStart, 0, "M0");
	to_start.distance = 130;
	description.rows.insert(begin(description.rows), to_start);
	auto exchange = makeRow(Row::MapExchange);
	exchange.distance = 0;
	description.rows.insert(end(description.rows) - 1, exchange);
	
	auto options = forestOptions();
	options.setValue(QLatin1Char('s'), QStringLiteral("5.00"));
	auto layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.box, 5.0);
	QVERIFY(std::abs(layout.scale - 5.0 / 6.0) < 1e-9);
	// Three rows of the head and ten others.
	QCOMPARE(layout.blocks[0], QRectF(0, 0, 42.5, 65));
	QCOMPARE(verticalRules(layout, layout.rows[4]), QStringLiteral("T0.00 t5.00 t11.00 T16.00 t21.00 t26.00 T32.50 t37.50 T42.50"));
	QVERIFY(std::abs(layout.backgrounds[0].left() + 5.0 / 3.0) < 1e-9);
	
	// The route to the start is the row from 15 to 20 mm: six dashes, the arrow and the start of a marked route.
	for (auto x : { 5.8, 9.2, 12.7, 29.7, 33.2, 36.7 })
		QVERIFY2(hasPictogram(layout, 16001, x, 17.5), qPrintable(QString::number(x)));
	QVERIFY(hasPictogram(layout, 17004, 37.25, 17.5));
	QVERIFY(hasPictogram(layout, 17005, 40, 17.5));
	QVERIFY(!hasPictogram(layout, 17002, 2.5, 17.5));
	QVERIFY(hasText(layout, Layout::Text::Distance, "130 m", 21, 18.75));
	// The start from 20, two controls from 25, the route between controls from 35 mm.
	QVERIFY(hasPictogram(layout, 17001, 2.5, 22.5));
	QVERIFY(hasText(layout, Layout::Text::Number, "1", 2.5, 28.75));
	QVERIFY(hasText(layout, Layout::Text::Code, "31", 8, 28.75));
	QVERIFY(hasPictogram(layout, 17002, 2.5, 37.5));
	QVERIFY(hasPictogram(layout, 16001, 5.8, 37.5));
	QVERIFY(hasPictogram(layout, 17004, 37.25, 37.5));
	QVERIFY(!hasPictogram(layout, 17003, 40, 37.5));
	QVERIFY(!hasPictogram(layout, 17005, 40, 37.5));
	QVERIFY(hasText(layout, Layout::Text::Distance, "80 m", 21, 38.75));
	// Three controls from 40, the map exchange from 55 mm: as a taped finish with the start of a marked route.
	QVERIFY(hasPictogram(layout, 17002, 2.5, 57.5));
	QVERIFY(hasPictogram(layout, 16001, 5.8, 57.5));
	QVERIFY(hasPictogram(layout, 17005, 40, 57.5));
	QVERIFY(hasText(layout, Layout::Text::Distance, "0 m", 21, 58.75));
	// The taped finish from 60 mm.
	QVERIFY(hasPictogram(layout, 17002, 2.5, 62.5));
	QVERIFY(hasPictogram(layout, 16001, 5.8, 62.5));
	QVERIFY(hasPictogram(layout, 16001, 36.7, 62.5));
	QVERIFY(hasPictogram(layout, 17004, 37.25, 62.5));
	QVERIFY(hasPictogram(layout, 17003, 40, 62.5));
	QVERIFY(hasText(layout, Layout::Text::Distance, "50 m", 21, 63.75));
	QCOMPARE(countPictograms(layout, 16003), 0);
	
	// The funnel takes the place of the first dash.
	description.rows.back().tapes = Row::Funnel;
	layout = layoutCourseDescription(description, options);
	QVERIFY(hasPictogram(layout, 16002, 7.2, 62.5));
	QVERIFY(!hasPictogram(layout, 16001, 5.8, 62.5));
	QVERIFY(hasPictogram(layout, 16001, 9.2, 62.5));
	QVERIFY(hasPictogram(layout, 16001, 12.7, 62.5));
	QVERIFY(hasPictogram(layout, 16001, 29.7, 62.5));
}


/// Accept (7)
void CourseDescriptionTest::ruleTest()
{
	auto description = plainDescription(8);
	auto options = forestOptions();
	// The start, eight controls, the finish.
	QCOMPARE(ruleWeights(layoutCourseDescription(description, options)), QStringLiteral("TTttTttTtT"));
	options.setValue(QLatin1Char('h'), QStringLiteral("2"));
	QCOMPARE(ruleWeights(layoutCourseDescription(description, options)), QStringLiteral("TTtttTtttT"));
	options.setValue(QLatin1Char('h'), QStringLiteral("0"));
	QCOMPARE(ruleWeights(layoutCourseDescription(description, options)), QStringLiteral("TTtttttttT"));
	
	// A crossing point behind the second control: the count begins anew.
	description.rows.insert(begin(description.rows) + 3, makeRow(Row::CrossingPoint));
	options.setValue(QLatin1Char('h'), QStringLiteral("1"));
	QCOMPARE(ruleWeights(layoutCourseDescription(description, options)), QStringLiteral("TTtTTttTttT"));
	
	// The weights are those of the rules.
	auto const layout = layoutCourseDescription(description, options);
	for (auto const& row : layout.rows)
	{
		auto found = 0;
		for (auto const& rule : layout.rules)
		{
			if (std::abs(rule.begin.y() - row.rect.top()) < 0.0005 && std::abs(rule.end.y() - row.rect.top()) < 0.0005)
			{
				++found;
				QCOMPARE(rule.thick, row.thick_above);
				QCOMPARE(std::min(rule.begin.x(), rule.end.x()), 0.0);
				QCOMPARE(std::max(rule.begin.x(), rule.end.x()), 51.0);
			}
		}
		QCOMPARE(found, 1);
	}
}


/// Accept (8)
void CourseDescriptionTest::headLayoutTest()
{
	auto description = plainDescription(1);
	auto options = forestOptions();
	
	auto layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.rows[0].rect.height(), 6.0);
	QCOMPARE(layout.rows[1].rect.height(), 6.0);
	QCOMPARE(layout.rows[2].rect.height(), 6.0);
	QCOMPARE(layout.rows[3].index, 0);
	
	description.classes.clear();
	layout = layoutCourseDescription(description, options);
	QVERIFY(std::abs(layout.rows[1].rect.height() - 2.4) < 1e-9);
	QVERIFY(std::abs(layout.rows[2].rect.top() - 8.4) < 1e-9);
	QVERIFY(hasText(layout, Layout::Text::Title, "Easy", 9.6, 12.6));
	QCOMPARE(layout.rows[3].index, 0);
	
	// The classes in the first cell, one below the other.
	options.setValue(QLatin1Char('t'), QStringLiteral("0"));
	description.classes = QStringList{ QStringLiteral("WE"), QStringLiteral("ME") };
	layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.rows[0].rect.height(), 6.0);
	QCOMPARE(layout.rows[1].rect, QRectF(0, 6, 51, 9));
	QCOMPARE(layout.rows[2].index, 0);
	QCOMPARE(verticalRules(layout, layout.rows[1]), QStringLiteral("T0.00 T19.20 T39.00 T51.00"));
	QVERIFY(hasText(layout, Layout::Text::Title, "WE", 9.6, 6 + 4.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "ME", 9.6, 6 + 7.2));
	QVERIFY(hasText(layout, Layout::Text::Title, "1.5 km", 29.1, 6 + 5.7));
	QVERIFY(hasText(layout, Layout::Text::Title, "10 m", 45.0, 6 + 5.7));
	QVERIFY(!findText(layout, Layout::Text::Title, "Easy"));
	
	description.classes.clear();
	layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.rows[1].rect, QRectF(0, 6, 51, 6));
	QVERIFY(hasText(layout, Layout::Text::Title, "Easy", 9.6, 10.2));
	
	// The title and the name.
	options.setValue(QLatin1Char('t'), QStringLiteral("2"));
	description.classes = QStringList{ QStringLiteral("WE"), QStringLiteral("ME") };
	layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.rows[1].rect, QRectF(0, 6, 51, 6));
	QCOMPARE(layout.rows[2].index, 0);
	QVERIFY(hasText(layout, Layout::Text::Title, "Easy", 9.6, 10.2));
	QVERIFY(!findText(layout, Layout::Text::Title, "WE"));
}


/// Accept (9)
void CourseDescriptionTest::secondBlockTest()
{
	auto description = plainDescription(14);
	description.classes = QStringList{ QStringLiteral("WE"), QStringLiteral("ME") };
	auto options = forestOptions();
	options.setValue(QLatin1Char('t'), QStringLiteral("0"));
	options.setValue(QLatin1Char('s'), QStringLiteral("5.00"));
	options.setValue(QLatin1Char('i'), QStringLiteral("14"));
	
	auto layout = layoutCourseDescription(description, options);
	QCOMPARE(layout.blocks.size(), std::size_t(2));
	QCOMPARE(layout.blocks[0], QRectF(0, 0, 42.5, 67.5));
	auto const left = 42.5 + 5.0 / 3.0;
	QVERIFY(std::abs(layout.blocks[1].left() - left) < 1e-9);
	QCOMPARE(layout.blocks[1].top(), 0.0);
	QCOMPARE(layout.blocks[1].width(), 42.5);
	QCOMPARE(layout.blocks[1].height(), 25.0);
	
	// Two rows of the head and eleven others in the first block, five in the second.
	QCOMPARE(layout.rows.size(), std::size_t(18));
	QCOMPARE(layout.rows[12].block, 0);
	QCOMPARE(layout.rows[12].index, 10);
	QCOMPARE(layout.rows[13].block, 1);
	QCOMPARE(layout.rows[13].index, 11);
	QVERIFY(std::abs(layout.rows[13].rect.left() - left) < 1e-9);
	QCOMPARE(layout.rows[13].rect.top(), 0.0);
	// The start, ten controls; then four controls and the finish.
	QCOMPARE(ruleWeights(layout), QStringLiteral("TTttTttTttT") + QStringLiteral("TttTT"));
	QVERIFY(hasText(layout, Layout::Text::Number, "11", left + 2.5, 3.75));
	
	// Every block has its rule at the bottom.
	auto bottoms = 0;
	for (auto const& rule : layout.rules)
	{
		if (rule.thick && std::abs(rule.begin.y() - rule.end.y()) < 0.0005
		    && (std::abs(rule.begin.y() - 67.5) < 0.0005 || (std::abs(rule.begin.y() - 25.0) < 0.0005 && rule.begin.x() > 43)))
			++bottoms;
	}
	QCOMPARE(bottoms, 2);
	
	QCOMPARE(layout.backgrounds.size(), std::size_t(2));
	QVERIFY(std::abs(layout.backgrounds[1].left() - (left - 5.0 / 3.0)) < 1e-9);
	QVERIFY(std::abs(layout.backgrounds[1].height() - (25.0 + 10.0 / 3.0)) < 1e-9);
	options.setValue(QLatin1Char('b'), QStringLiteral("0"));
	QVERIFY(layoutCourseDescription(description, options).backgrounds.empty());
	
	// Without a maximum: one block.
	options.setValue(QLatin1Char('i'), QStringLiteral("0"));
	QCOMPARE(layoutCourseDescription(description, options).blocks.size(), std::size_t(1));
}




/// Accept (12)
void CourseDescriptionTest::pictureTest()
{
	for (auto with_symbols : { false, true })
	{
		DescriptionMap m;
		m.data.options = forestOptions();
		m.addDescription(0, 0, false);
		m.addDescription(0, -100, true);
		if (with_symbols)
			m.addBlockSymbols();
		
		auto const drawing = drawCourseDescriptions(m.map, m.data, QStringLiteral("B"));
		QVERIFY(drawing.drawn);
		QCOMPARE(drawing.layouts.size(), std::size_t(2));
		QCOMPARE(bool(drawing.plain[0]), !with_symbols);
		QCOMPARE(bool(drawing.plain[1]), !with_symbols);
		QVERIFY(!drawing.layouts[0].in_text);
		QVERIFY(drawing.layouts[1].in_text);
		// The block: the title, an empty row of the classes, the name; the start, three controls, the finish.
		QCOMPARE(drawing.layouts[0].blocks.size(), std::size_t(1));
		QCOMPARE(drawing.layouts[0].blocks[0], QRectF(0, 0, 51, 6 + 2.4 + 6 + 5 * 6));
		// Without the symbols there are objects for the pictograms which the map has, and nothing else.
		auto rules = 0;
		auto pictograms = 0;
		for (auto const& object : drawing.objects)
		{
			if (object->getType() == Object::Point)
				++pictograms;
			else if (object->getType() == Object::Path && object->getSymbol()->getType() == Symbol::Line)
				++rules;
		}
		// The knoll of the start, knoll and side of the second control, and with the symbols the start itself.
		QCOMPARE(pictograms, with_symbols ? 4 : 3);
		// A rule above each of the eight rows and one below; the vertical ones of the head, of four rows with columns and of the finish.
		QCOMPARE(rules, with_symbols ? 9 + 2 + 2 + 4 + 4 * 9 + 2 : 0);
		
		// 10 pixels for a millimetre; the map from (-5, -5) to (95, 175).
		auto const ground = QColor(0, 120, 200);
		QImage image(1000, 1800, QImage::Format_ARGB32_Premultiplied);
		image.fill(ground);
		{
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.scale(10.0, 10.0);
			painter.translate(5.0, 5.0);
			paintCourseDescriptions(&painter, &m.map, drawing, QRectF(-5, -5, 100, 180), 10.0);
		}
		auto const colour = [&image](double x, double y) {
			return QColor(image.pixel(qRound((x + 5) * 10), qRound((y + 5) * 10)));
		};
		auto const is_white = [](const QColor& c) { return c.red() > 240 && c.green() > 240 && c.blue() > 240; };
		auto const is_black = [](const QColor& c) { return c.red() < 60 && c.green() < 60 && c.blue() < 60; };
		
		// The cell C of the first control, which is empty; the first control is the row from 20.4 mm.
		QVERIFY2(is_white(colour(16.2, 23.4)), qPrintable(colour(16.2, 23.4).name()));
		// The background reaches 2 mm beyond the frame.
		QVERIFY(is_white(colour(-1.5, 10)));
		QVERIFY(colour(-3.5, 10) == ground);
		QVERIFY(colour(60, 10) == ground);
		// The frame at the left and a rule between columns.
		QVERIFY2(is_black(colour(0, 23.4)), qPrintable(colour(0, 23.4).name()));
		QVERIFY(is_black(colour(19.2, 23.4)));
		// The number of the first control has ink in its cell.
		auto found = false;
		for (auto x = 0.5; x <= 5.5 && !found; x += 0.1)
		{
			for (auto y = 21.0; y <= 26.0 && !found; y += 0.1)
				found = is_black(colour(x, y));
		}
		QVERIFY(found);
		
		// The description in text: lines from 100 mm down, white around them.
		QVERIFY(is_white(colour(-1.0, 99.0)));
		found = false;
		for (auto x = 0.0; x <= 30.0 && !found; x += 0.1)
		{
			for (auto y = 100.0; y <= 106.0 && !found; y += 0.1)
				found = is_black(colour(x, y));
		}
		QVERIFY(found);
	}
}


/// Accept (12), the last sentence
void CourseDescriptionTest::plainFitTest()
{
	DescriptionMap m;
	m.data.options = forestOptions();
	m.course("A very long name of a course", { "sS1", "c31", "fF2" });
	m.addDescription(0, 0, false);
	
	auto const drawing = drawCourseDescriptions(m.map, m.data, QStringLiteral("A very long name of a course"));
	QVERIFY(drawing.drawn);
	QCOMPARE(drawing.layouts.size(), std::size_t(1));
	QVERIFY(drawing.plain[0]);
	
	// 10 pixels for a millimetre; the map from (-5, -5) to (95, 95).
	auto const ground = QColor(0, 120, 200);
	QImage image(1000, 1000, QImage::Format_ARGB32_Premultiplied);
	image.fill(ground);
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(10.0, 10.0);
		painter.translate(5.0, 5.0);
		paintCourseDescriptions(&painter, &m.map, drawing, QRectF(-5, -5, 100, 100), 10.0);
	}
	auto const colour = [&image](double x, double y) {
		return QColor(image.pixel(qRound((x + 5) * 10), qRound((y + 5) * 10)));
	};
	auto const is_dark = [](const QColor& c) { return c.red() < 160 && c.green() < 160 && c.blue() < 160; };
	
	// The row of the name is the third, from 8.4 mm to 14.4 mm; its cell reaches from 0 to 19.2 mm.
	auto beyond = 0;
	auto inside = 0;
	for (auto y = 8.8; y <= 14.0; y += 0.1)
	{
		for (auto x = -1.4; x <= -0.4; x += 0.1)
			beyond += is_dark(colour(x, y)) ? 1 : 0;
		for (auto x = 0.6; x <= 18.6; x += 0.1)
			inside += is_dark(colour(x, y)) ? 1 : 0;
	}
	QCOMPARE(beyond, 0);
	QVERIFY(inside > 0);
}



QTEST_MAIN(CourseDescriptionTest)
#include "course_description_t.moc"  // IWYU pragma: keep
