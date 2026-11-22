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
};


QTEST_MAIN(MapExportTest)
#include "map_export_t.moc"  // IWYU pragma: keep
