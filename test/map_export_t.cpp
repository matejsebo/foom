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

#include <QtTest>
#include <QAction>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/crop.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/map_export.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/widgets/symbol_widget.h"

using namespace OpenOrienteering;


namespace {

/// A map with a black colour, two line symbols, an unused line symbol, a text and an area symbol.
struct TestMap
{
	Map& map = *new Map();
	bool taken = false;
	MapColor* black = nullptr;
	LineSymbol* line = nullptr;
	LineSymbol* other_line = nullptr;
	LineSymbol* unused = nullptr;
	TextSymbol* text = nullptr;
	AreaSymbol* area = nullptr;

	TestMap()
	{
		black = new MapColor();
		black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
		black->setRgb(MapColorRgb(0.0f, 0.0f, 0.0f));
		black->setName(QStringLiteral("black"));
		map.addColor(black, 0);
		line = new LineSymbol();
		line->setName(QStringLiteral("first line"));
		line->setLineWidth(1.0);
		line->setColor(black);
		map.addSymbol(line, 0);
		other_line = new LineSymbol();
		other_line->setName(QStringLiteral("second line"));
		other_line->setLineWidth(0.5);
		other_line->setColor(black);
		map.addSymbol(other_line, 1);
		unused = new LineSymbol();
		unused->setName(QStringLiteral("unused"));
		unused->setLineWidth(0.5);
		unused->setColor(black);
		map.addSymbol(unused, 2);
		text = new TextSymbol();
		text->setName(QStringLiteral("text"));
		map.addSymbol(text, 3);
		area = new AreaSymbol();
		area->setName(QStringLiteral("area"));
		area->setColor(black);
		map.addSymbol(area, 4);
	}

	TestMap(const TestMap&) = delete;
	TestMap& operator=(const TestMap&) = delete;

	~TestMap()
	{
		if (!taken)
			delete &map;
	}
};

PathObject* makeLine(const Symbol* symbol, double x0, double y0, double x1, double y1)
{
	return new PathObject(symbol, MapCoordVector{ MapCoord(x0, y0), MapCoord(x1, y1) });
}

PathObject* square(const Symbol* symbol, double x0, double y0, double x1, double y1)
{
	auto* path = new PathObject(symbol, MapCoordVector{ MapCoord(x0, y0), MapCoord(x1, y0), MapCoord(x1, y1), MapCoord(x0, y1) });
	path->closeAllParts();
	return path;
}

std::unique_ptr<Map> load(const QString& path)
{
	auto map = std::make_unique<Map>();
	auto importer = FileFormats.makeImporter(path, *map, nullptr);
	if (!importer || !importer->doImport())
		return {};
	return map;
}

bool dark(const QImage& image, int x, int y)
{
	return qGray(image.pixel(x, y)) < 128;
}

}  // namespace



/**
 * @test Tests the exports: SVG, part of the map, objects by symbol or selection.
 *
 * See the design notes, "Export".
 */
