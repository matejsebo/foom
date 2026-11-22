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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QIODevice>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/object_style.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/private_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/objects/text_object.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

MapColor* addColor(Map& map, const QString& name, const MapColorCmyk& cmyk)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setCmyk(cmyk);
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

LineSymbol* addLineSymbol(Map& map, const QString& name, const MapColor* color, int number, double width = 0.5)
{
	auto* symbol = new LineSymbol();
	symbol->setName(name);
	symbol->setColor(color);
	symbol->setLineWidth(width);
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

AreaSymbol* addAreaSymbol(Map& map, const QString& name, const MapColor* color, int number)
{
	auto* symbol = new AreaSymbol();
	symbol->setName(name);
	symbol->setColor(color);
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

PathObject* addLine(Map& map, const Symbol* symbol, double y)
{
	auto* object = new PathObject(symbol, MapCoordVector{ MapCoord(0.0, y), MapCoord(10.0, y) });
	map.addObject(object);
	return object;
}

PathObject* addSquare(Map& map, const Symbol* symbol, double x, double y, double size)
{
	MapCoordVector coords {
	    MapCoord(x, y), MapCoord(x + size, y), MapCoord(x + size, y + size), MapCoord(x, y + size), MapCoord(x, y) };
	coords.back().setClosePoint(true);
	auto* object = new PathObject(symbol, coords);
	map.addObject(object);
	return object;
}

void select(Map& map, std::initializer_list<Object*> objects)
{
	map.clearObjectSelection(false);
	for (auto* object : objects)
		map.addObjectToSelection(object, false);
}

int countPrivate(const Map& map)
{
	auto count = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		count += map.getSymbol(i)->isPrivate();
	return count;
}

/// The number of mostly red pixels, drawn as in print (no helper symbols).
int countRedPixels(Map& map)
{
	map.updateObjects();
	QRectF const extent(-20, -20, 40, 40);
	constexpr double pixel_per_mm = 5;
	QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.scale(pixel_per_mm, pixel_per_mm);
	painter.translate(-extent.topLeft());
	map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
	painter.end();
	auto count = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			auto const c = QColor(image.pixel(x, y));
			if (c.red() > 200 && c.blue() < 60)
				++count;
		}
	}
	return count;
}

QByteArray save(const Map& map, bool foom)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setOption(QStringLiteral("foom"), foom);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool load(const QByteArray& data, Map& map)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format.makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

const MapColorCmyk red { 0.0f, 1.0f, 1.0f, 0.0f };
const MapColorCmyk blue { 1.0f, 1.0f, 0.0f, 0.0f };

}  // namespace



/**
 * Tests private symbols.
 *
 * See the design notes, "Private symbols".
 */
class PrivateSymbolTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	/// Accept (1)
	void copyTest()
	{
		Map map;
		auto* line = addLineSymbol(map, QStringLiteral("Line"), addColor(map, QStringLiteral("Red"), red), 101);
		QVERIFY(!line->isPrivate());
		
		auto copy = makePrivateCopy(*line);
		QVERIFY(copy->isPrivate());
		QCOMPARE(copy->getName(), line->getName());
		QCOMPARE(copy->getNumberAsString(), line->getNumberAsString());
		QCOMPARE(copy->asLine()->getColor(), line->getColor());
		QCOMPARE(copy->asLine()->getLineWidth(), line->getLineWidth());
		QVERIFY(!copy->equals(line));
		
		auto* area = addAreaSymbol(map, QStringLiteral("Area"), addColor(map, QStringLiteral("Blue"), blue), 102);
		auto* combined = new CombinedSymbol();
		combined->setName(QStringLiteral("Combined"));
		combined->setNumParts(2);
		combined->setPart(0, line, false);
		combined->setPart(1, area, false);
		map.addSymbol(combined, map.getNumSymbols());
		
		auto combined_copy = makePrivateCopy(*combined);
		QVERIFY(combined_copy->isPrivate());
		auto const* parts = combined_copy->asCombined();
		QCOMPARE(parts->getNumParts(), 2);
		QVERIFY(parts->isPartPrivate(0));
		QVERIFY(parts->isPartPrivate(1));
		QVERIFY(parts->getPart(0) != line);
		QVERIFY(parts->getPart(1) != area);
		QVERIFY(parts->getPart(0)->equals(line));
		QVERIFY(parts->getPart(1)->equals(area));
		QVERIFY(!combined_copy->containsSymbol(line));
		QVERIFY(!combined_copy->containsSymbol(area));
	}
	
	/// Accept (2)
	void fileTest()
	{
		Map map;
		auto* line = addLineSymbol(map, QStringLiteral("Line"), addColor(map, QStringLiteral("Red"), red), 101);
		auto* object = addLine(map, line, 0.0);
		select(map, { object });
		QCOMPARE(convertToGraphic(map), 1);
		
		auto const with = save(map, true);
		QVERIFY(with.contains("is_private=\"true\""));
		Map loaded;
		QVERIFY(load(with, loaded));
		QCOMPARE(loaded.getNumSymbols(), 2);
		QVERIFY(!loaded.getSymbol(0)->isPrivate());
		QVERIFY(loaded.getSymbol(1)->isPrivate());
		QCOMPARE(loaded.getNumPublicSymbols(), 1);
		QCOMPARE(loaded.getPart(0)->getObject(0)->getSymbol(), loaded.getSymbol(1));
		
		auto const without = save(map, false);
		QVERIFY(!without.isEmpty());
		QVERIFY(!without.contains("is_private"));
	}
	
	/// Accept (3)
	void orderTest()
	{
		Map map;
		auto* color = addColor(map, QStringLiteral("Red"), red);
		auto* z = addLineSymbol(map, QStringLiteral("Z"), color, 9);
		LineSymbol zero;
		zero.setName(QStringLiteral("Zero"));
		zero.setColor(color);
		zero.setNumberComponent(0, 0);
		auto* hidden = makePrivateCopy(zero).release();
		map.addSymbol(hidden, 0);
		QCOMPARE(map.getSymbol(1), static_cast<const Symbol*>(hidden));
		
		auto* a = addLineSymbol(map, QStringLiteral("A"), color, 1);
		QCOMPARE(map.getNumSymbols(), 3);
		QCOMPARE(map.getNumPublicSymbols(), 2);
		QCOMPARE(map.getSymbol(0), static_cast<const Symbol*>(z));
		QCOMPARE(map.getSymbol(1), static_cast<const Symbol*>(a));
		QCOMPARE(map.getSymbol(2), static_cast<const Symbol*>(hidden));
		
		// By number alone the private symbol, number 0, would come first.
		map.sortSymbols(Symbol::lessByNumber);
		QCOMPARE(map.getSymbol(0), static_cast<const Symbol*>(a));
		QCOMPARE(map.getSymbol(1), static_cast<const Symbol*>(z));
		QCOMPARE(map.getSymbol(2), static_cast<const Symbol*>(hidden));
	}
	
	/// Accept (4)
	void convertTest()
	{
		Map map;
		auto* line = addLineSymbol(map, QStringLiteral("Line"), addColor(map, QStringLiteral("Red"), red), 101, 0.5);
		auto* first = addLine(map, line, 0.0);
		auto* second = addLine(map, line, 5.0);
		auto* third = addLine(map, line, 10.0);
		auto const steps = map.undoManager().undoStepCount();
		
		select(map, { first, second });
		QCOMPARE(convertToGraphic(map), 2);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(map.getNumSymbols(), 3);
		QCOMPARE(countPrivate(map), 2);
		QVERIFY(first->getSymbol()->isPrivate());
		QVERIFY(second->getSymbol()->isPrivate());
		QVERIFY(first->getSymbol() != second->getSymbol());
		QCOMPARE(third->getSymbol(), static_cast<const Symbol*>(line));
		
		line->setLineWidth(1.0);
		QCOMPARE(first->getSymbol()->asLine()->getLineWidth(), 500);
		QCOMPARE(second->getSymbol()->asLine()->getLineWidth(), 500);
		
		// The only user of a private symbol stays as it is.
		select(map, { first });
		QCOMPARE(convertToGraphic(map), 0);
		QCOMPARE(map.getNumSymbols(), 3);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(first->getSymbol(), static_cast<const Symbol*>(line));
		QCOMPARE(second->getSymbol(), static_cast<const Symbol*>(line));
	}
	
	/// Accept (5)
	void drawTest()
	{
		Map map;
		auto* area = addAreaSymbol(map, QStringLiteral("Area"), addColor(map, QStringLiteral("Red"), red), 102);
		auto* object = addSquare(map, area, 0, 0, 10);
		auto const before = countRedPixels(map);
		QVERIFY(before > 2000);
		
		select(map, { object });
		QCOMPARE(convertToGraphic(map), 1);
		QVERIFY(object->getSymbol()->isPrivate());
		QCOMPARE(countRedPixels(map), before);
	}
	
	/// Accept (6)
	void purgeTest()
	{
		{
			Map map;
			auto* line = addLineSymbol(map, QStringLiteral("Line"), addColor(map, QStringLiteral("Red"), red), 101);
			auto* object = addLine(map, line, 0.0);
			select(map, { object });
			QCOMPARE(convertToGraphic(map), 1);
			QCOMPARE(purgePrivateSymbols(map), 0);
			
			select(map, { object });
			map.deleteSelectedObjects();
			QCOMPARE(map.getNumObjects(), 0);
			QCOMPARE(purgePrivateSymbols(map), 0);
			QCOMPARE(map.getNumSymbols(), 2);
			
			map.undoManager().clear();
			QCOMPARE(purgePrivateSymbols(map), 1);
			QCOMPARE(map.getNumSymbols(), 1);
			QCOMPARE(map.getSymbol(0), static_cast<const Symbol*>(line));
		}
		{
			Map map;
			auto* line = addLineSymbol(map, QStringLiteral("Line"), addColor(map, QStringLiteral("Red"), red), 101);
			auto* object = addLine(map, line, 0.0);
			select(map, { object });
			QCOMPARE(convertToGraphic(map), 1);
			QVERIFY(map.undoManager().undo());
			QCOMPARE(object->getSymbol(), static_cast<const Symbol*>(line));
			QVERIFY(map.undoManager().canRedo());
			QCOMPARE(purgePrivateSymbols(map), 0);
			
			QVERIFY(map.undoManager().redo());
			QVERIFY(object->getSymbol()->isPrivate());
			QVERIFY(map.undoManager().undo());
			map.undoManager().clear();
			QCOMPARE(purgePrivateSymbols(map), 1);
			QCOMPARE(map.getNumSymbols(), 1);
		}
	}
	
	/// Accept (7)
	void importTest()
	{
		Map source;
		auto* line = addLineSymbol(source, QStringLiteral("Line"), addColor(source, QStringLiteral("Red"), red), 101);
		auto* object = addLine(source, line, 0.0);
		select(source, { object });
		QCOMPARE(convertToGraphic(source), 1);
		
		Map map;
		map.importMap(source, Map::MinimalObjectImport, nullptr, -1, true);
		map.importMap(source, Map::MinimalObjectImport, nullptr, -1, true);
		QCOMPARE(map.getNumObjects(), 2);
		QCOMPARE(map.getNumSymbols(), 2);
		QCOMPARE(countPrivate(map), 2);
		auto const* part = map.getPart(0);
		QVERIFY(part->getObject(0)->getSymbol() != part->getObject(1)->getSymbol());
		QCOMPARE(map.getNumColors(), 1);
	}

	/// Accept (8)
	void objectStyleOfTest()
	{
		Map map;
		auto* red_color = addColor(map, QStringLiteral("Red"), red);
		auto* blue_color = addColor(map, QStringLiteral("Blue"), MapColorCmyk(1, 0.5f, 0, 0));

		LineSymbol line;
		line.setColor(red_color);
		line.setLineWidth(0.5);
		auto style = objectStyleOf(*makePrivateCopy(line));
		QVERIFY(style.has_line);
		QCOMPARE(style.line_color, red_color);
		QCOMPARE(style.line_width, 0.5);
		QVERIFY(!style.has_fill);
		QVERIFY(!style.has_text);

		auto* border = new LineSymbol();
		border->setColor(blue_color);
		border->setLineWidth(0.3);
		auto* fill = new AreaSymbol();
		fill->setColor(red_color);
		CombinedSymbol combined;
		combined.setNumParts(2);
		combined.setPart(0, border, true);
		combined.setPart(1, fill, true);
		style = objectStyleOf(combined);
		QVERIFY(style.has_line);
		QCOMPARE(style.line_color, blue_color);
		QCOMPARE(style.line_width, 0.3);
		QVERIFY(style.has_fill);
		QCOMPARE(style.fill_color, red_color);
		QVERIFY(!style.has_text);

		TextSymbol text;
		text.setColor(red_color);
		text.setFontFamily(QStringLiteral("Arial"));
		text.setFontSize(4000);
		style = objectStyleOf(text);
		QVERIFY(style.has_text);
		QCOMPARE(style.text_color, red_color);
		QCOMPARE(style.font_family, QStringLiteral("Arial"));
		QCOMPARE(style.font_size, 4.0);
		QVERIFY(!style.has_line);

		PointSymbol point;
		style = objectStyleOf(point);
		QVERIFY(!style.has_line);
		QVERIFY(!style.has_fill);
		QVERIFY(!style.has_text);
	}

	/// Accept (9)
	void setObjectStyleTest()
	{
		Map map;
		auto* red_color = addColor(map, QStringLiteral("Red"), red);
		auto* blue_color = addColor(map, QStringLiteral("Blue"), MapColorCmyk(1, 0.5f, 0, 0));
		auto* line = addLineSymbol(map, QStringLiteral("Line"), red_color, 101, 0.5);
		auto* a = addLine(map, line, 0.0);
		auto* b = addLine(map, line, 5.0);
		auto* c = addLine(map, line, 10.0);
		select(map, { a });
		QCOMPARE(convertToGraphic(map), 1);
		auto const* old_symbol = a->getSymbol();
		b->setSymbol(old_symbol, true);
		c->setSymbol(old_symbol, true);
		select(map, { a, b });
		QCOMPARE(sharedPrivateSymbol(map), old_symbol);

		auto const steps = map.undoManager().undoStepCount();
		auto style = objectStyleOf(*old_symbol);
		style.line_color = blue_color;
		style.line_width = 0.8;
		QVERIFY(setObjectStyle(map, style));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		auto const* new_symbol = a->getSymbol();
		QVERIFY(new_symbol != old_symbol);
		QCOMPARE(b->getSymbol(), new_symbol);
		QVERIFY(new_symbol->isPrivate());
		QVERIFY(map.findSymbolIndex(new_symbol) >= 0);
		QCOMPARE(new_symbol->asLine()->getColor(), blue_color);
		QCOMPARE(new_symbol->asLine()->getLineWidth(), 800);
		QCOMPARE(c->getSymbol(), old_symbol);
		QCOMPARE(old_symbol->asLine()->getColor(), red_color);
		QCOMPARE(old_symbol->asLine()->getLineWidth(), 500);

		map.undoManager().undo();
		QCOMPARE(map.getPart(0)->getObject(0)->getSymbol(), old_symbol);
		QCOMPARE(map.getPart(0)->getObject(1)->getSymbol(), old_symbol);

		auto* first = map.getPart(0)->getObject(0);
		auto* second = map.getPart(0)->getObject(1);
		select(map, { first, second });
		auto const steps_after_undo = map.undoManager().undoStepCount();
		QVERIFY(!setObjectStyle(map, objectStyleOf(*old_symbol)));
		QCOMPARE(map.undoManager().undoStepCount(), steps_after_undo);

		style = objectStyleOf(*old_symbol);
		style.line_width = 0;
		QVERIFY(setObjectStyle(map, style));
		QCOMPARE(first->getSymbol()->asLine()->getLineWidth(), 10);
	}

	/// Accept (10)
	void setObjectStyleCombinedAndTextTest()
	{
		Map map;
		auto* red_color = addColor(map, QStringLiteral("Red"), red);
		auto* blue_color = addColor(map, QStringLiteral("Blue"), MapColorCmyk(1, 0.5f, 0, 0));
		auto* border = new LineSymbol();
		border->setColor(blue_color);
		border->setLineWidth(0.3);
		auto* fill = new AreaSymbol();
		fill->setColor(red_color);
		auto* combined = new CombinedSymbol();
		combined->setNumParts(2);
		combined->setPart(0, border, true);
		combined->setPart(1, fill, true);
		combined->setPrivate(true);
		map.addSymbol(combined, map.getNumSymbols());
		auto* area = addSquare(map, combined, 0, 0, 10);
		select(map, { area });

		auto style = objectStyleOf(*combined);
		style.fill_color = blue_color;
		QVERIFY(setObjectStyle(map, style));
		auto const* new_symbol = area->getSymbol()->asCombined();
		QVERIFY(new_symbol != combined);
		QCOMPARE(new_symbol->getPart(1)->asArea()->getColor(), blue_color);
		QCOMPARE(new_symbol->getPart(0)->asLine()->getColor(), blue_color);
		QCOMPARE(new_symbol->getPart(0)->asLine()->getLineWidth(), 300);
		QCOMPARE(combined->getPart(1)->asArea()->getColor(), red_color);

		auto* text_symbol = new TextSymbol();
		text_symbol->setColor(red_color);
		text_symbol->setFontFamily(QStringLiteral("Arial"));
		text_symbol->setFontSize(4000);
		text_symbol->setPrivate(true);
		map.addSymbol(text_symbol, map.getNumSymbols());
		auto* text = new TextObject(text_symbol);
		text->setAnchorPosition(MapCoordF(0, 0));
		text->setText(QStringLiteral("Text"));
		map.addObject(text);
		select(map, { text });
		style = objectStyleOf(*text_symbol);
		style.font_family = QStringLiteral("Courier New");
		style.font_size = 5;
		QVERIFY(setObjectStyle(map, style));
		auto const* new_text = text->getSymbol()->asText();
		QVERIFY(new_text != text_symbol);
		QCOMPARE(new_text->getFontFamily(), QStringLiteral("Courier New"));
		QCOMPARE(new_text->getFontSize(), 5.0);
		QCOMPARE(new_text->getColor(), red_color);
	}
};


QTEST_MAIN(PrivateSymbolTest)
#include "private_symbol_t.moc"  // IWYU pragma: keep
