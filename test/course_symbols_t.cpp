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
#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QIODevice>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_edit.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "course/course_symbols.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/widgets/course_widget.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

using namespace OpenOrienteering;


namespace {

/// Upstream's symbol set in the source folder.
QString setPath()
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("../symbol sets/10000/Course_Design_10000.omap"));
}

/// The symbol with a number. A component which is not there is 0, as an
/// OCD file has it: 703 is 703.000 there.
const Symbol* symbolOf(const Map& map, int first, int second = -1, int third = -1)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getNumberComponent(0) == first
		    && qMax(symbol->getNumberComponent(1), 0) == qMax(second, 0)
		    && qMax(symbol->getNumberComponent(2), 0) == qMax(third, 0))
			return symbol;
	}
	return nullptr;
}

const PointSymbol* pointOf(const Map& map, int first, int second = -1, int third = -1)
{
	auto const* symbol = symbolOf(map, first, second, third);
	return symbol && symbol->getType() == Symbol::Point ? static_cast<const PointSymbol*>(symbol) : nullptr;
}

QString nameOf(const Map& map, int first, int second)
{
	auto const* symbol = symbolOf(map, first, second);
	return symbol ? symbol->getPlainTextName() : QStringLiteral("(none)");
}

int columnsOf(const Map& map, int first, int second)
{
	auto const* symbol = symbolOf(map, first, second);
	return symbol && symbol->courseRole() == Symbol::DescriptionSymbol ? symbol->descriptionColumns() : -1;
}

/// The numbers of the symbols for the columns C to H, as "11 71 82 3 71 4".
QString columnCounts(const Map& map)
{
	QStringList counts;
	for (int column = 0; column < CourseObject::ColumnCount; ++column)
		counts.push_back(QString::number(descriptionSymbols(map, CourseObject::Column(column)).size()));
	return counts.join(QLatin1Char(' '));
}

/// Whether two point symbols are drawn alike: their own disc and ring, and the places of their elements.
bool sameDrawing(const PointSymbol* lhs, const PointSymbol* rhs)
{
	if (!lhs || !rhs
	    || lhs->getInnerRadius() != rhs->getInnerRadius()
	    || lhs->getOuterWidth() != rhs->getOuterWidth()
	    || lhs->getNumElements() != rhs->getNumElements())
		return false;
	for (int i = 0; i < lhs->getNumElements(); ++i)
	{
		if (lhs->getElementSymbol(i)->getType() != rhs->getElementSymbol(i)->getType())
			return false;
		auto const& left = lhs->getElementObject(i)->getRawCoordinateVector();
		auto const& right = rhs->getElementObject(i)->getRawCoordinateVector();
		if (left.size() != right.size())
			return false;
		for (std::size_t c = 0; c < left.size(); ++c)
		{
			if (left[c] != right[c])
				return false;
		}
	}
	return true;
}

/// The rectangle of the coordinates of the elements of a point symbol, in mm, y down.
QRectF extentOf(const PointSymbol* symbol)
{
	QRectF extent;
	auto first = true;
	for (int i = 0; i < symbol->getNumElements(); ++i)
	{
		for (auto const& coord : symbol->getElementObject(i)->getRawCoordinateVector())
		{
			auto const point = QPointF(coord.x(), coord.y());
			if (first)
				extent = QRectF(point, point);
			else
			{
				extent.setLeft(qMin(extent.left(), point.x()));
				extent.setRight(qMax(extent.right(), point.x()));
				extent.setTop(qMin(extent.top(), point.y()));
				extent.setBottom(qMax(extent.bottom(), point.y()));
			}
			first = false;
		}
	}
	return extent;
}

/// The direction of a chevron of three points: 1 when its middle point is the rightmost, -1 when the leftmost.
int chevronDirection(const PointSymbol* symbol)
{
	if (!symbol || symbol->getNumElements() != 1)
		return 0;
	auto const& coords = symbol->getElementObject(0)->getRawCoordinateVector();
	if (coords.size() != 3)
		return 0;
	if (coords[1].x() > coords[0].x() && coords[1].x() > coords[2].x())
		return 1;
	if (coords[1].x() < coords[0].x() && coords[1].x() < coords[2].x())
		return -1;
	return 0;
}

/// The diameter of the middle of the ring of a point symbol, in mm.
double ringDiameter(const PointSymbol* symbol)
{
	return (2 * symbol->getInnerRadius() + symbol->getOuterWidth()) / 1000.0;
}

QByteArray exportTo(const Map& map, const char* format_id)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return {};
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool importFrom(const QByteArray& data, const char* format_id, Map& map)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

/// A map of 1:10,000 with FOOM's course symbols and a course drawn with them.
struct SymbolMap
{
	Map map;
	CourseSymbolsReport report;
	
