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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "check/issue.h"
#include "check/map_check.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/path_text_layout.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/xml_file_format.h"
#include "image-difference.h"

using namespace OpenOrienteering;


/**
 * Tests text on a path.
 *
 * See the design notes, "Text on a path".
 */
class PathTextTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void layoutTest();
	void heightTest();
	void renderTest();
	void xmlRoundTripTest();
	void ocdRoundTripTest();
	void mapCheckTest();
	
private:
	static TextSymbol* makeSymbol(Map& map, const MapColor* color, bool follows_path, int alignment);
	static TextObject* makePathText(Map& map, const TextSymbol* symbol, const QString& text, const MapCoordVector& path);
	static QImage render(Map& map);
};


void PathTextTest::initTestCase()
{
	doStaticInitializations();
}

TextSymbol* PathTextTest::makeSymbol(Map& map, const MapColor* color, bool follows_path, int alignment)
{
	auto* symbol = new TextSymbol();
	symbol->setColor(color);
	symbol->setFontFamily(QStringLiteral("Arial"));
	symbol->setFontSize(qRound(12.0 / 72.0 * 25.4 * 1000.0));  // 12 pt in 1/1000 mm
	symbol->setFollowsPath(follows_path);
	symbol->setPathAlignment(alignment);
	symbol->setNumberComponent(0, 701 + map.getNumSymbols());  // OCD needs a non-zero number
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

TextObject* PathTextTest::makePathText(Map& map, const TextSymbol* symbol, const QString& text, const MapCoordVector& path)
{
	auto* object = new TextObject(symbol);
	object->setText(text);
	object->setAnchorPath(path);
	map.addObject(object);
	return object;
}

QImage PathTextTest::render(Map& map)
{
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setResolution(300);
	printer.setPrintArea(QRectF(0.0, 0.0, 100.0, 40.0));
	auto const pixel_per_mm = 300 / 25.4;
	QImage image(qRound(100.0 * pixel_per_mm), qRound(40.0 * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image, RenderConfig::NoOptions);
	}
	return image;
}

void PathTextTest::layoutTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	map.addColor(black, 0);
	auto* symbol = makeSymbol(map, black, true, 0);
	auto const text = QStringLiteral("Path text 123");
	auto* straight = makePathText(map, symbol, text, { MapCoord(0.0, 0.0), MapCoord(60.0, 0.0) });
	QVERIFY(straight->followsPath());
	QVERIFY(straight->hasSingleAnchor());
	auto layout = symbol->layoutPathText(straight);
	QVERIFY(!layout.overflow);
	QCOMPARE(layout.path_length, 60.0);
	QVERIFY(layout.text_advance > 10.0 && layout.text_advance < 60.0);
	QCOMPARE(layout.glyph_centres.size(), std::size_t(text.size()));
	auto previous_x = -1.0;
	for (auto const& centre : layout.glyph_centres)
	{
		QVERIFY(std::abs(centre.y()) < 0.01);
		QVERIFY(centre.x() > previous_x);
		previous_x = centre.x();
	}
	QVERIFY(layout.glyph_centres.front().x() > 0.0 && layout.glyph_centres.front().x() < layout.text_advance / text.size());
	QVERIFY(!layout.outline.isEmpty());
	
	auto* overflowing = makePathText(map, symbol, text, { MapCoord(0.0, 10.0), MapCoord(10.0, 10.0) });
	QVERIFY(symbol->layoutPathText(overflowing).overflow);
	
	// Digits have equal advances, so the glyph centres of "12321" are symmetric.
	auto* centred_symbol = makeSymbol(map, black, true, 1);
	auto* centred = makePathText(map, centred_symbol, QStringLiteral("12321"), { MapCoord(0.0, 20.0), MapCoord(60.0, 20.0) });
	auto const centred_layout = centred_symbol->layoutPathText(centred);
	auto const& centres = centred_layout.glyph_centres;
	QCOMPARE(centres.size(), std::size_t(5));
	QVERIFY(std::abs((centres.front().x() + centres.back().x()) / 2 - 30.0) < 0.01);
	
	auto curve_start = MapCoord(0.0, 40.0);
	curve_start.setCurveStart(true);
	MapCoordVector const curve = { curve_start, MapCoord(20.0, 20.0), MapCoord(40.0, 20.0), MapCoord(60.0, 40.0) };
	auto* curved = makePathText(map, symbol, text, curve);
	auto const curved_layout = symbol->layoutPathText(curved);
	auto* line_symbol = new LineSymbol();
	line_symbol->setColor(black);
	map.addSymbol(line_symbol, map.getNumSymbols());
	PathObject reference(line_symbol, curve);
	reference.update();
	for (auto const& centre : curved_layout.glyph_centres)
	{
		auto const closest = reference.findClosestPointTo(centre);
		QVERIFY2(std::sqrt(closest.distance_squared) < 0.01, qPrintable(QString::number(std::sqrt(closest.distance_squared))));
	}
}

