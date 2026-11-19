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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/xml_file_format.h"

using namespace OpenOrienteering;


/**
 * Tests the text symbol properties which symbols of OCD files need.
 *
 * See the design notes, "Text symbol properties".
 */
class TextSymbolTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void wordSpacingTest();
	void fontWeightTest();
	void indentTest();
	void justifyTest();
	void boxFramingTest();
	void frameSymbolTest();
	void xmlRoundTripTest();
	void ocdRoundTripTest();

private:
	static MapColor* addColor(Map& map, const QString& name, const MapColorCmyk& cmyk);
	static TextSymbol* makeSymbol(Map& map, const MapColor* color);
	static TextObject* makeBoxText(Map& map, const TextSymbol* symbol, const QString& text, double width, TextObject::HorizontalAlignment h_align);
	static QImage render(Map& map, double width_mm, double height_mm);
	static QRgb pixelAt(const QImage& image, double x_mm, double y_mm);
	static bool noPropertyWarning(const std::vector<QString>& warnings);

	static constexpr double tolerance = 0.01;  // mm, coordinate resolution
};


void TextSymbolTest::initTestCase()
{
	doStaticInitializations();
}

MapColor* TextSymbolTest::addColor(Map& map, const QString& name, const MapColorCmyk& cmyk)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setCmyk(cmyk);
	map.addColor(color, map.getNumColors());
	return color;
}

TextSymbol* TextSymbolTest::makeSymbol(Map& map, const MapColor* color)
{
	auto* symbol = new TextSymbol();
	symbol->setColor(color);
	symbol->setFontFamily(QStringLiteral("Arial"));
	symbol->setFontSize(qRound(12.0 / 72.0 * 25.4 * 1000.0));  // 12 pt in 1/1000 mm
	symbol->setNumberComponent(0, 701 + map.getNumSymbols());  // OCD needs a non-zero number
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

TextObject* TextSymbolTest::makeBoxText(Map& map, const TextSymbol* symbol, const QString& text, double width, TextObject::HorizontalAlignment h_align)
{
	auto* object = new TextObject(symbol);
	object->setText(text);
	object->setBox(MapCoord(20.0, 20.0).nativeX(), MapCoord(20.0, 20.0).nativeY(), width, 40.0);
	object->setHorizontalAlignment(h_align);
	object->setVerticalAlignment(TextObject::AlignTop);
	map.addObject(object);
	return object;
}

QImage TextSymbolTest::render(Map& map, double width_mm, double height_mm)
{
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setResolution(300);
	printer.setPrintArea(QRectF(0.0, 0.0, width_mm, height_mm));
	auto const pixel_per_mm = 300 / 25.4;
	QImage image(qRound(width_mm * pixel_per_mm), qRound(height_mm * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image, RenderConfig::NoOptions);
	}
	return image;
}

QRgb TextSymbolTest::pixelAt(const QImage& image, double x_mm, double y_mm)
{
	auto const pixel_per_mm = 300 / 25.4;
	return image.pixel(qRound(x_mm * pixel_per_mm), qRound(y_mm * pixel_per_mm));
}

bool TextSymbolTest::noPropertyWarning(const std::vector<QString>& warnings)
{
	static char const* const phrases[] = { "weight", "Justified", "word spacing", "indents", "framing (mode", "point symbol" };
	auto ok = true;
	for (auto const& warning : warnings)
	{
		for (auto const* phrase : phrases)
		{
			if (warning.contains(QLatin1String(phrase)))
			{
				qWarning("property warning: %s", qPrintable(warning));
				ok = false;
			}
		}
	}
	return ok;
}


void TextSymbolTest::wordSpacingTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* normal = makeSymbol(map, black);
	auto* wide = makeSymbol(map, black);
	QCOMPARE(normal->getWordSpacing(), 1.0);
	wide->setWordSpacing(2.0);

	auto const scaling = normal->calculateInternalScaling();
	auto const space = normal->getFontMetrics().horizontalAdvance(QStringLiteral(" ")) / scaling;
	QVERIFY(space > 0.5);

	auto width = [](const TextSymbol* symbol, const QString& text) {
		TextObject object(symbol);
		object.setText(text);
		object.setAnchorPosition(MapCoord(0.0, 0.0));
		object.prepareLineInfos();
		return object.getLineInfo(0)->width / symbol->calculateInternalScaling();
	};
	auto const a_b = QStringLiteral("a b");
	QVERIFY(std::abs(width(wide, a_b) - width(normal, a_b) - space) < tolerance);
	QVERIFY(std::abs(width(wide, QStringLiteral("ab")) - width(normal, QStringLiteral("ab"))) < tolerance);
}