	explicit SymbolMap(unsigned int scale = 10000)
	{
		map.setScaleDenominator(scale);
		report = addCourseSymbols(map, setPath());
	}
	
	/// Adds an object as the drawing tools do.
	void push(Object* object)
	{
		auto const index = map.addObject(object);
		auto* step = new DeleteObjectsUndoStep(&map);
		step->addObject(index);
		map.push(step);
	}
	
	void point(int number, double x, double y)
	{
		auto* object = new PointObject(symbolOf(map, number));
		object->setPosition(MapCoord(x, y));
		push(object);
	}
	
	/// S1, 31 to 33, M1, F1 and D1, and the course A of them.
	void drawCourseA()
	{
		point(701, 0, 0);
		point(703, 20, 0);
		point(703, 40, 0);
		point(703, 60, 0);
		auto* path = new PathObject(symbolOf(map, 707));
		path->addCoordinate(MapCoord(64.0, 0.0));
		path->addCoordinate(MapCoord(74.0, 0.0));
		push(path);
		point(706, 80, 0);
		point(720, 100, -50);
		
		auto data = map.courseData();
		Course course;
		course.name = QStringLiteral("A");
		auto const add = [&course](CourseItem::Type type, const char* code) {
			CourseItem item;
			item.type = type;
			item.code = QString::fromLatin1(code);
			course.items.push_back(item);
		};
		add(CourseItem::Start, "S1");
		add(CourseItem::Control, "31");
		add(CourseItem::Control, "32");
		add(CourseItem::Control, "33");
		add(CourseItem::MarkedRoute, "M1");
		add(CourseItem::Finish, "F1");
		data.courses.push_back(course);
		data.options.setValue(QLatin1Char('e'), QStringLiteral("Test event"));
		data.options.setValue(QLatin1Char('b'), QStringLiteral("1"));
		setCourseObjectCell(map, data, QStringLiteral("31"), CourseObject::ColumnD, 1010);
		setCourseObjectCell(map, data, QStringLiteral("31"), CourseObject::ColumnG, 12102);
		setCourseObjectSize(map, data, QStringLiteral("32"), QStringLiteral("2"));
		setCourseObjectCell(map, data, QStringLiteral("32"), CourseObject::ColumnD, 5002);
		setCourseObjectCell(map, data, QStringLiteral("33"), CourseObject::ColumnC, 101);
		setCourseObjectCell(map, data, QStringLiteral("33"), CourseObject::ColumnD, 2004);
		setCourseObjectCell(map, data, QStringLiteral("33"), CourseObject::ColumnH, 13002);
		changeCourseData(map, data);
	}
};

/// The cells of a course object, as "0 1010 0 0 12102 0".
QString cellsText(const CourseObject& object)
{
	QStringList cells;
	for (auto cell : object.cell)
		cells.push_back(QString::number(cell));
	return cells.join(QLatin1Char(' '));
}

/// The numbers of the symbols of objects, as the cells have them, with their counts.
std::map<int, int> symbolCounts(const std::vector<std::unique_ptr<Object>>& objects)
{
	std::map<int, int> counts;
	for (auto const& object : objects)
		++counts[descriptionCellNumber(*object->getSymbol())];
	return counts;
}

}  // namespace



/**
 * @test Tests FOOM's course symbols, which are made of upstream's symbol
 *       set Course Design.
 *
 * The acceptance criteria are those of the design notes,
 * "FOOM's course symbols". The last case writes pictures for looking at
 * them.
 */
class CourseSymbolsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void numberTest();
	void pictogramTest();
	void roleTest();
	void partTest();
	void scaleTest();
	void addTest();
	void courseTest();
	void fileTest();
	void pictureTest();
};


void CourseSymbolsTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY2(QFileInfo::exists(setPath()), qPrintable(setPath()));
}