void PathTextTest::heightTest()
{
	// the design notes
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	map.addColor(black, 0);
	auto const text = QStringLiteral("H");
	auto const bounds_of = [&](int alignment, double y) {
		auto* symbol = makeSymbol(map, black, true, alignment);
		auto* object = makePathText(map, symbol, text, { MapCoord(0.0, y), MapCoord(60.0, y) });
		return symbol->layoutPathText(object).outline.boundingRect().translated(0.0, -y);
	};
	// The letter H has no overshoot: its outline is as high as the font's capitals.
	auto const bottom = bounds_of(0, 10.0);
	auto const cap = bottom.height();
	QVERIFY(cap > 2.9 && cap < 3.2);  // Arial, 12 pt: 0.716 em
	QVERIFY(std::abs(bottom.bottom()) < 0.01);
	// Middle: the path runs through half the height of the capitals.
	auto const middle = bounds_of(4, 20.0);
	QVERIFY2(std::abs(middle.bottom() - cap / 2) < 0.02, qPrintable(QString::number(middle.bottom())));
	QVERIFY(std::abs(middle.top() + cap / 2) < 0.02);
	// Top: the capitals hang from the path.
	auto const top = bounds_of(8, 30.0);
	QVERIFY2(std::abs(top.top()) < 0.02, qPrintable(QString::number(top.top())));
	QVERIFY(std::abs(top.bottom() - cap) < 0.02);
}

void PathTextTest::renderTest()
{
	auto const text = QStringLiteral("Hello path");
	Map plain_map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	plain_map.addColor(black, 0);
	auto* plain_symbol = makeSymbol(plain_map, black, false, 0);
	auto* plain = new TextObject(plain_symbol);
	plain->setText(text);
	plain->setAnchorPosition(MapCoord(10.0, 20.0));
	plain->setHorizontalAlignment(TextObject::AlignLeft);
	plain->setVerticalAlignment(TextObject::AlignBaseline);
	plain_map.addObject(plain);
	
	Map path_map;
	auto* path_black = new MapColor(QStringLiteral("Black"), 0);
	path_map.addColor(path_black, 0);
	auto* path_symbol = makeSymbol(path_map, path_black, true, 0);
	makePathText(path_map, path_symbol, text, { MapCoord(10.0, 20.0), MapCoord(90.0, 20.0) });
	
	auto const plain_image = render(plain_map);
	auto const path_image = render(path_map);
	QVERIFY(plain_image.pixel(50, 50) == qRgb(255, 255, 255));
	auto dark = 0;
	for (int y = 0; y < path_image.height(); ++y)
	{
		for (int x = 0; x < path_image.width(); ++x)
			dark += qGray(path_image.pixel(x, y)) < 128;
	}
	QVERIFY(dark > 100);
	// The same glyphs at the same places: the grey levels differ only by antialiasing noise.
	auto const ink_difference = [dark](const QImage& a, const QImage& b) {
		auto sum = 0.0;
		auto large = 0;
		for (int y = 0; y < a.height(); ++y)
		{
			for (int x = 0; x < a.width(); ++x)
			{
				auto const d = std::abs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
				sum += d;
				large += d > 64;
			}
		}
		qInfo("ink difference: %f per ink pixel, %d pixels differ by more than 64", sum / dark, large);
		return sum / dark;
	};
	QVERIFY(ink_difference(path_image, plain_image) < 4.0);
	
	path_symbol->setPathAlignment(4);  // middle
	path_map.getPart(0)->getObject(0)->setOutputDirty();
	auto const middle_image = render(path_map);
	QVERIFY(ink_difference(middle_image, plain_image) > 20.0);
}