void TextSymbolTest::fontWeightTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* symbol = makeSymbol(map, black);
	QCOMPARE(symbol->getFontWeight(), 400);
	QVERIFY(!symbol->getQFont().bold());

	symbol->setFontWeight(700);
	QVERIFY(symbol->getQFont().bold());

	symbol->setFontWeight(300);
	QCOMPARE(symbol->getFontWeight(), 300);
	QVERIFY(symbol->getQFont().weight() < QFont::Normal);
	QVERIFY(!symbol->isBold());

	symbol->setFontWeight(0);
	symbol->setBold(true);
	QCOMPARE(symbol->getFontWeight(), 700);
	QVERIFY(symbol->getQFont().bold());
}

void TextSymbolTest::indentTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* symbol = makeSymbol(map, black);
	symbol->setIndents(5000, 2000);
	auto const scaling = symbol->calculateInternalScaling();
	auto* object = makeBoxText(map, symbol, QStringLiteral("aaaa bbbb cccc dddd eeee ffff gggg\nhh"), 30.0, TextObject::AlignLeft);
	object->prepareLineInfos();
	QVERIFY(object->getNumLines() >= 3);
	auto const left_edge = -15.0;  // text coordinates in mm, box centred on the anchor
	auto start = [object, scaling, left_edge](int line) {
		return object->getLineInfo(line)->part_infos.front().part_x / scaling - left_edge;
	};
	QVERIFY2(std::abs(start(0) - 5.0) < tolerance, qPrintable(QString::number(start(0))));
	QVERIFY2(std::abs(start(1) - 2.0) < tolerance, qPrintable(QString::number(start(1))));
	auto const last = object->getNumLines() - 1;
	QCOMPARE(object->getLineInfo(last)->part_infos.front().part_text, QStringLiteral("hh"));
	QVERIFY2(std::abs(start(last) - 5.0) < tolerance, qPrintable(QString::number(start(last))));

	// Centred text ignores the indents.
	object->setHorizontalAlignment(TextObject::AlignHCenter);
	object->prepareLineInfos();
	auto const& centred = *object->getLineInfo(0);
	QVERIFY(std::abs(centred.line_x + centred.width / 2) < tolerance * scaling);
}

void TextSymbolTest::justifyTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* symbol = makeSymbol(map, black);
	auto const scaling = symbol->calculateInternalScaling();
	auto* object = makeBoxText(map, symbol, QStringLiteral("aaaa bbbb cccc dddd eeee ffff gggg\nhh"), 30.0, TextObject::AlignJustify);
	object->prepareLineInfos();
	QVERIFY(object->getNumLines() >= 3);
	auto const left_edge = -15.0 * scaling;
	auto const right_edge = 15.0 * scaling;
	auto justified_lines = 0;
	for (int i = 0; i < object->getNumLines(); ++i)
	{
		auto const& line = *object->getLineInfo(i);
		QVERIFY(!line.part_infos.empty());
		QVERIFY(std::abs(line.part_infos.front().part_x - left_edge) < tolerance * scaling);
		if (line.paragraph_end)
			continue;
		++justified_lines;
		auto const words = object->getText().mid(line.start_index, line.end_index - line.start_index).split(QLatin1Char(' '));
		QCOMPARE(int(line.part_infos.size()), words.size());
		auto const& last = line.part_infos.back();
		QVERIFY2(std::abs(last.part_x + last.width - right_edge) < tolerance * scaling,
		         qPrintable(QString::number((last.part_x + last.width - right_edge) / scaling)));
		for (std::size_t j = 0; j < line.part_infos.size(); ++j)
			QCOMPARE(line.part_infos[j].part_text, words.at(int(j)));
	}
	QVERIFY(justified_lines >= 2);
	QVERIFY(object->getLineInfo(object->getNumLines() - 1)->paragraph_end);
}

void TextSymbolTest::boxFramingTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* blue = addColor(map, QStringLiteral("Blue"), MapColorCmyk(1.0f, 1.0f, 0.0f, 0.0f));
	auto* symbol = makeSymbol(map, black);
	symbol->setFraming(true);
	symbol->setFramingColor(blue);
	symbol->setFramingMode(TextSymbol::BoxFraming);
	symbol->setFramingBox(2000, 1000, 1500, 500);
	auto* object = new TextObject(symbol);
	object->setText(QStringLiteral("Box"));
	object->setAnchorPosition(MapCoord(20.0, 20.0));
	object->setHorizontalAlignment(TextObject::AlignLeft);
	object->setVerticalAlignment(TextObject::AlignBaseline);
	map.addObject(object);
	auto const ascent = symbol->getFontMetrics().ascent() / symbol->calculateInternalScaling();
	QVERIFY(ascent > 2.0 && ascent < 4.0);

	auto const image = render(map, 60.0, 40.0);
	auto const blue_rgb = QRgb(*blue);
	QVERIFY(blue_rgb != qRgb(255, 255, 255));
	QCOMPARE(pixelAt(image, 19.0, 20.0 - ascent / 2), blue_rgb);
	QCOMPARE(pixelAt(image, 20.0 - 3.0, 20.0 - ascent / 2), qRgb(255, 255, 255));
	QCOMPARE(pixelAt(image, 20.0 + 1.0, 20.0 - ascent - 0.5), blue_rgb);
	QCOMPARE(pixelAt(image, 20.0 + 1.0, 20.0 - ascent - 2.0), qRgb(255, 255, 255));
}