class MapExportTest : public QObject
{
Q_OBJECT

private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		static QTemporaryDir settings_dir;
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	}


	void svgTest()
	{
		// the design notes, "SVG export", Accept.
		TestMap t;
		t.map.addObject(makeLine(t.line, 10, 25, 90, 25));
		auto* label = new TextObject(t.text);
		label->setAnchorPosition(MapCoordF(20, 10));
		label->setText(QStringLiteral("Label"));
		t.map.addObject(label);
		auto* hidden = new LineSymbol();
		hidden->setLineWidth(1.0);
		hidden->setColor(t.black);
		hidden->setHidden(true);
		t.map.addSymbol(hidden, t.map.getNumSymbols());
		t.map.addObject(makeLine(hidden, 10, 40, 90, 40));

		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("map.svg"));
		QString error;
		QVERIFY2(exportSvg(t.map, path, QRectF(0, 0, 100, 50), &error), qPrintable(error));

		// (1), (2)
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		auto const content = file.readAll();
		QXmlStreamReader xml(content);
		QVERIFY(xml.readNextStartElement());
		QCOMPARE(xml.name().toString(), QStringLiteral("svg"));
		auto const mm = [](const QStringRef& value) {
			auto text = value.toString();
			text.remove(QLatin1String("mm"));
			return text.toDouble();
		};
		QVERIFY(std::abs(mm(xml.attributes().value(QLatin1String("width"))) - 100.0) < 0.01);
		QVERIFY(std::abs(mm(xml.attributes().value(QLatin1String("height"))) - 50.0) < 0.01);
		int paths = 0;
		int texts = 0;
		while (!xml.atEnd())
		{
			if (xml.readNext() == QXmlStreamReader::StartElement)
			{
				paths += xml.name() == QLatin1String("path");
				texts += xml.name() == QLatin1String("text");
			}
		}
		QVERIFY(!xml.hasError());
		QVERIFY(paths > 0);
		QCOMPARE(texts, 0);
		QVERIFY(content.contains("#000000"));

		// (3)
		QSvgRenderer renderer(path);
		QVERIFY(renderer.isValid());
		QImage image(200, 100, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		renderer.render(&painter, QRectF(0, 0, 200, 100));
		painter.end();
		QVERIFY(dark(image, 100, 50));
		QVERIFY(!dark(image, 100, 10));
		QVERIFY(!dark(image, 100, 80));

		// (4)
		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, &t.map);
		window->setController(editor);
		t.taken = true;
		QVERIFY(editor->getAction("export-svg"));
		auto const editor_path = dir.filePath(QStringLiteral("editor.svg"));
		QVERIFY(editor->exportSvg(editor_path));
		QVERIFY(QSvgRenderer(editor_path).isValid());
		window->deleteLater();
	}


	void extractTest()
	{
		// the design notes, "Part of the map, and objects by symbol or selection", Accept (1) to (3).
		TestMap t;
		t.map.setScaleDenominator(15000);
		Georeferencing georef;
		georef.setScaleDenominator(15000);
		georef.setProjectedRefPoint(QPointF(350000, 150000));
		t.map.setGeoreferencing(georef);
		auto* first = makeLine(t.line, -5, 5, 15, 5);
		auto* second = makeLine(t.other_line, 0, 20, 10, 20);
		t.map.addObject(first);
		t.map.addObject(second);

		// (1)
		auto extracted = extractMap(t.map, { first });
		QVERIFY(extracted);
		QCOMPARE(extracted->getScaleDenominator(), 15000u);
		QCOMPARE(extracted->getGeoreferencing().getProjectedRefPoint(), QPointF(350000, 150000));
		QCOMPARE(extracted->getNumObjects(), 1);
		QCOMPARE(extracted->getNumSymbols(), 1);
		QCOMPARE(extracted->getSymbol(0)->getName(), QStringLiteral("first line"));
		auto const* copy = extracted->getCurrentPart()->getObject(0);
		QCOMPARE(copy->uuid(), first->uuid());
		QCOMPARE(copy->getSymbol(), extracted->getSymbol(0));

		auto* combined = new CombinedSymbol();
		combined->setName(QStringLiteral("combined"));
		combined->setNumParts(2);
		combined->setPart(0, t.line, false);
		combined->setPart(1, t.area, false);
		t.map.addSymbol(combined, t.map.getNumSymbols());
		auto* both = square(combined, 30, 30, 40, 40);
		t.map.addObject(both);
		auto with_parts = extractMap(t.map, { both });
		QCOMPARE(with_parts->getNumSymbols(), 3);

		// (2)
		ops::CropParams crop;
		crop.rect = QRectF(0, 0, 10, 10);
		auto cropped = extractMap(t.map, { first }, &crop);
		QCOMPARE(cropped->getNumObjects(), 1);
		auto const& coords = cropped->getCurrentPart()->getObject(0)->getRawCoordinateVector();
		QVERIFY(MapCoordF(coords.front()).distanceTo(MapCoordF(0, 5)) < 0.01);
		QVERIFY(MapCoordF(coords.back()).distanceTo(MapCoordF(10, 5)) < 0.01);
		QVERIFY(MapCoordF(first->getRawCoordinateVector().front()).distanceTo(MapCoordF(-5, 5)) < 0.001);
		QCOMPARE(t.map.getNumObjects(), 3);

		// (3)
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("part.foom"));
		QString error;
		QVERIFY2(saveMapFile(*extracted, path, &error), qPrintable(error));
		auto reopened = load(path);
		QVERIFY(reopened);
		QCOMPARE(reopened->getNumObjects(), 1);
		QCOMPARE(reopened->getNumSymbols(), 1);
		QCOMPARE(reopened->getCurrentPart()->getObject(0)->uuid(), first->uuid());
	}


	void exportCommands()
	{
		// the design notes, "Part of the map, and objects by symbol or selection", Accept (4).
		TestMap t;
		auto* first = makeLine(t.line, -5, 5, 15, 5);
		auto* second = makeLine(t.other_line, -5, 7, 15, 7);
		auto* third = makeLine(t.other_line, 0, 30, 10, 30);
		auto* region = square(t.area, 0, 0, 10, 10);
		for (auto* object : { first, second, third, region })
			t.map.addObject(static_cast<Object*>(object));
		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, &t.map);
		window->setController(editor);
		t.taken = true;
		QVERIFY(editor->getAction("export-part"));
		QVERIFY(editor->getAction("export-objects"));

		QTemporaryDir dir;
		auto const selection_path = dir.filePath(QStringLiteral("selection.foom"));
		t.map.clearObjectSelection(false);
		t.map.addObjectToSelection(first, true);
		QVERIFY(editor->exportObjects(selection_path, false));
		QCOMPARE(load(selection_path)->getNumObjects(), 1);

		auto const symbols_path = dir.filePath(QStringLiteral("symbols.foom"));
		editor->getSymbolWidget()->selectSingleSymbol(t.other_line);
		QVERIFY(editor->exportObjects(symbols_path, true));
		auto by_symbol = load(symbols_path);
		QCOMPARE(by_symbol->getNumObjects(), 2);
		QCOMPARE(by_symbol->getNumSymbols(), 1);

		auto const rect_path = dir.filePath(QStringLiteral("rect.foom"));
		QVERIFY(editor->exportPart(rect_path, true, QRectF(0, 0, 10, 10)));
		auto by_rect = load(rect_path);
		QCOMPARE(by_rect->getNumObjects(), 3);  // two lines and the square

		auto const shape_path = dir.filePath(QStringLiteral("shape.foom"));
		t.map.clearObjectSelection(false);
		t.map.addObjectToSelection(region, true);
		QVERIFY(editor->exportPart(shape_path, false, {}));
		auto by_shape = load(shape_path);
		QCOMPARE(by_shape->getNumObjects(), 2);  // two lines, not the square
		for (int i = 0; i < by_shape->getNumObjects(); ++i)
			QVERIFY(std::abs(by_shape->getCurrentPart()->getObject(i)->asPath()->getPaperLength() - 10.0) < 0.01);
		QCOMPARE(t.map.getNumObjects(), 4);
		window->deleteLater();
	}
};


QTEST_MAIN(MapExportTest)
#include "map_export_t.moc"  // IWYU pragma: keep