void PathTextTest::xmlRoundTripTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	map.addColor(black, 0);
	auto* symbol = makeSymbol(map, black, true, 6);
	symbol->setLegacyPathSpacing(true);
	auto* plain_symbol = makeSymbol(map, black, true, 0);
	auto curve_start = MapCoord(0.0, 0.0);
	curve_start.setCurveStart(true);
	MapCoordVector const curve = { curve_start, MapCoord(20.0, -20.0), MapCoord(40.0, -20.0), MapCoord(60.0, 0.0) };
	makePathText(map, symbol, QStringLiteral("Along"), curve);
	Q_UNUSED(plain_symbol)
	
	XMLFileFormat format;
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	QVERIFY(QString::fromUtf8(buffer.data()).contains(QLatin1String("follows_path=\"true\"")));
	QCOMPARE(QString::fromUtf8(buffer.data()).count(QLatin1String("path_spacing=\"legacy\"")), 1);
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format.makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	auto const* loaded_symbol = reloaded.getSymbol(0)->asText();
	QVERIFY(loaded_symbol->followsPath());
	QCOMPARE(loaded_symbol->getPathAlignment(), 6);
	QVERIFY(loaded_symbol->hasLegacyPathSpacing());
	QVERIFY(!reloaded.getSymbol(1)->asText()->hasLegacyPathSpacing());
	auto const* loaded = reloaded.getPart(0)->getObject(0)->asText();
	QVERIFY(loaded->followsPath());
	QCOMPARE(loaded->getText(), QStringLiteral("Along"));
	QCOMPARE(loaded->getRawCoordinateVector().size(), curve.size());
	for (std::size_t i = 0; i < curve.size(); ++i)
		QVERIFY(loaded->getRawCoordinateVector()[i].isPositionEqualTo(curve[i]));
	QVERIFY(loaded->getRawCoordinateVector()[0].isCurveStart());
}

void PathTextTest::ocdRoundTripTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* symbol = makeSymbol(map, black, true, 9);  // centre, top
	auto curve_start = MapCoord(0.0, 0.0);
	curve_start.setCurveStart(true);
	MapCoordVector const curve = { curve_start, MapCoord(20.0, -20.0), MapCoord(40.0, -20.0), MapCoord(60.0, 0.0) };
	makePathText(map, symbol, QStringLiteral("Along the way"), curve);
	
	auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(12));
	QVERIFY(format);
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	for (auto const& warning : exporter->warnings())
		qInfo("export warning: %s", qPrintable(warning));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	for (auto const& warning : importer->warnings())
	{
		qInfo("import warning: %s", qPrintable(warning));
		QVERIFY2(!warning.contains(QLatin1String("Line text")), qPrintable(warning));
	}
	QCOMPARE(reloaded.getNumSymbols(), 1);
	auto const* loaded_symbol = reloaded.getSymbol(0)->asText();
	QVERIFY(loaded_symbol);
	QVERIFY(loaded_symbol->followsPath());
	QVERIFY(!loaded_symbol->isHidden());
	QCOMPARE(loaded_symbol->getPathAlignment(), 9);
	QVERIFY(!symbol->hasLegacyPathSpacing());
	QVERIFY(loaded_symbol->hasLegacyPathSpacing());
	QCOMPARE(reloaded.getNumObjects(), 1);
	auto const* loaded = reloaded.getPart(0)->getObject(0)->asText();
	QVERIFY(loaded && loaded->followsPath());
	QCOMPARE(loaded->getText(), QStringLiteral("Along the way"));
	QCOMPARE(loaded->getRawCoordinateVector().size(), curve.size());
	for (std::size_t i = 0; i < curve.size(); ++i)
		QVERIFY(loaded->getRawCoordinateVector()[i].isPositionEqualTo(curve[i]));
}

void PathTextTest::mapCheckTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	map.addColor(black, 0);
	auto* symbol = makeSymbol(map, black, true, 0);
	auto* fine = makePathText(map, symbol, QStringLiteral("Fits"), { MapCoord(0.0, 0.0), MapCoord(60.0, 0.0) });
	auto* overflowing = makePathText(map, symbol, QStringLiteral("Does not fit at all"), { MapCoord(0.0, 10.0), MapCoord(5.0, 10.0) });
	auto const issues = checkMap(map);
	auto found = 0;
	for (auto const& issue : issues)
	{
		if (issue.rule != rule::text_path_overflow)
			continue;
		++found;
		QVERIFY(issue.objects.contains(overflowing->uuid()));
		QVERIFY(!issue.objects.contains(fine->uuid()));
		QVERIFY(issue.measured > issue.limit);
	}
	QCOMPARE(found, 1);
}


QTEST_MAIN(PathTextTest)
#include "path_text_t.moc"  // IWYU pragma: keep