void TextSymbolTest::frameSymbolTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* red = addColor(map, QStringLiteral("Red"), MapColorCmyk(0.0f, 1.0f, 1.0f, 0.0f));
	auto* dot = new PointSymbol();
	dot->setInnerColor(red);
	dot->setInnerRadius(4000);
	dot->setNumberComponent(0, 101);
	map.addSymbol(dot, 0);
	auto* symbol = makeSymbol(map, black);
	symbol->setFrameSymbol(dot);
	QVERIFY(symbol->containsSymbol(dot));
	auto* object = new TextObject(symbol);
	object->setText(QStringLiteral("I"));
	object->setAnchorPosition(MapCoord(20.0, 20.0));
	object->setHorizontalAlignment(TextObject::AlignHCenter);
	object->setVerticalAlignment(TextObject::AlignVCenter);
	map.addObject(object);

	auto const image = render(map, 60.0, 40.0);
	auto const red_rgb = QRgb(*red);
	QCOMPARE(pixelAt(image, 22.5, 20.0), red_rgb);
	QCOMPARE(pixelAt(image, 25.0, 20.0), qRgb(255, 255, 255));

	map.deleteSymbol(map.findSymbolIndex(dot));
	QVERIFY(!symbol->getFrameSymbol());
	QVERIFY(!symbol->containsSymbol(dot));
}

void TextSymbolTest::xmlRoundTripTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* dot = new PointSymbol();
	dot->setInnerColor(black);
	dot->setInnerRadius(1000);
	dot->setNumberComponent(0, 101);
	map.addSymbol(dot, 0);
	auto* symbol = makeSymbol(map, black);
	symbol->setFontWeight(300);
	symbol->setWordSpacing(1.5);
	symbol->setIndents(5000, 2000);
	symbol->setFraming(true);
	symbol->setFramingColor(black);
	symbol->setFramingMode(TextSymbol::BoxFraming);
	symbol->setFramingBox(2000, 1000, 1500, 500);
	symbol->setFrameSymbol(dot);
	makeBoxText(map, symbol, QStringLiteral("Justified text"), 30.0, TextObject::AlignJustify);

	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto const* format = FileFormats.findFormat("XML");
	QVERIFY(format);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());

	QCOMPARE(reloaded.getNumSymbols(), 2);
	auto const* loaded = reloaded.getSymbol(1)->asText();
	QVERIFY(loaded);
	QCOMPARE(loaded->getFontWeight(), 300);
	QCOMPARE(loaded->getWordSpacing(), 1.5);
	QCOMPARE(loaded->getIndentFirst(), 5000);
	QCOMPARE(loaded->getIndentOther(), 2000);
	QVERIFY(loaded->usesFraming());
	QCOMPARE(loaded->getFramingMode(), int(TextSymbol::BoxFraming));
	QCOMPARE(loaded->getFramingBoxLeft(), 2000);
	QCOMPARE(loaded->getFramingBoxRight(), 1000);
	QCOMPARE(loaded->getFramingBoxTop(), 1500);
	QCOMPARE(loaded->getFramingBoxBottom(), 500);
	QCOMPARE(loaded->getFrameSymbol(), reloaded.getSymbol(0));
	QVERIFY(loaded->equals(symbol));
	QCOMPARE(reloaded.getNumObjects(), 1);
	QCOMPARE(reloaded.getPart(0)->getObject(0)->asText()->getHorizontalAlignment(), TextObject::AlignJustify);

	// Without the properties the attributes are absent, as in upstream files.
	Map plain_map;
	auto* plain_black = addColor(plain_map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	makeSymbol(plain_map, plain_black);
	QBuffer plain_buffer;
	QVERIFY(plain_buffer.open(QIODevice::ReadWrite));
	auto plain_exporter = format->makeExporter({}, &plain_map, nullptr);
	plain_exporter->setDevice(&plain_buffer);
	QVERIFY(plain_exporter->doExport());
	auto const xml = QString::fromUtf8(plain_buffer.data());
	for (auto const* attribute : { "weight", "word_spacing", "indent_first", "indent_other", "box_left", "frame_symbol" })
		QVERIFY2(!xml.contains(QLatin1String(attribute)), attribute);
}