/// Accept (1)
void CourseSymbolsTest::numberTest()
{
	SymbolMap m;
	auto const& map = m.map;
	QVERIFY(m.report.loaded);
	QCOMPARE(columnCounts(map), QStringLiteral("11 71 82 3 71 4"));
	
	QCOMPARE(nameOf(map, 0, 101), QStringLiteral("Northern"));
	QCOMPARE(nameOf(map, 0, 102), QStringLiteral("Western"));
	QCOMPARE(nameOf(map, 0, 104), QStringLiteral("Eastern"));
	QCOMPARE(nameOf(map, 0, 208), QStringLiteral("North Eastern"));
	QCOMPARE(nameOf(map, 0, 300), QStringLiteral("Upper"));
	QCOMPARE(nameOf(map, 1, 10), QStringLiteral("Knoll"));
	QCOMPARE(nameOf(map, 5, 21), QStringLiteral("Building pass through"));
	QCOMPARE(nameOf(map, 5, 22), QStringLiteral("Stairway"));
	QCOMPARE(nameOf(map, 8, 11), QStringLiteral("Ruined"));
	QCOMPARE(nameOf(map, 10, 2), QStringLiteral("Junction"));
	QCOMPARE(nameOf(map, 11, 1), QStringLiteral("Bend"));
	QCOMPARE(nameOf(map, 12, 101), QStringLiteral("North Side"));
	QCOMPARE(nameOf(map, 12, 102), QStringLiteral("West Side"));
	QCOMPARE(nameOf(map, 12, 107), QStringLiteral("South east Side"));
	QCOMPARE(nameOf(map, 12, 708), QStringLiteral("North east End"));
	QCOMPARE(nameOf(map, 12, 128), QStringLiteral("North east Foot"));
	QCOMPARE(nameOf(map, 12, 8), QStringLiteral("Upper Part"));
	QCOMPARE(nameOf(map, 12, 13), QStringLiteral("Beneath"));
	QCOMPARE(nameOf(map, 12, 14), QStringLiteral("Between"));
	QCOMPARE(nameOf(map, 13, 2), QStringLiteral("Refreshment point"));
	QCOMPARE(nameOf(map, 13, 4), QStringLiteral("Radio or TV control"));
	
	auto const c = int(Symbol::DescriptionColumnC);
	auto const d_and_e = int(Symbol::DescriptionColumnD | Symbol::DescriptionColumnE);
	QCOMPARE(columnsOf(map, 0, 500), c);
	QCOMPARE(columnsOf(map, 1, 1), d_and_e);
	QCOMPARE(columnsOf(map, 6, 2), d_and_e);
	QCOMPARE(columnsOf(map, 7, 1), d_and_e);
	QCOMPARE(columnsOf(map, 8, 11), int(Symbol::DescriptionColumnE));
	QCOMPARE(columnsOf(map, 10, 2), int(Symbol::DescriptionColumnF));
	QCOMPARE(columnsOf(map, 11, 1), int(Symbol::DescriptionColumnF | Symbol::DescriptionColumnG));
	QCOMPARE(columnsOf(map, 12, 101), int(Symbol::DescriptionColumnG));
	QCOMPARE(columnsOf(map, 13, 5), int(Symbol::DescriptionColumnH));
	
	auto description_symbols = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		QVERIFY2(!symbol->getPlainTextName().startsWith(QLatin1Char('|')), qPrintable(symbol->getPlainTextName()));
		QVERIFY2(!symbol->getPlainTextName().trimmed().isEmpty(), qPrintable(symbol->getNumberAsString()));
		QCOMPARE(symbol->getPlainTextName(), symbol->getPlainTextName().trimmed());
		if (symbol->courseRole() != Symbol::DescriptionSymbol)
			continue;
		++description_symbols;
		QVERIFY2(symbol->getNumberComponent(1) >= 0 && symbol->getNumberComponent(2) < 0, qPrintable(symbol->getNumberAsString()));
		QCOMPARE(symbol->getType(), Symbol::Point);
		// The cell number finds the symbol again.
		QCOMPARE(symbolOf(map, descriptionCellNumber(*symbol) / 1000, descriptionCellNumber(*symbol) % 1000), symbol);
	}
	// 11 of C, 71 of D and E, 11 of E, 2 of F, the bend, 70 of G and 4 of H.
	QCOMPARE(description_symbols, 11 + 71 + 11 + 2 + 1 + 70 + 4);
	
	// No number twice.
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		QCOMPARE(symbolOf(map, symbol->getNumberComponent(0), symbol->getNumberComponent(1), symbol->getNumberComponent(2)), symbol);
	}
}


/// Accept (2)
void CourseSymbolsTest::pictogramTest()
{
	SymbolMap m;
	Map upstream;
	QVERIFY(upstream.loadFrom(setPath()));
	
	QVERIFY(sameDrawing(pointOf(m.map, 12, 102), pointOf(upstream, 11, 1, 7)));
	QVERIFY(sameDrawing(pointOf(m.map, 12, 107), pointOf(upstream, 11, 1, 4)));
	QVERIFY(sameDrawing(pointOf(m.map, 12, 208), pointOf(upstream, 11, 2, 2)));
	QVERIFY(sameDrawing(pointOf(m.map, 0, 102), pointOf(upstream, 0, 1, 4)));
	QVERIFY(sameDrawing(pointOf(m.map, 5, 21), pointOf(upstream, 5, 23)));
	QVERIFY(!sameDrawing(pointOf(m.map, 12, 102), pointOf(upstream, 11, 1, 3)));
	
	auto const* knoll = pointOf(m.map, 1, 10);
	QVERIFY(knoll);
	QCOMPARE(knoll->getInnerRadius(), 1000);
	QVERIFY(knoll->getInnerColor());
	QCOMPARE(QColor(knoll->getInnerColor()->operator const QColor&()).rgb(), QColor(Qt::black).rgb());
}


