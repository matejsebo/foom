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
#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <vector>

#include <QtGlobal>
#include <QtEndian>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/ocd_graphic_object.h"
#include "fileformats/ocd_layout_image.h"
#include "core/objects/image_object.h"

using namespace OpenOrienteering;


/**
 * @test The graphic and layout objects in OCD files.
 *
 * See the design notes, "Graphic and layout objects in OCD files".
 */
class OcdGraphicObjectTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}

	/// Accept (1): the object string of a layout object.
	void layoutStyle()
	{
		auto style = parseOcdLayoutStyle(QStringLiteral("\to50"));
		QVERIFY(style.font.isEmpty());
		QCOMPARE(style.opacity, 0.5);
		QCOMPARE(style.size_pt, 0.0);
		QCOMPARE(style.alignment, 3);
		style = parseOcdLayoutStyle(QStringLiteral("Arial\to50\ts12.0\tb0\ti0\ta3"));
		QCOMPARE(style.font, QStringLiteral("Arial"));
		QCOMPARE(style.opacity, 0.5);
		QCOMPARE(style.size_pt, 12.0);
		QVERIFY(!style.bold && !style.italic);
		QCOMPARE(style.alignment, 3);
		style = parseOcdLayoutStyle(QStringLiteral("Times New Roman\to100\ts9.5\tb1\ti1\ta1"));
		QCOMPARE(style.font, QStringLiteral("Times New Roman"));
		QCOMPARE(style.opacity, 1.0);
		QCOMPARE(style.size_pt, 9.5);
		QVERIFY(style.bold && style.italic);
		QCOMPARE(style.alignment, 1);
		style = parseOcdLayoutStyle({});
		QCOMPARE(style.opacity, 1.0);
		QCOMPARE(style.alignment, 3);
		// Values out of range and fields which are no numbers leave the defaults.
		style = parseOcdLayoutStyle(QStringLiteral("\to250\ta9\tsx"));
		QCOMPARE(style.opacity, 1.0);
		QCOMPARE(style.alignment, 3);
		QCOMPARE(style.size_pt, 0.0);
	}

	/// Accept (2): the colour of a layout object.
	void layoutColor()
	{
		auto const red = ocdLayoutCmyk(0x00ffff00u);  // the bytes 00 ff ff 00
		QCOMPARE(red.c, 0.0f);
		QCOMPARE(red.m, 1.0f);
		QCOMPARE(red.y, 1.0f);
		QCOMPARE(red.k, 0.0f);
		auto const yellow = ocdLayoutCmyk(0x00c94500u);  // 00 45 c9 00: a yellow of 27 and 79 percent
		QVERIFY(std::abs(yellow.m - 0.27f) < 0.003f);
		QVERIFY(std::abs(yellow.y - 0.79f) < 0.003f);
		QCOMPARE(ocdLayoutColorName(red, 1.0), QStringLiteral("Layout C0 M100 Y100 K0"));
		QCOMPARE(ocdLayoutColorName(red, 0.5), QStringLiteral("Layout C0 M100 Y100 K0, 50 %"));
		QCOMPARE(ocdLayoutColorName(yellow, 1.0), QStringLiteral("Layout C0 M27 Y79 K0"));

		Map map;
		map.addColor(new MapColor(QStringLiteral("Black"), 0), 0);
		auto const* half = ocdLayoutColor(map, red, 0.5);
		QVERIFY(half);
		QCOMPARE(map.getNumColors(), 2);
		QCOMPARE(map.getColor(0), half);
		QCOMPARE(half->getName(), QStringLiteral("Layout C0 M100 Y100 K0, 50 %"));
		QCOMPARE(half->getOpacity(), 0.5f);
		QCOMPARE(half->getCmyk().m, 1.0f);
		QCOMPARE(ocdLayoutColor(map, red, 0.5), half);
		QCOMPARE(map.getNumColors(), 2);
		QVERIFY(ocdLayoutColor(map, red, 1.0) != half);
		QCOMPARE(map.getNumColors(), 3);
	}

	/// Accept (3): the private symbols.
	void graphicSymbols()
	{
		MapColor color(QStringLiteral("Brown"), 0);
		auto line = makeOcdGraphicSymbol(2, &color, 21, nullptr, QStringLiteral("Graphic object"));
		QVERIFY(line);
		QCOMPARE(line->getType(), Symbol::Line);
		QVERIFY(line->isPrivate());
		QCOMPARE(line->getName(), QStringLiteral("Graphic object"));
		QCOMPARE(line->asLine()->getLineWidth(), 210);
		QCOMPARE(line->asLine()->getColor(), static_cast<const MapColor*>(&color));
		auto area = makeOcdGraphicSymbol(3, &color, 0, nullptr, QStringLiteral("Graphic object"));
		QVERIFY(area);
		QCOMPARE(area->getType(), Symbol::Area);
		QVERIFY(area->isPrivate());
		QCOMPARE(area->asArea()->getColor(), static_cast<const MapColor*>(&color));
		auto style = parseOcdLayoutStyle(QStringLiteral("Arial\to100\ts12.0\tb1\ti0\ta1"));
		auto text = makeOcdGraphicSymbol(4, &color, 0, &style, QStringLiteral("Text object"));
		QVERIFY(text);
		QCOMPARE(text->getType(), Symbol::Text);
		QVERIFY(text->isPrivate());
		QCOMPARE(text->asText()->getFontFamily(), QStringLiteral("Arial"));
		QCOMPARE(text->asText()->getFontSize(), 4.233);
		QVERIFY(text->asText()->isBold());
		QVERIFY(!text->asText()->isItalic());
		// What the probe did not show stays without a symbol.
		QVERIFY(!makeOcdGraphicSymbol(4, &color, 0, nullptr, {}));
		QVERIFY(!makeOcdGraphicSymbol(1, &color, 0, &style, {}));
		QVERIFY(!makeOcdGraphicSymbol(5, &color, 0, &style, {}));
	}

	/// Accept (4): the entry of a layout line, area or text in the strings of type 27 is no image and no fault.
	void layoutEntry()
	{
		QString warning;
		QVERIFY(!ocdLayoutImageFromString(QStringLiteral("Line object\tr0\ts1\tn6220"), &warning));
		QVERIFY2(warning.isEmpty(), qPrintable(warning));
		QVERIFY(!ocdLayoutImageFromString(QStringLiteral("picture.png\tr1\ts1\tu0.1\tv0.1\tx1\ty2"), &warning));
		QVERIFY(!warning.isEmpty());
	}

	/// Accept (5): a file with the format's records. FOOM writes five objects; their records are then given the
	/// symbol numbers, colour fields and widths of the format, and the file is read again.
	void importFromFile()
	{
		auto const* format = FileFormats.findFormat("OCD2018");
		QVERIFY(format);
		Map map;
		map.setScaleDenominator(10000);
		auto* black = new MapColor(QStringLiteral("Black"), 0);
		black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
		map.addColor(black, 0);
		auto* brown = new MapColor(QStringLiteral("Brown"), 1);
		brown->setCmyk({ 0.0f, 0.56f, 1.0f, 0.18f });
		map.addColor(brown, 1);
		auto* yellow = new MapColor(QStringLiteral("Yellow"), 2);
		yellow->setCmyk({ 0.0f, 0.27f, 0.79f, 0.0f });
		map.addColor(yellow, 2);
		auto* line_symbol = new LineSymbol();
		line_symbol->setNumberComponent(0, 101);
		line_symbol->setColor(black);
		line_symbol->setLineWidth(0.14);
		map.addSymbol(line_symbol, 0);
		auto* area_symbol = new AreaSymbol();
		area_symbol->setNumberComponent(0, 401);
		area_symbol->setColor(black);
		map.addSymbol(area_symbol, 1);
		auto* text_symbol = new TextSymbol();
		text_symbol->setNumberComponent(0, 801);
		text_symbol->setColor(black);
		map.addSymbol(text_symbol, 2);

		auto const add_path = [&map](const Symbol* symbol, std::vector<MapCoord> coords, const char* object_string) {
			auto* path = new PathObject(symbol);
			for (auto const& coord : coords)
				path->addCoordinate(coord);
			if (symbol->getType() == Symbol::Area)
				path->closeAllParts();
			if (object_string)
			{
				path->setTag(QStringLiteral("ocd:object_string_type"), QStringLiteral("3"));
				path->setTag(QStringLiteral("ocd:object_string"), QString::fromLatin1(object_string));
			}
			map.addObject(path);
		};
		add_path(line_symbol, { MapCoord(10.0, 10.0), MapCoord(40.0, 10.0) }, "\to100");                          // 0: layout line
		add_path(area_symbol, { MapCoord(10.0, 20.0), MapCoord(40.0, 20.0), MapCoord(40.0, 40.0) }, "\to50");    // 1: layout area
		auto* text = new TextObject(text_symbol);                                                               // 2: layout text
		text->setText(QStringLiteral("Probe text"));
		text->setAnchorPosition(MapCoordF(10.0, 50.0));
		text->setTag(QStringLiteral("ocd:object_string_type"), QStringLiteral("3"));
		text->setTag(QStringLiteral("ocd:object_string"), QStringLiteral("Arial\to50\ts12.0\tb1\ti0\ta1"));
		map.addObject(text);
		add_path(line_symbol, { MapCoord(60.0, 10.0), MapCoord(90.0, 10.0) }, nullptr);                         // 3: graphic line
		add_path(area_symbol, { MapCoord(60.0, 20.0), MapCoord(90.0, 20.0), MapCoord(90.0, 40.0) }, nullptr);   // 4: graphic area
		add_path(line_symbol, { MapCoord(60.0, 50.0), MapCoord(90.0, 50.0) }, nullptr);                         // 5: a line of the symbol

		QBuffer buffer;
		QVERIFY(buffer.open(QIODevice::ReadWrite));
		{
			auto exporter = format->makeExporter({}, &map, nullptr);
			exporter->setDevice(&buffer);
			QVERIFY(exporter->doExport());
		}
		auto bytes = buffer.data();
		auto* d = reinterpret_cast<uchar*>(bytes.data());
		// The object index: blocks of 256 entries of 40 bytes behind the place of the next block.
		std::vector<quint32> entries;
		for (auto block = qFromLittleEndian<quint32>(d + 12); block; block = qFromLittleEndian<quint32>(d + block))
		{
			for (int i = 0; i < 256; ++i)
			{
				auto const entry = block + 4 + 40 * quint32(i);
				if (qFromLittleEndian<quint32>(d + entry + 16))
					entries.push_back(entry);
			}
		}
		QCOMPARE(int(entries.size()), 6);
		auto const patch = [d, &entries](int k, int type, qint32 symbol, quint32 color, quint16 width) {
			auto const entry = entries[std::size_t(k)];
			auto const pos = qFromLittleEndian<quint32>(d + entry + 16);
			if (d[entry + 28] != type || d[pos + 4] != type)
				return false;
			qToLittleEndian<qint32>(symbol, d + entry + 24);
			qToLittleEndian<qint32>(symbol, d + pos);
			qToLittleEndian<quint32>(color, d + pos + 8);
			qToLittleEndian<quint16>(width, d + pos + 12);
			return true;
		};
		QVERIFY(patch(0, 2, ocd_layout_object_symbol, 0x00ffff00u, 100));
		QVERIFY(patch(1, 3, ocd_layout_object_symbol, 0x00ffff00u, 0));
		QVERIFY(patch(2, 4, ocd_layout_object_symbol, 0x00c94500u, 0));
		QVERIFY(patch(3, 2, ocd_graphic_object_symbol, 1, 21));   // the colour of number 1: brown
		QVERIFY(patch(4, 3, ocd_graphic_object_symbol, 2, 0));    // yellow

		Map loaded;
		QBuffer patched(&bytes);
		QVERIFY(patched.open(QIODevice::ReadOnly));
		auto importer = format->makeImporter({}, &loaded, nullptr);
		importer->setDevice(&patched);
		QVERIFY(importer->doImport());
		for (auto const& warning : importer->warnings())
			QVERIFY2(!warning.contains(QLatin1String("Unable")), qPrintable(warning));

		// The layout objects are in the layout part, each with a private symbol of its own.
		auto const layout_index = loaded.findPartIndexWithRole(MapPart::Role::Layout);
		QVERIFY(layout_index >= 0);
		auto const* layout = loaded.getPart(std::size_t(layout_index));
		QCOMPARE(layout->getNumObjects(), 3);
		std::set<const Symbol*> symbols;
		for (int i = 0; i < 3; ++i)
		{
			QVERIFY(layout->getObject(i)->getSymbol()->isPrivate());
			symbols.insert(layout->getObject(i)->getSymbol());
		}
		QCOMPARE(int(symbols.size()), 3);
		auto const* layout_line = layout->getObject(0)->getSymbol();
		QCOMPARE(layout_line->getType(), Symbol::Line);
		QCOMPARE(layout_line->asLine()->getLineWidth(), 1000);
		QCOMPARE(layout_line->asLine()->getColor()->getName(), QStringLiteral("Layout C0 M100 Y100 K0"));
		QCOMPARE(layout_line->asLine()->getColor()->getOpacity(), 1.0f);
		auto const* layout_area = layout->getObject(1)->getSymbol();
		QCOMPARE(layout_area->getType(), Symbol::Area);
		QCOMPARE(layout_area->asArea()->getColor()->getName(), QStringLiteral("Layout C0 M100 Y100 K0, 50 %"));
		QCOMPARE(layout_area->asArea()->getColor()->getOpacity(), 0.5f);
		QCOMPARE(layout_area->asArea()->getColor()->getCmyk().m, 1.0f);
		QVERIFY(layout->getObject(1)->asPath()->parts().front().isClosed());
		auto const* layout_text = layout->getObject(2);
		QCOMPARE(layout_text->getType(), Object::Text);
		QCOMPARE(layout_text->asText()->getText(), QStringLiteral("Probe text"));
		QCOMPARE(layout_text->asText()->getHorizontalAlignment(), TextObject::AlignHCenter);
		auto const* text_style = layout_text->getSymbol()->asText();
		QCOMPARE(text_style->getFontFamily(), QStringLiteral("Arial"));
		QCOMPARE(text_style->getFontSize(), 4.233);
		QVERIFY(text_style->isBold());
		QCOMPARE(text_style->getColor()->getName(), QStringLiteral("Layout C0 M27 Y79 K0, 50 %"));
		// The colours of the layout objects stand before the map's own.
		QCOMPARE(loaded.getNumColors(), 6);
		for (int i = 0; i < 3; ++i)
			QVERIFY2(loaded.getColor(i)->getName().startsWith(QLatin1String("Layout ")), qPrintable(loaded.getColor(i)->getName()));

		// The graphic objects stay in the map part, with private symbols in the map's colours.
		const Object* graphic_line = nullptr;
		const Object* graphic_area = nullptr;
		const Object* plain = nullptr;
		auto const* first = loaded.getPart(0);
		QCOMPARE(first->getNumObjects(), 3);
		for (int i = 0; i < first->getNumObjects(); ++i)
		{
			auto const* object = first->getObject(i);
			if (!object->getSymbol()->isPrivate())
				plain = object;
			else if (object->getSymbol()->getType() == Symbol::Line)
				graphic_line = object;
			else if (object->getSymbol()->getType() == Symbol::Area)
				graphic_area = object;
		}
		QVERIFY(graphic_line && graphic_area && plain);
		QCOMPARE(graphic_line->getSymbol()->asLine()->getLineWidth(), 210);
		QCOMPARE(graphic_line->getSymbol()->asLine()->getColor()->getName(), QStringLiteral("Brown"));
		QCOMPARE(graphic_area->getSymbol()->asArea()->getColor()->getName(), QStringLiteral("Yellow"));
		QCOMPARE(plain->getSymbol()->getNumberComponent(0), 101);
		QVERIFY(!loaded.getUndefinedLine()->isPrivate());
	}
	
	/// Written (1): which symbols can be written as objects without a symbol, and the fields of a layout object.
	void writeHelpers()
	{
		MapColor brown(QStringLiteral("Brown"), 0);
		OcdGraphicStyle style;
		auto line = makeOcdGraphicSymbol(2, &brown, 21, nullptr, QStringLiteral("Graphic object"));
		QVERIFY(isOcdGraphicSymbol(line.get(), &style));
		QCOMPARE(style.color, &brown);
		QCOMPARE(style.line_width, 21);
		line->asLine()->setCapStyle(LineSymbol::RoundCap);
		QVERIFY(isOcdGraphicSymbol(line.get()));
		line->asLine()->setDashed(true);
		QVERIFY(!isOcdGraphicSymbol(line.get()));
		line->asLine()->setDashed(false);
		line->setPrivate(false);
		QVERIFY(!isOcdGraphicSymbol(line.get()));
		auto area = makeOcdGraphicSymbol(3, &brown, 0, nullptr, QStringLiteral("Graphic object"));
		QVERIFY(isOcdGraphicSymbol(area.get(), &style));
		QCOMPARE(style.line_width, 0);
		area->asArea()->setNumFillPatterns(1);
		QVERIFY(!isOcdGraphicSymbol(area.get()));
		auto no_color = makeOcdGraphicSymbol(3, nullptr, 0, nullptr, QStringLiteral("Graphic object"));
		QVERIFY(!isOcdGraphicSymbol(no_color.get()));
		OcdLayoutStyle text_style;
		auto text = makeOcdGraphicSymbol(4, &brown, 0, &text_style, QStringLiteral("Text object"));
		QVERIFY(!isOcdGraphicSymbol(text.get()));
		QVERIFY(!isOcdGraphicSymbol(nullptr));
	
		QCOMPARE(ocdLayoutColorField(MapColorCmyk(0.0f, 1.0f, 1.0f, 0.0f)), 0x00ffff00u);
		QCOMPARE(ocdLayoutColorField(ocdLayoutCmyk(0x00c94500u)), 0x00c94500u);
		QCOMPARE(ocdLayoutColorField(ocdLayoutCmyk(0x80402010u)), 0x80402010u);
	
		OcdLayoutStyle half;
		half.opacity = 0.5;
		QCOMPARE(ocdLayoutObjectString(half, false), QStringLiteral("\to50"));
		half.font = QStringLiteral("Arial");
		half.size_pt = 11.9991;
		half.bold = true;
		half.alignment = 1;
		QCOMPARE(ocdLayoutObjectString(half, true), QStringLiteral("Arial\to50\ts12.0\tb1\ti0\ta1"));
		auto const read = parseOcdLayoutStyle(ocdLayoutObjectString(half, true));
		QCOMPARE(read.font, half.font);
		QCOMPARE(read.size_pt, 12.0);
		QVERIFY(read.bold && !read.italic);
	
		QCOMPARE(ocdLayoutListEntry(2, {}, 6220), QStringLiteral("Line object\tr0\ts1\tn6220"));
		QCOMPARE(ocdLayoutListEntry(3, {}, 6221), QStringLiteral("Area object\tr0\ts1\tn6221"));
		QCOMPARE(ocdLayoutListEntry(4, QStringLiteral("Probe text"), 6222), QStringLiteral("Text object (Probe text)\tr0\ts1\tn6222"));
		QCOMPARE(ocdLayoutListEntry(4, QStringLiteral("One\tline\nand another"), 7), QStringLiteral("Text object (One line)\tr0\ts1\tn7"));
	}
	
	/// Written (2): FOOM writes the objects of private plain symbols as OCD objects without a symbol.
	void exportToFile()
	{
		auto const* format = FileFormats.findFormat("OCD2018");
		QVERIFY(format);
		Map map;
		map.setScaleDenominator(10000);
		auto* black = new MapColor(QStringLiteral("Black"), 0);
		black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
		map.addColor(black, 0);
		auto* brown = new MapColor(QStringLiteral("Brown"), 1);
		brown->setCmyk({ 0.0f, 0.56f, 1.0f, 0.18f });
		map.addColor(brown, 1);
		auto* yellow = new MapColor(QStringLiteral("Yellow"), 2);
		yellow->setCmyk({ 0.0f, 0.27f, 0.79f, 0.0f });
		map.addColor(yellow, 2);
		auto* line_symbol = new LineSymbol();
		line_symbol->setNumberComponent(0, 101);
		line_symbol->setColor(black);
		line_symbol->setLineWidth(0.14);
		map.addSymbol(line_symbol, 0);
	
		auto const add_symbol = [&map](std::unique_ptr<Symbol> symbol) {
			auto* added = symbol.release();
			map.addSymbol(added, map.getNumSymbols());
			return added;
		};
		auto const add_path = [&map](const Symbol* symbol, std::vector<MapCoord> coords, int part) {
			auto* path = new PathObject(symbol);
			for (auto const& coord : coords)
				path->addCoordinate(coord);
			if (symbol->getType() == Symbol::Area)
				path->closeAllParts();
			map.addObject(path, part);
			return path;
		};
		// The map part: a graphic line and a graphic area, a line of a symbol, and a private line which is dashed.
		auto* graphic_line = add_symbol(makeOcdGraphicSymbol(2, brown, 21, nullptr, QStringLiteral("Graphic object")));
		auto* graphic_area = add_symbol(makeOcdGraphicSymbol(3, yellow, 0, nullptr, QStringLiteral("Graphic object")));
		auto* dashed = add_symbol(makeOcdGraphicSymbol(2, black, 30, nullptr, QStringLiteral("Dashed")));
		dashed->asLine()->setDashed(true);
		add_path(graphic_line, { MapCoord(60.0, 10.0), MapCoord(90.0, 10.0) }, 0);                         // 1
		add_path(graphic_area, { MapCoord(60.0, 20.0), MapCoord(90.0, 20.0), MapCoord(90.0, 40.0) }, 0);   // 2
		add_path(line_symbol, { MapCoord(60.0, 50.0), MapCoord(90.0, 50.0) }, 0);                          // 3
		add_path(dashed, { MapCoord(60.0, 60.0), MapCoord(90.0, 60.0) }, 0);                               // 4
		// The layout part: a line, an area of half opacity and a text.
		auto* layout = new MapPart(QStringLiteral("Layout"), &map);
		layout->setRole(MapPart::Role::Layout);
		map.addPart(layout, 1);
		auto const red = MapColorCmyk(0.0f, 1.0f, 1.0f, 0.0f);
		auto* layout_line = add_symbol(makeOcdGraphicSymbol(2, ocdLayoutColor(map, red, 1.0), 100, nullptr, QStringLiteral("Line object")));
		auto* layout_area = add_symbol(makeOcdGraphicSymbol(3, ocdLayoutColor(map, red, 0.5), 0, nullptr, QStringLiteral("Area object")));
		OcdLayoutStyle text_style;
		text_style.font = QStringLiteral("Arial");
		text_style.size_pt = 12.0;
		text_style.bold = true;
		auto* layout_text = add_symbol(makeOcdGraphicSymbol(4, ocdLayoutColor(map, ocdLayoutCmyk(0x00c94500u), 0.5), 0, &text_style, QStringLiteral("Text object")));
		add_path(layout_line, { MapCoord(10.0, 10.0), MapCoord(40.0, 10.0) }, 1);                          // 5
		add_path(layout_area, { MapCoord(10.0, 20.0), MapCoord(40.0, 20.0), MapCoord(40.0, 40.0) }, 1);    // 6
		auto* text = new TextObject(layout_text);                                                          // 7
		text->setText(QStringLiteral("Probe text"));
		text->setAnchorPosition(MapCoordF(10.0, 50.0));
		text->setHorizontalAlignment(TextObject::AlignHCenter);
		text->setVerticalAlignment(TextObject::AlignBaseline);
		map.addObject(text, 1);
		// A sketch part: its stroke goes into the sketch block (test/ocd_sketch_t.cpp), with no object and no symbol.
		auto* sketch = new MapPart(QStringLiteral("Sketch"), &map);
		sketch->setRole(MapPart::Role::Sketch);
		map.addPart(sketch, 2);
		auto* pen = add_symbol(makeOcdGraphicSymbol(2, black, 35, nullptr, QStringLiteral("Sketch black 0.35")));
		add_path(pen, { MapCoord(10.0, 70.0), MapCoord(40.0, 70.0) }, 2);                                  // 8
	
		QBuffer buffer;
		QVERIFY(buffer.open(QIODevice::ReadWrite));
		{
			auto exporter = format->makeExporter({}, &map, nullptr);
			exporter->setDevice(&buffer);
			QVERIFY(exporter->doExport());
		}
		auto const bytes = buffer.data();
		auto const* d = reinterpret_cast<const uchar*>(bytes.constData());
	
		// The symbols: the line 101 and the dashed line; the six others are not written.
		auto symbols_written = 0;
		for (auto block = qFromLittleEndian<quint32>(d + 8); block; block = qFromLittleEndian<quint32>(d + block))
		{
			for (int i = 0; i < 256; ++i)
			{
				if (qFromLittleEndian<quint32>(d + block + 4 + 4 * quint32(i)))
					++symbols_written;
			}
		}
		QCOMPARE(symbols_written, 2);
	
		// The object index: blocks of 256 entries of 40 bytes behind the place of the next block.
		std::vector<quint32> entries;
		for (auto block = qFromLittleEndian<quint32>(d + 12); block; block = qFromLittleEndian<quint32>(d + block))
		{
			for (int i = 0; i < 256; ++i)
			{
				auto const entry = block + 4 + 40 * quint32(i);
				if (qFromLittleEndian<quint32>(d + entry + 16))
					entries.push_back(entry);
			}
		}
		QCOMPARE(int(entries.size()), 7);
		struct Record
		{
			qint32 entry_symbol;
			int type;
			int entry_color;
			qint32 symbol;
			quint32 color;
			int width;
			int string_type;
			QString string;
		};
		auto const record = [d, &entries](int k) {
			auto const entry = entries[std::size_t(k)];
			auto const pos = qFromLittleEndian<quint32>(d + entry + 16);
			Record r;
			r.entry_symbol = qFromLittleEndian<qint32>(d + entry + 24);
			r.type = d[entry + 28];
			r.entry_color = qFromLittleEndian<quint16>(d + entry + 32);
			r.symbol = qFromLittleEndian<qint32>(d + pos);
			r.color = qFromLittleEndian<quint32>(d + pos + 8);
			r.width = qFromLittleEndian<quint16>(d + pos + 12);
			auto const items = qFromLittleEndian<quint32>(d + pos + 44);
			auto const text = qFromLittleEndian<quint16>(d + pos + 48);
			auto const string_length = qFromLittleEndian<quint16>(d + pos + 50);
			r.string_type = d[pos + 54];
			if (string_length)
				r.string = QString::fromUtf16(reinterpret_cast<const char16_t*>(d + pos + 56 + 8 * (items + text)));
			return r;
		};
		// The graphic objects: the number of the colour in both colour fields, the width of the line.
		auto r = record(0);
		QCOMPARE(r.entry_symbol, ocd_graphic_object_symbol);
		QCOMPARE(r.symbol, ocd_graphic_object_symbol);
		QCOMPARE(r.type, 2);
		QCOMPARE(r.color, quint32(map.findColorIndex(brown)));
		QCOMPARE(r.entry_color, map.findColorIndex(brown));
		QCOMPARE(r.width, 21);
		QVERIFY(r.string.isEmpty());
		r = record(1);
		QCOMPARE(r.entry_symbol, ocd_graphic_object_symbol);
		QCOMPARE(r.symbol, ocd_graphic_object_symbol);
		QCOMPARE(r.type, 3);
		QCOMPARE(r.color, quint32(map.findColorIndex(yellow)));
		QCOMPARE(r.width, 0);
		// The objects of symbols which are written.
		r = record(2);
		QCOMPARE(r.symbol, 101000);
		r = record(3);
		QVERIFY(r.symbol != ocd_graphic_object_symbol && r.symbol != ocd_layout_object_symbol);
		QCOMPARE(r.entry_symbol, r.symbol);
		// The layout objects: the colour as four bytes, 65535 in the index, the style in the object string.
		r = record(4);
		QCOMPARE(r.entry_symbol, ocd_layout_object_symbol);
		QCOMPARE(r.symbol, ocd_layout_object_symbol);
		QCOMPARE(r.type, 2);
		QCOMPARE(r.color, 0x00ffff00u);
		QCOMPARE(r.entry_color, 65535);
		QCOMPARE(r.width, 100);
		QCOMPARE(r.string_type, ocd_layout_object_string_type);
		QCOMPARE(r.string, QStringLiteral("\to100"));
		r = record(5);
		QCOMPARE(r.symbol, ocd_layout_object_symbol);
		QCOMPARE(r.type, 3);
		QCOMPARE(r.color, 0x00ffff00u);
		QCOMPARE(r.width, 0);
		QCOMPARE(r.string, QStringLiteral("\to50"));
		r = record(6);
		QCOMPARE(r.entry_symbol, ocd_layout_object_symbol);
		QCOMPARE(r.symbol, ocd_layout_object_symbol);
		QCOMPARE(r.type, 4);
		QCOMPARE(r.color, 0x00c94500u);
		QCOMPARE(r.entry_color, 65535);
		QCOMPARE(r.string_type, ocd_layout_object_string_type);
		QCOMPARE(r.string, QStringLiteral("Arial\to50\ts12.0\tb1\ti0\ta1"));
	
		// The file's list of layout objects: a string for each, with the object's place counted from 1.
		QStringList list;
		std::vector<quint32> list_places;
		for (auto block = qFromLittleEndian<quint32>(d + 32); block; block = qFromLittleEndian<quint32>(d + block))
		{
			for (int i = 0; i < 256; ++i)
			{
				auto const entry = block + 4 + 16 * quint32(i);
				auto const pos = qFromLittleEndian<quint32>(d + entry);
				if (pos && qFromLittleEndian<qint32>(d + entry + 8) == ocd_layout_image_string_type)
				{
					list.push_back(QString::fromUtf8(bytes.constData() + pos));
					list_places.push_back(qFromLittleEndian<quint32>(d + entry + 12));
				}
			}
		}
		list.sort();
		QCOMPARE(list, QStringList({ QStringLiteral("Area object\tr0\ts1\tn6"), QStringLiteral("Line object\tr0\ts1\tn5"),
		                             QStringLiteral("Text object (Probe text)\tr0\ts1\tn7") }));
		std::sort(list_places.begin(), list_places.end());
		QCOMPARE(list_places, std::vector<quint32>({ 5, 6, 7 }));
	
		// Read again, the objects are what they were.
		Map loaded;
		QBuffer written(&buffer.buffer());
		QVERIFY(written.open(QIODevice::ReadOnly));
		auto importer = format->makeImporter({}, &loaded, nullptr);
		importer->setDevice(&written);
		QVERIFY(importer->doImport());
		for (auto const& warning : importer->warnings())
			QVERIFY2(!warning.contains(QLatin1String("Unable")), qPrintable(warning));
		auto const layout_index = loaded.findPartIndexWithRole(MapPart::Role::Layout);
		QVERIFY(layout_index >= 0);
		auto const* read_layout = loaded.getPart(std::size_t(layout_index));
		QCOMPARE(read_layout->getNumObjects(), 3);
		auto const* read_line = read_layout->getObject(0)->getSymbol();
		QVERIFY(read_line->isPrivate());
		QCOMPARE(read_line->getType(), Symbol::Line);
		QCOMPARE(read_line->asLine()->getLineWidth(), 1000);
		QCOMPARE(read_line->asLine()->getColor()->getName(), QStringLiteral("Layout C0 M100 Y100 K0"));
		auto const* read_area = read_layout->getObject(1)->getSymbol();
		QCOMPARE(read_area->getType(), Symbol::Area);
		QCOMPARE(read_area->asArea()->getColor()->getName(), QStringLiteral("Layout C0 M100 Y100 K0, 50 %"));
		auto const* read_text = read_layout->getObject(2);
		QCOMPARE(read_text->getType(), Object::Text);
		QCOMPARE(read_text->asText()->getText(), QStringLiteral("Probe text"));
		QCOMPARE(read_text->asText()->getHorizontalAlignment(), TextObject::AlignHCenter);
		QCOMPARE(read_text->asText()->getAnchorCoordF(), MapCoordF(10.0, 50.0));
		QCOMPARE(read_text->getSymbol()->asText()->getFontFamily(), QStringLiteral("Arial"));
		QCOMPARE(read_text->getSymbol()->asText()->getFontSize(), 4.233);
		QVERIFY(read_text->getSymbol()->asText()->isBold());
		QCOMPARE(read_text->getSymbol()->asText()->getColor()->getName(), QStringLiteral("Layout C0 M27 Y79 K0, 50 %"));
		auto graphic_lines = 0;
		auto graphic_areas = 0;
		auto const* first = loaded.getPart(0);
		for (int i = 0; i < first->getNumObjects(); ++i)
		{
			auto const* symbol = first->getObject(i)->getSymbol();
			if (symbol->getName() != QLatin1String("Graphic object"))
				continue;
			QVERIFY(symbol->isPrivate());
			if (symbol->getType() == Symbol::Line)
			{
				++graphic_lines;
				QCOMPARE(symbol->asLine()->getLineWidth(), 210);
				QCOMPARE(symbol->asLine()->getColor()->getName(), QStringLiteral("Brown"));
			}
			else if (symbol->getType() == Symbol::Area)
			{
				++graphic_areas;
				QCOMPARE(symbol->asArea()->getColor()->getName(), QStringLiteral("Yellow"));
			}
		}
		QCOMPARE(graphic_lines, 1);
		QCOMPARE(graphic_areas, 1);
	}
};


QTEST_MAIN(OcdGraphicObjectTest)
#include "ocd_graphic_object_t.moc"  // IWYU pragma: keep