void TextSymbolTest::ocdRoundTripTest()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	auto* dot = new PointSymbol();
	dot->setInnerColor(black);
	dot->setInnerRadius(1000);
	dot->setNumberComponent(0, 101);
	map.addSymbol(dot, 0);
	auto* symbol = makeSymbol(map, black);
	symbol->setFontWeight(300);
	symbol->setWordSpacing(1.5);
	symbol->setIndents(5000, 2000);
	symbol->setFraming(true);
	symbol->setFramingColor(black);
	symbol->setFramingMode(TextSymbol::BoxFraming);
	symbol->setFramingBox(2000, 1000, 1500, 500);
	symbol->setFrameSymbol(dot);
	makeBoxText(map, symbol, QStringLiteral("Justified text"), 30.0, TextObject::AlignJustify);
	auto* top = new TextObject(symbol);
	top->setText(QStringLiteral("Top"));
	top->setAnchorPosition(MapCoord(10.0, 10.0));
	top->setHorizontalAlignment(TextObject::AlignLeft);
	top->setVerticalAlignment(TextObject::AlignTop);
	map.addObject(top);
	auto* middle = new TextObject(symbol);
	middle->setText(QStringLiteral("Middle"));
	middle->setAnchorPosition(MapCoord(10.0, 30.0));
	middle->setHorizontalAlignment(TextObject::AlignRight);
	middle->setVerticalAlignment(TextObject::AlignVCenter);
	map.addObject(middle);

	auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(12));
	QVERIFY(format);
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	for (auto const& warning : exporter->warnings())
		qInfo("export warning: %s", qPrintable(warning));
	QVERIFY(noPropertyWarning(exporter->warnings()));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	for (auto const& warning : importer->warnings())
		qInfo("import warning: %s", qPrintable(warning));
	QVERIFY(noPropertyWarning(importer->warnings()));

	const TextSymbol* loaded = nullptr;
	const PointSymbol* loaded_dot = nullptr;
	for (int i = 0; i < reloaded.getNumSymbols(); ++i)
	{
		auto const* s = reloaded.getSymbol(i);
		if (s->getType() == Symbol::Point)
			loaded_dot = s->asPoint();
		else if (s->getType() == Symbol::Text && s->getNumberComponent(0) == symbol->getNumberComponent(0) && s->getNumberComponent(1) <= 0)
			loaded = s->asText();
	}
	QVERIFY(loaded);
	QVERIFY(loaded_dot);
	QCOMPARE(loaded->getFontWeight(), 300);
	QVERIFY(std::abs(loaded->getWordSpacing() - 1.5) < 0.001);
	QCOMPARE(loaded->getIndentFirst(), 5000);
	QCOMPARE(loaded->getIndentOther(), 2000);
	QVERIFY(loaded->usesFraming());
	QCOMPARE(loaded->getFramingMode(), int(TextSymbol::BoxFraming));
	QCOMPARE(loaded->getFramingBoxLeft(), 2000);
	QCOMPARE(loaded->getFramingBoxRight(), 1000);
	QCOMPARE(loaded->getFramingBoxTop(), 1500);
	QCOMPARE(loaded->getFramingBoxBottom(), 500);
	QCOMPARE(loaded->getFrameSymbol(), loaded_dot);

	QCOMPARE(reloaded.getNumObjects(), 3);
	auto found_justified = 0;
	auto found_top = 0;
	auto found_middle = 0;
	for (int i = 0; i < reloaded.getPart(0)->getNumObjects(); ++i)
	{
		auto const* object = reloaded.getPart(0)->getObject(i)->asText();
		QVERIFY(object);
		auto const* object_symbol = object->getSymbol()->asText();
		QCOMPARE(object_symbol->getFontWeight(), 300);
		if (object->getText() == QLatin1String("Justified text"))
		{
			QCOMPARE(object->getHorizontalAlignment(), TextObject::AlignJustify);
			++found_justified;
		}
		else if (object->getText() == QLatin1String("Top"))
		{
			QCOMPARE(object->getHorizontalAlignment(), TextObject::AlignLeft);
			QCOMPARE(object->getVerticalAlignment(), TextObject::AlignTop);
			++found_top;
		}
		else if (object->getText() == QLatin1String("Middle"))
		{
			QCOMPARE(object->getHorizontalAlignment(), TextObject::AlignRight);
			QCOMPARE(object->getVerticalAlignment(), TextObject::AlignVCenter);
			++found_middle;
		}
	}
	QCOMPARE(found_justified, 1);
	QCOMPARE(found_top, 1);
	QCOMPARE(found_middle, 1);
}


QTEST_MAIN(TextSymbolTest)
#include "text_symbol_t.moc"  // IWYU pragma: keep