/// Accept (3)
void CourseSymbolsTest::roleTest()
{
	SymbolMap m;
	auto const& map = m.map;
	
	struct { int first; int second; Symbol::CourseRole role; Symbol::Type type; } const expected[] = {
	    { 701, -1, Symbol::CourseStart, Symbol::Point },
	    { 703, -1, Symbol::CourseControl, Symbol::Point },
	    { 706, -1, Symbol::CourseFinish, Symbol::Point },
	    { 707, -1, Symbol::CourseMarkedRoute, Symbol::Line },
	    { 720, -1, Symbol::CourseDescription, Symbol::Point },
	    { 721, -1, Symbol::CourseTitle, Symbol::Text },
	    { 722, -1, Symbol::CourseStartNumber, Symbol::Text },
	    { 722, 1, Symbol::CourseVariantCode, Symbol::Text },
	    { 704, -1, Symbol::NoCourseRole, Symbol::Text },
	    { 705, -1, Symbol::NoCourseRole, Symbol::Line },
	    { 708, -1, Symbol::NoCourseRole, Symbol::Line },
	    { 709, -1, Symbol::NoCourseRole, Symbol::Area },
	    { 709, 2, Symbol::NoCourseRole, Symbol::Line },
	    { 709, 4, Symbol::NoCourseRole, Symbol::Line },
	    { 709, 5, Symbol::NoCourseRole, Symbol::Area },
	    { 710, -1, Symbol::NoCourseRole, Symbol::Point },
	    { 711, -1, Symbol::NoCourseRole, Symbol::Point },
	    { 712, -1, Symbol::NoCourseRole, Symbol::Point },
	    { 713, -1, Symbol::NoCourseRole, Symbol::Point },
	    { 799, -1, Symbol::NoCourseRole, Symbol::Line },
	};
	for (auto const& entry : expected)
	{
		auto const* symbol = symbolOf(map, entry.first, entry.second);
		auto const number = QStringLiteral("%1.%2").arg(entry.first).arg(entry.second);
		QVERIFY2(symbol, qPrintable(number));
		QVERIFY2(symbol->courseRole() == entry.role, qPrintable(number));
		QVERIFY2(symbol->getType() == entry.type, qPrintable(number));
	}
	// Upstream's number of the control is that of nothing here.
	QVERIFY(!symbolOf(map, 702));
	// The sheets for descriptions which are placed by hand are left out.
	QVERIFY(!symbolOf(map, 99, 3));
}


/// Accept (4)
void CourseSymbolsTest::partTest()
{
	SymbolMap m;
	auto const& map = m.map;
	
	auto const* thick = symbolOf(map, 18, 1);
	auto const* thin = symbolOf(map, 18, 2);
	QVERIFY(thick && thick->getType() == Symbol::Line);
	QVERIFY(thin && thin->getType() == Symbol::Line);
	QCOMPARE(static_cast<const LineSymbol*>(thick)->getLineWidth(), 500);
	QCOMPARE(static_cast<const LineSymbol*>(thin)->getLineWidth(), 250);
	auto const* black = static_cast<const LineSymbol*>(thick)->getColor();
	QVERIFY(black);
	
	for (auto second : { 1, 2, 3, 4, 5, 6, 7 })
	{
		auto const* text = symbolOf(map, 19, second);
		QVERIFY2(text && text->getType() == Symbol::Text, qPrintable(QString::number(second)));
		QCOMPARE(static_cast<const TextSymbol*>(text)->getColor(), black);
	}
	QVERIFY(static_cast<const TextSymbol*>(symbolOf(map, 19, 1))->isBold());
	QCOMPARE(static_cast<const TextSymbol*>(symbolOf(map, 19, 7))->getNumCustomTabs(), 2);
	
	auto const* ground = symbolOf(map, 20, 1);
	QVERIFY(ground && ground->getType() == Symbol::Area);
	auto const* white = static_cast<const AreaSymbol*>(ground)->getColor();
	QVERIFY(white);
	QCOMPARE(QColor(white->operator const QColor&()).rgb(), QColor(Qt::white).rgb());
	QVERIFY(white->getPriority() > black->getPriority());
	
	for (auto number : { 15003, 15004, 15006, 16001, 16002, 16003, 17001, 17002, 17003, 17004, 17005 })
	{
		auto const* part = pointOf(map, number / 1000, number % 1000);
		QVERIFY2(part, qPrintable(QString::number(number)));
		QCOMPARE(part->courseRole(), Symbol::NoCourseRole);
		QVERIFY2(part->containsColor(black), qPrintable(QString::number(number)));
	}
	
	auto const* control = pointOf(map, 17, 2);
	QCOMPARE(control->getInnerRadius(), 1950);
	QCOMPARE(control->getOuterWidth(), 300);
	QVERIFY(!control->getInnerColor());
	QCOMPARE(control->getNumElements(), 0);
	auto const* finish = pointOf(map, 17, 3);
	QCOMPARE(finish->getInnerRadius(), 1950);
	QCOMPARE(finish->getNumElements(), 1);
	
	auto const dash = extentOf(pointOf(map, 16, 1));
	QCOMPARE(dash, QRectF(-1.4, 0.0, 2.8, 0.0));
	QCOMPARE(chevronDirection(pointOf(map, 16, 3)), -1);
	QCOMPARE(chevronDirection(pointOf(map, 17, 4)), 1);
	QCOMPARE(chevronDirection(pointOf(map, 16, 2)), 1);
	// A chevron stands around its middle.
	QCOMPARE(extentOf(pointOf(map, 17, 4)), QRectF(-0.9, -1.8, 1.8, 3.6));
	QCOMPARE(extentOf(pointOf(map, 16, 3)), QRectF(-0.9, -1.8, 1.8, 3.6));
	
	// The triangle of the start stands on its base.
	auto const start = extentOf(pointOf(map, 17, 1));
	QCOMPARE(start, QRectF(-2.0, -2.0, 4.0, 3.5));
	QVERIFY(sameDrawing(pointOf(map, 17, 1), pointOf(map, 17, 5)));
	
	auto const crossing = extentOf(pointOf(map, 15, 3));
	QVERIFY(crossing.left() >= -4.2 && crossing.right() <= 4.2 && crossing.width() > 6.0);
	QCOMPARE(pointOf(map, 15, 3)->getNumElements(), 2);
	auto const passage = extentOf(pointOf(map, 15, 4));
	QVERIFY(passage.left() >= -10.2 && passage.right() <= 10.2 && passage.width() > 15.0);
	QCOMPARE(pointOf(map, 15, 4)->getNumElements(), 2);
	auto const flip = extentOf(pointOf(map, 15, 6));
	QVERIFY(flip.left() >= -3.0 && flip.right() <= 3.0 && flip.top() >= -3.0 && flip.bottom() <= 3.0);
	QVERIFY(pointOf(map, 15, 6)->getNumElements() >= 2);
}


/// Accept (5)
void CourseSymbolsTest::scaleTest()
{
	struct { unsigned int scale; double diameter; int line; } const expected[] = {
	    { 4000, 6.0, 350 }, { 5000, 6.0, 350 }, { 7500, 7.5, 438 }, { 10000, 7.5, 438 }, { 15000, 5.0, 292 }, { 20000, 5.0, 292 },
	};
	for (auto const& entry : expected)
	{
		SymbolMap m(entry.scale);
		auto const* control = pointOf(m.map, 703);
		QVERIFY(control);
		QVERIFY2(qAbs(ringDiameter(control) - entry.diameter) < 0.01, qPrintable(QStringLiteral("1:%1: %2").arg(entry.scale).arg(ringDiameter(control))));
		QCOMPARE(control->getOuterWidth(), entry.line);
		// The description symbols are for a box of 6 mm at every scale.
		QCOMPARE(pointOf(m.map, 17, 2)->getInnerRadius(), 1950);
		QCOMPARE(pointOf(m.map, 1, 10)->getInnerRadius(), 1000);
		QCOMPARE(static_cast<const LineSymbol*>(symbolOf(m.map, 18, 1))->getLineWidth(), 500);
	}
}


/// Accept (6)
void CourseSymbolsTest::addTest()
{
	// Into a map with a colour.
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* brown = new MapColor(QStringLiteral("Brown"), 0);
		brown->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
		map.addColor(brown, 0);
		
		auto const first = addCourseSymbols(map, setPath());
		QVERIFY(first.loaded);
		QCOMPARE(first.added, map.getNumSymbols());
		// 170 description symbols, 11 parts of the block, 2 rules, 7 texts, the background, and 20 symbols of the course.
		QCOMPARE(first.added, 170 + 11 + 2 + 7 + 1 + 20);
		QCOMPARE(first.taken, 0);
		
		auto const* purple = static_cast<const PointSymbol*>(symbolOf(map, 703))->getOuterColor();
		auto const* black = static_cast<const LineSymbol*>(symbolOf(map, 18, 1))->getColor();
		auto const* white = static_cast<const AreaSymbol*>(symbolOf(map, 20, 1))->getColor();
		QVERIFY(purple && black && white);
		QVERIFY(purple->getPriority() < black->getPriority());
		QVERIFY(black->getPriority() < white->getPriority());
		QVERIFY(white->getPriority() < brown->getPriority());
		QCOMPARE(map.getColor(map.getNumColors() - 1), static_cast<const MapColor*>(brown));
		
		auto const symbols = map.getNumSymbols();
		auto const colours = map.getNumColors();
		auto const second = addCourseSymbols(map, setPath());
		QVERIFY(second.loaded);
		QCOMPARE(second.added, 0);
		QCOMPARE(second.taken, first.added);
		QCOMPARE(map.getNumSymbols(), symbols);
		QCOMPARE(map.getNumColors(), colours);
	}
	
	// Into a map which has a symbol with one of the numbers.
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* own = new PointSymbol();
		own->setNumberComponent(0, 703);
		own->setName(QStringLiteral("My own"));
		map.addSymbol(own, 0);
		
		auto const report = addCourseSymbols(map, setPath());
		QVERIFY(report.loaded);
		QCOMPARE(report.taken, 1);
		QCOMPARE(report.added, map.getNumSymbols() - 1);
		QCOMPARE(symbolOf(map, 703), static_cast<const Symbol*>(own));
		QCOMPARE(own->courseRole(), Symbol::NoCourseRole);
		QVERIFY(symbolOf(map, 701));
	}
	
	// From the data of the program, which the test finds in the source folder.
	{
		QDir::addSearchPath(QStringLiteral("data"), QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("..")));
		QVERIFY2(QFileInfo::exists(courseSymbolSetPath()), qPrintable(courseSymbolSetPath()));
		Map map;
		map.setScaleDenominator(15000);
		auto const report = addCourseSymbols(map);
		QVERIFY(report.loaded);
		QVERIFY(report.added > 200);
		QVERIFY(symbolOf(map, 12, 101));
	}
	
	// Without the symbol set.
	{
		Map map;
		auto const report = addCourseSymbols(map, QDir::temp().absoluteFilePath(QStringLiteral("there-is-no-such-set.omap")));
		QVERIFY(!report.loaded);
		QCOMPARE(report.added, 0);
		QCOMPARE(map.getNumSymbols(), 0);
		QCOMPARE(map.getNumColors(), 0);
	}
}


/// Accept (7)
void CourseSymbolsTest::courseTest()
{
	SymbolMap m;
	m.drawCourseA();
	auto const& map = m.map;
	auto const& data = map.courseData();
	
	QStringList codes;
	for (auto const& object : data.objects)
		codes.push_back(object.code);
	QCOMPARE(codes.join(QLatin1Char(' ')), QStringLiteral("S1 31 32 33 M1 F1 D1"));
	QCOMPARE(data.findObject(QStringLiteral("D1"))->kind, CourseObject::Description);
	QCOMPARE(data.findObject(QStringLiteral("M1"))->kind, CourseObject::MarkedRoute);
	QCOMPARE(cellsText(*data.findObject(QStringLiteral("31"))), QStringLiteral("0 1010 0 0 12102 0"));
	QCOMPARE(courseObjectText(map, *data.findObject(QStringLiteral("31"))), QStringLiteral("Knoll, West Side"));
	QCOMPARE(courseObjectText(map, *data.findObject(QStringLiteral("32"))), QStringLiteral("Track / Path, 2m"));
	QCOMPARE(courseObjectText(map, *data.findObject(QStringLiteral("33"))), QStringLiteral("Northern, Boulder, Refreshment point"));
	
	// The course: lines of 705 and numbers of 704.
	auto const course = drawCourse(map, data, QStringLiteral("A"));
	QVERIFY(course.drawn);
	auto lines = 0;
	auto numbers = 0;
	for (auto const& object : course.objects)
	{
		if (object->getSymbol() == symbolOf(map, 705))
			++lines;
		else if (object->getSymbol() == symbolOf(map, 704))
			++numbers;
	}
	QCOMPARE(numbers, 3);
	// Start to 31, 31 to 32, 32 to 33, and the piece from 33 to the route; the route ends before the finish.
	QVERIFY(lines >= 3);
	QCOMPARE(lines + numbers, int(course.objects.size()));
	
	// The description, with the symbols and not plainly.
	auto const description = drawCourseDescriptions(map, data, QStringLiteral("A"));
	QVERIFY(description.drawn);
	QCOMPARE(description.layouts.size(), std::size_t(1));
	QVERIFY(!description.plain[0]);
	auto counts = symbolCounts(description.objects);
	QVERIFY(counts[18001] > 10);
	QVERIFY(counts[18002] > 10);
	QCOMPARE(counts[20001], 1);
	QCOMPARE(counts[17001], 1);
	QCOMPARE(counts[1010], 1);
	QCOMPARE(counts[12102], 1);
	QCOMPARE(counts[5002], 1);
	QCOMPARE(counts[101], 1);
	QCOMPARE(counts[2004], 1);
	QCOMPARE(counts[13002], 1);
	QCOMPARE(counts[17002], 1);
	QCOMPARE(counts[16001], 6);
	QCOMPARE(counts[17004], 1);
	QCOMPARE(counts[17003], 1);
	QCOMPARE(counts[19001], 3);
	QCOMPARE(counts[19002], 3);
	QVERIFY(counts[19003] >= 1);
	QCOMPARE(counts[19004], 1);
	QCOMPARE(counts[19005], 1);
	// Every object is of a symbol of the map.
	for (auto const& object : description.objects)
		QVERIFY(map.findSymbolIndex(object->getSymbol()) >= 0);
	
	// The lists of the dock.
	CourseWidget widget(&m.map);
	QStringList list_counts;
	for (auto letter : { 'c', 'd', 'e', 'f', 'g', 'h' })
	{
		auto const* list = widget.findChild<QComboBox*>(QStringLiteral("course-cell-%1").arg(QLatin1Char(letter)));
		QVERIFY(list);
		list_counts.push_back(QString::number(list->count() - 1));
	}
	QCOMPARE(list_counts.join(QLatin1Char(' ')), QStringLiteral("11 71 82 3 71 4"));
}


/// Accept (8)
void CourseSymbolsTest::fileTest()
{
	SymbolMap m;
	m.drawCourseA();
	
	for (auto const* format : { "XML", "OCD2018" })
	{
		auto const written = exportTo(m.map, format);
		QVERIFY2(!written.isEmpty(), format);
		Map read;
		QVERIFY2(importFrom(written, qstrcmp(format, "XML") == 0 ? "XML" : "OCD", read), format);
		
		QCOMPARE(columnCounts(read), QStringLiteral("11 71 82 3 71 4"));
		for (int i = 0; i < m.map.getNumSymbols(); ++i)
		{
			auto const* symbol = m.map.getSymbol(i);
			if (symbol->courseRole() == Symbol::NoCourseRole)
				continue;
			auto const* found = symbolOf(read, symbol->getNumberComponent(0), symbol->getNumberComponent(1), symbol->getNumberComponent(2));
			QVERIFY2(found, qPrintable(QString::fromLatin1(format) + QLatin1Char(' ') + symbol->getNumberAsString()));
			QCOMPARE(found->getPlainTextName(), symbol->getPlainTextName());
			QCOMPARE(found->courseRole(), symbol->courseRole());
			QCOMPARE(found->descriptionColumns(), symbol->descriptionColumns());
			QCOMPARE(found->getType(), symbol->getType());
		}
		for (auto number : { 15003, 16001, 17001, 17004, 18001, 19001, 19007, 20001 })
			QVERIFY2(symbolOf(read, number / 1000, number % 1000), qPrintable(QString::fromLatin1(format) + QLatin1Char(' ') + QString::number(number)));
		
		auto const& data = read.courseData();
		QCOMPARE(data.objects.size(), m.map.courseData().objects.size());
		QCOMPARE(data.courses.size(), std::size_t(1));
		QVERIFY(data.courses == m.map.courseData().courses);
		QCOMPARE(cellsText(*data.findObject(QStringLiteral("31"))), QStringLiteral("0 1010 0 0 12102 0"));
		QCOMPARE(courseObjectText(read, *data.findObject(QStringLiteral("33"))), QStringLiteral("Northern, Boulder, Refreshment point"));
		// The description is drawn with the symbols of the file which was read.
		auto const description = drawCourseDescriptions(read, data, QStringLiteral("A"));
		QVERIFY(description.drawn);
		QVERIFY(!description.plain[0]);
	}
}



/// Accept (10)
void CourseSymbolsTest::pictureTest()
{
	SymbolMap m;
	m.drawCourseA();
	auto const& map = m.map;
	auto const drawing = drawCourseDescriptions(map, map.courseData(), QStringLiteral("A"));
	QVERIFY(drawing.drawn);
	auto const& layout = drawing.layouts[0];
	QVERIFY(!layout.blocks.empty());
	
	// The block begins at the description object, at (100, -50); the layout of a drawing is in the map.
	QCOMPARE(layout.blocks[0].topLeft(), QPointF(100.0, -50.0));
	auto const area = layout.blocks[0].adjusted(-4, -4, 4, 4);
	auto const pixels_per_mm = 12.0;
	QImage image(qCeil(area.width() * pixels_per_mm), qCeil(area.height() * pixels_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(190, 215, 190));
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(pixels_per_mm, pixels_per_mm);
		painter.translate(-area.topLeft());
		paintCourseDescriptions(&painter, &m.map, drawing, area, pixels_per_mm);
	}
	auto const is_black = [&image, &area, pixels_per_mm](const QPointF& map_point) {
		auto const pixel = (map_point - area.topLeft()) * pixels_per_mm;
		auto const c = QColor(image.pixel(qRound(pixel.x()), qRound(pixel.y())));
		return c.red() < 60 && c.green() < 60 && c.blue() < 60;
	};
	auto const is_white = [&image, &area, pixels_per_mm](const QPointF& map_point) {
		auto const pixel = (map_point - area.topLeft()) * pixels_per_mm;
		auto const c = QColor(image.pixel(qRound(pixel.x()), qRound(pixel.y())));
		return c.red() > 240 && c.green() > 240 && c.blue() > 240;
	};
	
	// The pictures, for looking at them.
	auto const folder = QString::fromLocal8Bit(qgetenv("FOOM_SYMBOLS_DUMP"));
	if (!folder.isEmpty())
	{
		QVERIFY(image.save(QDir(folder).absoluteFilePath(QStringLiteral("block.png"))));
		
		// A sheet with every description symbol and every part, each in a box of 6 mm with its number.
		std::vector<const Symbol*> symbols;
		for (int i = 0; i < map.getNumSymbols(); ++i)
		{
			auto const* symbol = map.getSymbol(i);
			if (symbol->getType() == Symbol::Point && symbol->getNumberComponent(0) < 100)
				symbols.push_back(symbol);
		}
		std::sort(begin(symbols), end(symbols), [](const Symbol* lhs, const Symbol* rhs) {
			return descriptionCellNumber(*lhs) < descriptionCellNumber(*rhs);
		});
		auto const columns = 12;
		auto const cell_width = 150;
		auto const cell_height = 150;
		auto const rows = (int(symbols.size()) + columns - 1) / columns;
		QImage sheet(columns * cell_width, rows * cell_height, QImage::Format_ARGB32_Premultiplied);
		sheet.fill(Qt::white);
		{
			QPainter painter(&sheet);
			painter.setRenderHint(QPainter::Antialiasing);
			QFont font = painter.font();
			font.setPixelSize(11);
			painter.setFont(font);
			for (std::size_t i = 0; i < symbols.size(); ++i)
			{
				auto const column = int(i) % columns;
				auto const row = int(i) / columns;
				auto const left = column * cell_width;
				auto const top = row * cell_height;
				// 12 pixels for a millimetre: the box of 6 mm is 72 pixels.
				auto const box = QRectF(left + (cell_width - 72) / 2.0, top + 26.0, 72.0, 72.0);
				auto const number = descriptionCellNumber(*symbols[i]);
				painter.setPen(QColor(200, 200, 200));
				painter.drawRect(box);
				// An icon of 120 pixels at a zoom at which 1 mm is 12 pixels; a symbol
				// which is larger than the icon is made smaller by createIcon().
				auto const side = 120;
				auto const icon = symbols[i]->createIcon(map, side, true, 24.0 / side);
				painter.drawImage(QPointF(box.center().x() - side / 2.0, box.center().y() - side / 2.0), icon);
				painter.setPen(Qt::black);
				const QString label = QStringLiteral("%1.%2 ").arg(number / 1000).arg(number % 1000, 3, 10, QLatin1Char('0')) + symbols[i]->getPlainTextName();
				painter.drawText(QRectF(left + 2, top + 112, cell_width - 4, 36), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, label);
			}
		}
		QVERIFY(sheet.save(QDir(folder).absoluteFilePath(QStringLiteral("sheet.png"))));
	}
	
	auto knolls = 0;
	auto dashes = 0;
	auto rings = 0;
	for (auto const& pictogram : layout.pictograms)
	{
		auto const centre = pictogram.centre;
		if (pictogram.symbol == 1010)
		{
			// The dot of the knoll.
			QVERIFY(is_black(centre));
			QVERIFY(is_white(centre + QPointF(2.0, 2.0)));
			++knolls;
		}
		else if (pictogram.symbol == 16001)
		{
			QVERIFY(is_black(centre));
			QVERIFY(is_black(centre + QPointF(1.3, 0.0)));
			QVERIFY(is_white(centre + QPointF(0.0, 1.0)));
			++dashes;
		}
		else if (pictogram.symbol == 17002)
		{
			// A ring: white in its middle, ink on its line.
			QVERIFY(is_white(centre));
			QVERIFY(is_black(centre + QPointF(2.1, 0.0)));
			++rings;
		}
	}
	QCOMPARE(knolls, 1);
	QCOMPARE(dashes, 6);
	QCOMPARE(rings, 1);
}


QTEST_MAIN(CourseSymbolsTest)
#include "course_symbols_t.moc"  // IWYU pragma: keep
