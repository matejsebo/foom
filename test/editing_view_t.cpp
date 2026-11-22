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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QSettings>
#include <QSlider>
#include <QString>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include "global.h"
#include "settings.h"
#include "core/box_selection_rule.h"
#include "core/glyph_code.h"
#include "core/lasso_query.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/map_view.h"
#include "core/unsymbolized_objects.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/vertex_type.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/impassable_features.h"
#include "course/route_settings.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/main_window.h"
#include "gui/map/draft_view_bar.h"
#include "gui/map/insert_glyph_dialog.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/symbol_widget.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "tools/curve_entry_style.h"
#include "tools/draw_path_tool.h"
#include "tools/edit_point_tool.h"
#include "tools/lasso_tool.h"
#include "tools/text_object_editor_helper.h"
#include "undo/undo_manager.h"
#include "util/util.h"

using namespace OpenOrienteering;


namespace {

bool saveMap(const Map& map, const QString& path, const char* format_id = "XML")
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	auto exporter = format->makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

bool loadMap(Map& map, MapView* view, const QString& path)
{
	auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsReading);
	if (!format)
		return false;
	auto importer = format->makeImporter(path, &map, view);
	return importer && importer->doImport();
}

MapColor* addColor(Map& map, const char* name, const MapColorCmyk& cmyk)
{
	auto* color = new MapColor(QString::fromLatin1(name), map.getNumColors());
	color->setCmyk(cmyk);
	color->setOpacity(1.0f);
	map.addColor(color, map.getNumColors());
	return color;
}

/** The map at 50 pixels per mm over the given area, on white. */
QImage render(Map& map, const QRectF& area)
{
	map.updateObjects();
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setResolution(1270);  // 50 pixels per mm
	printer.setPrintArea(area);
	QImage image(qRound(area.width() * 50), qRound(area.height() * 50), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image, RenderConfig::NoOptions);
	}
	return image;
}

/** The grey value of the image at a map position. */
int greyAt(const QImage& image, const QRectF& area, double x_mm, double y_mm)
{
	auto const x = qBound(0, int((x_mm - area.left()) * 50), image.width() - 1);
	auto const y = qBound(0, int((y_mm - area.top()) * 50), image.height() - 1);
	return qGray(image.pixel(x, y));
}

bool inked(const QImage& image, const QRectF& area, double x_mm, double y_mm)
{
	return greyAt(image, area, x_mm, y_mm) < 128;
}

PathObject* closedPath(const Symbol* symbol, std::initializer_list<MapCoord> coords)
{
	auto* path = new PathObject(symbol);
	for (auto const& coord : coords)
		path->addCoordinate(coord);
	path->parts().front().setClosed(true);
	return path;
}

std::vector<int> flagsOf(const Object* object, MapCoord::Flag flag)
{
	std::vector<int> flags;
	for (auto const& coord : object->asPath()->getRawCoordinateVector())
		flags.push_back(int((coord.flags() & flag) != 0));
	return flags;
}

/** A map editor in a window which is never on the screen. */
struct Editor
{
	MainWindow* window;
	MapEditorController* editor;
	MapWidget* map_widget;

	explicit Editor(Map* map)
	{
		window = new MainWindow();
		editor = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(editor);
		map_widget = editor->getMainWidget();
		window->setAttribute(Qt::WA_DontShowOnScreen);
		window->resize(900, 700);
		window->show();
		QCoreApplication::processEvents();
		map_widget->getMapView()->setCenter(MapCoord(0, 0));
		map_widget->getMapView()->setZoom(2.0);
	}

	~Editor()
	{
		window->deleteLater();
	}

	QPoint pixel(double x_mm, double y_mm) const
	{
		return map_widget->mapToViewport(QPointF(x_mm, y_mm)).toPoint();
	}

	void drag(QPoint from, QPoint to)
	{
		// The tools take the place of a press from the hover before it.
		QMouseEvent hover(QEvent::MouseMove, from, map_widget->mapToGlobal(from), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QCoreApplication::sendEvent(map_widget, &hover);
		QTest::mousePress(map_widget, Qt::LeftButton, {}, from);
		QMouseEvent move(QEvent::MouseMove, to, map_widget->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(map_widget, &move);
		QTest::mouseRelease(map_widget, Qt::LeftButton, {}, to);
	}
};

/** A map with a black colour, a line symbol and nothing else. */
struct BasicMap
{
	Map* map = new Map();  // owned by an editor, or deleted by the test
	MapColor* black = nullptr;
	LineSymbol* line = nullptr;
	PointSymbol* point = nullptr;
	TextSymbol* text = nullptr;

	BasicMap()
	{
		black = addColor(*map, "black", MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
		line = new LineSymbol();
		line->setColor(black);
		line->setLineWidth(1.0);
		line->setNumberComponent(0, 801);  // OCD files tell symbols by their numbers
		map->addSymbol(line, 0);
		point = new PointSymbol();
		point->setInnerColor(black);
		point->setInnerRadius(500);
		point->setNumberComponent(0, 802);
		map->addSymbol(point, 1);
		text = new TextSymbol();
		text->setColor(black);
		text->setNumberComponent(0, 803);
		map->addSymbol(text, 2);
	}
};

LineSymbol* numberedLine(Map& map, int number, int sub = 0)
{
	auto* symbol = new LineSymbol();
	symbol->setColor(map.getColor(0));
	symbol->setLineWidth(0.5);
	symbol->setNumberComponent(0, number);
	symbol->setNumberComponent(1, sub);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

AreaSymbol* numberedArea(Map& map, int number)
{
	auto* symbol = new AreaSymbol();
	symbol->setColor(map.getColor(0));
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

}  // namespace



/**
 * Tests the editing and view additions of 2026-11-22: corner and virtual
 * gap vertices, the rectangle selection rule, glyphs, the curve entry
 * style, the draft view and the highlights of objects without symbol and
 * of impassable features.
 *
 * See the design notes, the design notes
 * and the design notes.
 */
class EditingViewTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;

	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}

private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
	}

	// Editing spec, "Corner and virtual gap vertices", Accept (2)
	void virtualGapRenderTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto const area = QRectF(-5.0, -5.0, 40.0, 20.0);

		auto gap = MapCoord(10.0, 0.0);
		gap.setVirtualGap(true);
		auto* line = new PathObject(m.line, { MapCoord(0.0, 0.0), gap, MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) });
		m.map->addObject(line);
		auto image = render(*m.map, area);
		QVERIFY(!inked(image, area, 15.0, 0.0));
		QVERIFY(inked(image, area, 5.0, 0.0));
		QVERIFY(inked(image, area, 25.0, 0.0));
		// The geometry stays.
		QCOMPARE(line->getPaperLength(), 30.0);

		line->getCoordinateRef(1).setVirtualGap(false);
		line->setOutputDirty();
		image = render(*m.map, area);
		QVERIFY(inked(image, area, 15.0, 0.0));

		// On the last vertex of an open line the flag does nothing.
		line->getCoordinateRef(3).setVirtualGap(true);
		line->setOutputDirty();
		image = render(*m.map, area);
		QVERIFY(inked(image, area, 15.0, 0.0));
		QVERIFY(inked(image, area, 29.0, 0.0));
		m.map->deleteObject(line);

		// A closed square with the gap at (10,0)
		auto* square = closedPath(m.line, { MapCoord(0.0, 0.0), gap, MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) });
		m.map->addObject(square);
		square->update();
		auto count = 0;
		square->renderables().forEach([&count](const Renderable&) { ++count; });
		QCOMPARE(count, 1);
		image = render(*m.map, area);
		QVERIFY(!inked(image, area, 10.0, 5.0));
		QVERIFY(inked(image, area, 5.0, 0.0));
		QVERIFY(inked(image, area, 5.0, 10.0));
		QVERIFY(inked(image, area, 0.0, 5.0));
		m.map->deleteObject(square);

		// An area of a combined symbol: fill and a border of 1 mm.
		auto* yellow = addColor(*m.map, "yellow", MapColorCmyk(0.0f, 0.0f, 1.0f, 0.0f));
		auto* fill = new AreaSymbol();
		fill->setColor(yellow);
		auto* border = new LineSymbol();
		border->setColor(m.black);
		border->setLineWidth(1.0);
		auto* combined = new CombinedSymbol();
		combined->setNumParts(2);
		combined->setPart(0, fill, true);
		combined->setPart(1, border, true);
		m.map->addSymbol(combined, m.map->getNumSymbols());
		auto* filled = closedPath(combined, { MapCoord(0.0, 0.0), gap, MapCoord(10.0, 10.0), MapCoord(0.0, 10.0) });
		m.map->addObject(filled);
		image = render(*m.map, area);
		auto const centre = QColor(image.pixel(int((5.0 - area.left()) * 50), int((5.0 - area.top()) * 50)));
		QVERIFY2(centre.blue() < 100 && centre.red() > 200, qPrintable(centre.name()));  // yellow
		QCOMPARE(greyAt(image, area, 10.3, 5.0), 255);
		QVERIFY(inked(image, area, -0.3, 5.0));
	}

	// Seen on 2026-11-22: the hidden segment of a virtual gap could not be clicked,
	// and the selection frame left it out.
	void virtualGapSelectTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto gap = MapCoord(10.0, 0.0);
		gap.setVirtualGap(true);
		auto* line = new PathObject(m.line, { MapCoord(0.0, 0.0), gap, MapCoord(10.0, 20.0) });
		m.map->addObject(line);
		line->update();
		QVERIFY(line->getExtent().contains(QPointF(10.0, 19.0)));
		SelectionInfoVector found;
		m.map->findObjectsAt(MapCoordF(10.0, 15.0), 0.25, false, false, false, false, found);
		QCOMPARE(int(found.size()), 1);
		QCOMPARE(found.front().second, line);
		// Nothing far beside it.
		found.clear();
		m.map->findObjectsAt(MapCoordF(15.0, 15.0), 0.25, false, false, false, false, found);
		QVERIFY(found.empty());
	}

	// Editing spec, "Corner and virtual gap vertices", Accept (3)
	void vertexTypeFileTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		m.line->setDashed(true);
		m.line->setDashLength(2000);
		m.line->setBreakLength(1000);
		auto corner = MapCoord(10.0, 0.0);
		corner.setDashPoint(true);
		corner.setCornerPoint(true);
		auto dash = MapCoord(20.0, 0.0);
		dash.setDashPoint(true);
		auto gap = MapCoord(30.0, 0.0);
		gap.setVirtualGap(true);
		m.map->addObject(new PathObject(m.line, { MapCoord(0.0, 0.0), corner, dash, gap, MapCoord(40.0, 0.0) }));

		auto const check = [](const Object* object, bool gap_kept) {
			auto const& coords = object->asPath()->getRawCoordinateVector();
			QCOMPARE(int(coords.size()), 5);
			QVERIFY(coords[1].isDashPoint() && coords[1].isCornerPoint());
			QVERIFY(coords[2].isDashPoint() && !coords[2].isCornerPoint());
			QCOMPARE(coords[3].isVirtualGap(), gap_kept);
			QVERIFY(!coords[3].isDashPoint() && !coords[3].isCornerPoint());
			QVERIFY(!coords[0].isCornerPoint() && !coords[4].isCornerPoint());
		};

		QVERIFY(saveMap(*m.map, file("types.foom")));
		{
			Map loaded;
			QVERIFY(loadMap(loaded, nullptr, file("types.foom")));
			QCOMPARE(loaded.getNumObjects(), 1);
			check(loaded.getPart(0)->getObject(0), true);
		}
		QVERIFY(saveMap(*m.map, file("types.ocd"), "OCD2018"));
		{
			Map loaded;
			QVERIFY(loadMap(loaded, nullptr, file("types.ocd")));

			QCOMPARE(loaded.getNumObjects(), 1);
			check(loaded.getPart(0)->getObject(0), false);
		}
	}

	// Editing spec, "Rectangle selection rule", Accept (1) and (2)
	void rectangleSelectionRuleTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto const box = QRectF(QPointF(0.0, 0.0), QPointF(10.0, 10.0));
		auto* point = new PointObject(m.point);
		point->setPosition(MapCoord(5.0, 5.0));
		auto* inside = new PathObject(m.line, { MapCoord(2.0, 2.0), MapCoord(8.0, 2.0) });
		auto* across = new PathObject(m.line, { MapCoord(2.0, 8.0), MapCoord(20.0, 8.0) });
		auto* text = new TextObject(m.text);
		text->setAnchorPosition(MapCoordF(50.0, 50.0));
		text->setText(QStringLiteral("A"));
		auto* corner = new PointObject(m.point);
		corner->setPosition(MapCoord(10.0, 10.0));
		for (auto* object : std::initializer_list<Object*>{ point, inside, across, text, corner })
			m.map->addObject(object);
		m.map->updateObjects();

		QVERIFY(objectEntirelyInBox(*point, box));
		QVERIFY(objectEntirelyInBox(*inside, box));
		QVERIFY(!objectEntirelyInBox(*across, box));
		QVERIFY(!objectEntirelyInBox(*text, box));
		QVERIFY(objectEntirelyInBox(*corner, box));

		// A curve from (2,5) to (8,5); handles at y = 11 bulge to y = 9.5.
		auto start = MapCoord(2.0, 5.0);
		start.setCurveStart(true);
		auto* low = new PathObject(m.line, { start, MapCoord(2.0, 11.0), MapCoord(8.0, 11.0), MapCoord(8.0, 5.0) });
		auto* high = new PathObject(m.line, { start, MapCoord(2.0, 14.5), MapCoord(8.0, 14.5), MapCoord(8.0, 5.0) });
		m.map->addObject(low);
		m.map->addObject(high);
		m.map->updateObjects();
		QVERIFY(objectEntirelyInBox(*low, box));
		QVERIFY(!objectEntirelyInBox(*high, box));  // bulges to y = 12.125

		auto const triangle = std::vector<MapCoordF>{ MapCoordF(0.0, 0.0), MapCoordF(12.0, 0.0), MapCoordF(6.0, 12.0) };
		auto* short_line = new PathObject(m.line, { MapCoord(4.0, 2.0), MapCoord(8.0, 2.0) });
		m.map->addObject(short_line);
		m.map->updateObjects();
		QVERIFY(objectEntirelyInPolygon(*point, triangle));
		QVERIFY(objectEntirelyInPolygon(*short_line, triangle));
		QVERIFY(!objectEntirelyInPolygon(*across, triangle));

		std::vector<Object*> found;
		findObjectsInPolygon(*m.map->getCurrentPart(), triangle, false, false, found, true);
		std::sort(begin(found), end(found));
		auto expected = std::vector<Object*>{ point, inside, short_line };
		std::sort(begin(expected), end(expected));
		QCOMPARE(found, expected);
	}

	// Editing spec, "Rectangle selection rule", Accept (3)
	void rectangleSelectionRule()
	{
		BasicMap m;
		auto* point = new PointObject(m.point);
		point->setPosition(MapCoord(5.0, 5.0));
		auto* inside = new PathObject(m.line, { MapCoord(2.0, 2.0), MapCoord(8.0, 2.0) });
		auto* across = new PathObject(m.line, { MapCoord(2.0, 8.0), MapCoord(20.0, 8.0) });
		for (auto* object : std::initializer_list<Object*>{ point, inside, across })
			m.map->addObject(object);
		Editor e(m.map);
		e.editor->setTool(new EditPointTool(e.editor, nullptr));

		auto& settings = Settings::getInstance();
		settings.setSetting(Settings::MapEditor_RectangleSelectionInside, false);
		m.map->clearObjectSelection(false);
		e.drag(e.pixel(-0.5, -0.5), e.pixel(10.0, 10.0));
		QCOMPARE(m.map->getNumSelectedObjects(), 3);

		settings.setSetting(Settings::MapEditor_RectangleSelectionInside, true);
		m.map->clearObjectSelection(false);
		e.drag(e.pixel(-0.5, -0.5), e.pixel(10.0, 10.0));
		QCOMPARE(m.map->getNumSelectedObjects(), 2);
		QVERIFY(m.map->isObjectSelected(point) && m.map->isObjectSelected(inside));

		auto* lasso = new LassoTool(e.editor, nullptr);
		e.editor->setTool(lasso);
		m.map->clearObjectSelection(false);
		lasso->selectPolygon({ MapCoordF(0.0, 0.0), MapCoordF(12.0, 0.0), MapCoordF(6.0, 12.0) }, false);
		QCOMPARE(m.map->getNumSelectedObjects(), 2);
		QVERIFY(m.map->isObjectSelected(point) && m.map->isObjectSelected(inside));

		settings.setSetting(Settings::MapEditor_RectangleSelectionInside, false);
		e.editor->setTool(nullptr);
	}

	// Editing spec, "Insert glyphs", Accept (1)
	void glyphCodeTest()
	{
		auto const e_acute = QString(QChar(0xE9));
		for (auto const* code : { "E9", "00e9", "U+00E9", "0xE9", " e9 ", "\\u00e9" })
		{
			QString error;
			QCOMPARE(glyphFromHex(QString::fromLatin1(code), &error), e_acute);
			QVERIFY(error.isEmpty());
		}
		auto const emoji = glyphFromHex(QStringLiteral("1F600"));
		QCOMPARE(emoji.size(), 2);
		QCOMPARE(int(emoji.at(0).unicode()), 0xD83D);
		QCOMPARE(int(emoji.at(1).unicode()), 0xDE00);
		for (auto const* code : { "D800", "110000", "0", "xyz", "", "U+" })
		{
			QString error;
			QVERIFY2(glyphFromHex(QString::fromLatin1(code), &error).isEmpty(), code);
			QVERIFY2(!error.isEmpty(), code);
		}
	}

	// Editing spec, "Insert glyphs", Accept (2)
	void insertGlyph()
	{
		BasicMap m;
		auto* text = new TextObject(m.text);
		text->setAnchorPosition(MapCoordF(0.0, 0.0));
		text->setText(QStringLiteral("Bach"));
		m.map->addObject(text);
		Editor e(m.map);
		{
			TextObjectEditorHelper helper(text, e.editor);
			QVERIFY(helper.setTextSelection(2, 2));
			helper.insertGlyph(QString(QChar(0xE9)));
			QCOMPARE(text->getText(), QString::fromUtf8("Ba\xc3\xa9" "ch"));
			QCOMPARE(helper.cursorPosition(), 3);
			QVERIFY(helper.setTextSelection(3, 5));
			helper.insertGlyph(QString(QChar(0xE9)));
			QCOMPARE(text->getText(), QString::fromUtf8("Ba\xc3\xa9\xc3\xa9"));
			QVERIFY(helper.toolBar());
			auto const actions = helper.toolBar()->actions();
			QVERIFY(std::any_of(actions.begin(), actions.end(), [](const QAction* a) { return a->objectName() == QLatin1String("insert-glyph"); }));
		}
	}

	// Seen on 2026-11-22: with the font of a text symbol, whose size is in map
	// units, the buttons were centimetres high and the dialog filled the screen.
	void insertGlyphDialogSize()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto const symbol_font = m.text->getQFont();
		QFont huge(QStringLiteral("Arial"));
		huge.setPixelSize(4000);
		for (auto const& font : { symbol_font, huge })
		{
			InsertGlyphDialog dialog(font, nullptr);
			auto const ordinary = QFontMetrics(dialog.font()).height();
			auto const buttons = dialog.findChildren<QToolButton*>();
			QVERIFY(!buttons.isEmpty());
			for (auto const* button : buttons)
			{
				QVERIFY2(button->sizeHint().height() <= 3 * ordinary, qPrintable(QString::number(button->sizeHint().height())));
				QCOMPARE(button->font().family(), font.family());
			}
			QVERIFY2(dialog.sizeHint().height() < 600, qPrintable(QString::number(dialog.sizeHint().height())));
			// The preview is large.
			auto const* preview = dialog.findChild<QLabel*>(QStringLiteral("preview"));
			QVERIFY(preview);
			QVERIFY(QFontMetrics(preview->font()).height() >= 3 * ordinary);
		}
	}

	// Seen on 2026-11-22: in a window which had shown four maps, the context menu
	// of the tool bars listed Numeric entry, Sketch and Draft view four times.
	void editorBarsTest()
	{
		auto* window = new MainWindow();
		window->setAttribute(Qt::WA_DontShowOnScreen);
		for (int i = 0; i < 3; ++i)
			window->setController(new MapEditorController(MapEditorController::MapEditor, new Map()));
		for (auto const* name : { "Numeric entry toolbar", "Sketch toolbar", "Draft view toolbar", "Course setting toolbar" })
		{
			auto const bars = window->findChildren<QToolBar*>(QString::fromLatin1(name));
			QCOMPARE(bars.size(), 1);
		}
		delete window;
	}

	// Editing spec, "Curve entry style", Accept (1)
	void curveEntryStyleTest()
	{
		QCOMPARE(curveHandleLength(CurveEntryStyle::Legacy, 30.0, 5.0), 30.0 * BEZIER_HANDLE_DISTANCE);
		QCOMPARE(curveHandleLength(CurveEntryStyle::Illustrator, 30.0, 5.0), 5.0);
		QCOMPARE(curveHandleLength(CurveEntryStyle::Illustrator, 30.0, 0.0), 30.0 * BEZIER_HANDLE_DISTANCE);
	}

	// Editing spec, "Curve entry style", Accept (2)
	void curveEntryStyle_data()
	{
		QTest::addColumn<int>("style");
		QTest::addColumn<double>("first_handle");
		QTest::addColumn<double>("second_handle");
		QTest::newRow("legacy") << 0 << 30.0 * BEZIER_HANDLE_DISTANCE << 30.0 - 30.0 * BEZIER_HANDLE_DISTANCE;
		QTest::newRow("illustrator") << 1 << 5.0 << 25.0;
	}

	void curveEntryStyle()
	{
		QFETCH(int, style);
		QFETCH(double, first_handle);
		QFETCH(double, second_handle);
		Settings::getInstance().setSetting(Settings::MapEditor_CurveEntryStyle, style);
		BasicMap m;
		Editor e(m.map);
		e.editor->getSymbolWidget()->selectSingleSymbol(m.line);
		auto* tool = new DrawPathTool(e.editor, nullptr, false, true);
		e.editor->setTool(tool);
		e.drag(e.pixel(0.0, 0.0), e.pixel(5.0, 0.0));
		e.drag(e.pixel(30.0, 0.0), e.pixel(35.0, 0.0));
		QTest::keyClick(e.map_widget, Qt::Key_Return);
		Settings::getInstance().setSetting(Settings::MapEditor_CurveEntryStyle, 0);

		QCOMPARE(m.map->getNumObjects(), 1);
		auto const* path = m.map->getPart(0)->getObject(0)->asPath();
		QVERIFY(path);
		auto const& coords = path->getRawCoordinateVector();
		QVERIFY2(coords.size() >= 4, qPrintable(QString::number(coords.size())));
		QVERIFY(coords[0].isCurveStart());
		auto const near = [](const MapCoord& c, double x, double y) {
			return std::abs(c.x() - x) < 0.3 && std::abs(c.y() - y) < 0.3;  // two pixels at zoom 2
		};
		QVERIFY(near(coords[0], 0.0, 0.0));
		QVERIFY2(near(coords[1], first_handle, 0.0), qPrintable(QString::number(coords[1].x())));
		QVERIFY2(near(coords[2], second_handle, 0.0), qPrintable(QString::number(coords[2].x())));
		QVERIFY(near(coords[3], 30.0, 0.0));
		e.editor->setTool(nullptr);
	}

	// Interface spec, "Draft view", Accept (1) and (2)
	void draftViewTest()
	{
		Map map;
		MapView view(&map);
		QVERIFY(writeImage(file("black.png")));
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("black.png"), &map));
		auto* temp = map.getTemplate(0);
		QVERIFY(temp->loadTemplateFile());
		temp->setTemplateState(Template::Loaded);
		view.setTemplateVisibility(temp, { 0.6f, true });
		view.setMapVisibility({ 0.8f, true });

		QVERIFY(!view.isDraftMode());
		QCOMPARE(view.draftMapDim(), 0.5);
		QCOMPARE(view.draftTemplateDim(), 0.5);
		QVERIFY(std::abs(view.screenMapVisibility().opacity - 0.8) < 1e-6);
		QVERIFY(std::abs(view.screenTemplateVisibility(temp).opacity - 0.6) < 1e-6);

		auto signals_count = 0;
		auto last_feature = MapView::MultipleFeatures;
		connect(&view, &MapView::visibilityChanged, this, [&signals_count, &last_feature](MapView::VisibilityFeature feature) {
			++signals_count;
			last_feature = feature;
		});
		view.setDraftMode(true);
		QCOMPARE(signals_count, 1);
		QCOMPARE(last_feature, MapView::DraftMode);
		view.setDraftMode(true);
		QCOMPARE(signals_count, 1);
		view.setDraftMapDim(0.25);
		view.setDraftTemplateDim(0.5);
		QVERIFY(std::abs(view.screenMapVisibility().opacity - 0.6) < 1e-6);
		QVERIFY(std::abs(view.screenTemplateVisibility(temp).opacity - 0.3) < 1e-6);
		QVERIFY(std::abs(view.getMapVisibility().opacity - 0.8) < 1e-6);
		QVERIFY(std::abs(view.getTemplateVisibility(temp).opacity - 0.6) < 1e-6);

		view.setDraftMapDim(-1.0);
		QCOMPARE(view.draftMapDim(), 0.0);
		view.setDraftMapDim(2.0);
		QCOMPARE(view.draftMapDim(), 1.0);
		view.setDraftMapDim(0.25);

		// Saved with the view
		QVERIFY(saveMapWithView(map, view, file("draft.foom")));
		{
			Map loaded;
			MapView loaded_view(&loaded);
			QVERIFY(loadMap(loaded, &loaded_view, file("draft.foom")));
			QVERIFY(loaded_view.isDraftMode());
			QCOMPARE(loaded_view.draftMapDim(), 0.25);
			QCOMPARE(loaded_view.draftTemplateDim(), 0.5);
		}
		view.setDraftMode(false);
		view.setDraftMapDim(0.5);
		QVERIFY(saveMapWithView(map, view, file("plain.foom")));
		QFile plain(file("plain.foom"));
		QVERIFY(plain.open(QIODevice::ReadOnly));
		QVERIFY(!plain.readAll().contains("draft"));
	}

	// Interface spec, "Draft view", Accept (3)
	void draftViewDrawTest()
	{
		Map map;
		MapView view(&map);
		QVERIFY(writeImage(file("black.png")));
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("black.png"), &map));
		auto* temp = map.getTemplate(0);
		QVERIFY(temp->loadTemplateFile());
		temp->setTemplateState(Template::Loaded);
		view.setTemplateVisibility(temp, { 1.0f, true });
		auto const box = temp->calculateTemplateBoundingBox();
		auto const centreGrey = [&map, &view, box](bool on_screen) {
			QImage image(40, 40, QImage::Format_RGB32);
			image.fill(Qt::white);
			QPainter painter(&image);
			painter.scale(40.0 / box.width(), 40.0 / box.height());
			painter.translate(-box.topLeft());
			map.drawTemplates(&painter, box, 0, map.getNumTemplates() - 1, &view, on_screen);
			painter.end();
			return qGray(image.pixel(20, 20));
		};
		QCOMPARE(centreGrey(true), 0);
		view.setDraftMode(true);
		view.setDraftTemplateDim(0.5);
		auto const dimmed = centreGrey(true);
		QVERIFY2(std::abs(dimmed - 128) <= 10, qPrintable(QString::number(dimmed)));
		QCOMPARE(centreGrey(false), 0);
	}

	// Interface spec, "Draft view", Accept (4)
	void draftViewCommands()
	{
		BasicMap m;
		Editor e(m.map);
		auto* action = e.editor->getAction("view-draft");
		QVERIFY(action);
		QVERIFY(action->isCheckable());
		auto* view = e.map_widget->getMapView();
		QVERIFY(!view->isDraftMode());
		action->trigger();
		QVERIFY(view->isDraftMode());
		action->trigger();
		QVERIFY(!view->isDraftMode());
		auto* bar = e.editor->draftViewBar();
		QVERIFY(bar);
		bar->mapSlider()->setValue(30);
		bar->templateSlider()->setValue(70);
		QVERIFY(std::abs(view->draftMapDim() - 0.3) < 1e-6);
		QVERIFY(std::abs(view->draftTemplateDim() - 0.7) < 1e-6);
	}

	// Symbols spec, "Show objects without symbol", Accept (1)
	void unsymbolizedObjectsTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto objects = fillUnsymbolized(*m.map);
		auto const found = findUnsymbolizedObjects(*m.map);
		QCOMPARE(found, objects);
	}

	// Editing spec, "Show impassable features", Accept (1) and (2)
	void impassableFeaturesTest()
	{
		BasicMap m;
		std::unique_ptr<Map> owner(m.map);
		auto objects = fillImpassable(*m.map);
		RouteSettings settings;
		QCOMPARE(findImpassableObjects(*m.map, settings), (std::vector<const Object*>{ objects[0], objects[1] }));

		settings.symbols.push_back({ QStringLiteral("505.000"), Runnability{ Runnability::Impassable, 0 } });
		QCOMPARE(findImpassableObjects(*m.map, settings), (std::vector<const Object*>{ objects[0], objects[1], objects[2] }));
		settings.symbols.push_back({ QStringLiteral("518.000"), Runnability{ Runnability::None, 100 } });
		QCOMPARE(findImpassableObjects(*m.map, settings), (std::vector<const Object*>{ objects[1], objects[2] }));
		const_cast<Symbol*>(objects[1]->getSymbol())->setHidden(true);
		QCOMPARE(findImpassableObjects(*m.map, settings), (std::vector<const Object*>{ objects[2] }));
		const_cast<Symbol*>(objects[1]->getSymbol())->setHidden(false);

		// A part of the layout role is left out.
		auto* layout = new MapPart(QStringLiteral("layout"), m.map);
		layout->setRole(MapPart::Role::Layout);
		m.map->addPart(layout, 1);
		auto* barrier = new PathObject(objects[1]->getSymbol(), { MapCoord(0.0, 40.0), MapCoord(10.0, 40.0), MapCoord(10.0, 50.0) });
		layout->addObject(barrier);
		QCOMPARE(findImpassableObjects(*m.map, settings), (std::vector<const Object*>{ objects[1], objects[2] }));
	}

	// Editing spec, "Show impassable features", Accept (3); symbols spec, "Show objects without symbol", Accept (2)
	void highlightCommands()
	{
		BasicMap m;
		fillImpassable(*m.map);
		fillUnsymbolized(*m.map);
		Editor e(m.map);
		auto* impassable = e.editor->getAction("view-impassable");
		auto* unsymbolized = e.editor->getAction("view-unsymbolized");
		QVERIFY(impassable && unsymbolized);
		QVERIFY(impassable->isCheckable() && unsymbolized->isCheckable());
		QCOMPARE(e.editor->impassableObjectCount(), 0);
		impassable->trigger();
		QCOMPARE(e.editor->impassableObjectCount(), 2);
		unsymbolized->trigger();
		QCOMPARE(e.editor->unsymbolizedObjectCount(), 3);
		e.map_widget->repaint();  // the overlay draws
		impassable->trigger();
		unsymbolized->trigger();
		QCOMPARE(e.editor->impassableObjectCount(), 0);
		QCOMPARE(e.editor->unsymbolizedObjectCount(), 0);
	}

	// Editing spec, "Corner and virtual gap vertices", Accept (4)
	void vertexTypeCommands()
	{
		BasicMap m;
		auto* line = new PathObject(m.line, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(20.0, 0.0) });
		m.map->addObject(line);
		auto const uuid = line->uuid();
		Editor e(m.map);
		for (auto const* id : { "set-vertex-type-normal", "set-vertex-type-dash", "set-vertex-type-corner", "set-vertex-type-gap" })
			QVERIFY2(e.editor->getAction(id), id);
		m.map->clearObjectSelection(false);
		m.map->addObjectToSelection(line, true);
		auto const steps = m.map->undoManager().undoStepCount();
		e.editor->setVertexType(ops::VertexType::Corner, true);
		auto const* flagged = m.map->findObject(uuid);
		QVERIFY(flagged && flagged != line);
		QCOMPARE(flagsOf(flagged, MapCoord::CornerPoint), (std::vector<int>{ 0, 1, 0 }));
		QCOMPARE(flagsOf(flagged, MapCoord::DashPoint), (std::vector<int>{ 0, 1, 0 }));
		QCOMPARE(m.map->undoManager().undoStepCount(), steps + 1);
		QVERIFY(m.map->undoManager().undo());
		QCOMPARE(m.map->findObject(uuid), line);
	}

private:
	static bool writeImage(const QString& path)
	{
		QImage image(4, 4, QImage::Format_RGB32);
		image.fill(Qt::black);
		return image.save(path, "PNG");
	}

	static bool saveMapWithView(const Map& map, const MapView& view, const QString& path)
	{
		XMLFileFormat format;
		auto exporter = format.makeExporter(path, &map, &view);
		return exporter && exporter->doExport();
	}

	/** Adds a 518 line, a 521 area, a 505 line and a 201 point; returns them. */
	static std::vector<const Object*> fillImpassable(Map& map)
	{
		auto* l518 = numberedLine(map, 518);
		auto* a521 = numberedArea(map, 521);
		auto* l505 = numberedLine(map, 505);
		auto* p201 = new PointSymbol();
		p201->setNumberComponent(0, 201);
		map.addSymbol(p201, map.getNumSymbols());
		std::vector<const Object*> objects;
		auto add = [&map, &objects](Object* object) { map.addObject(object, 0); objects.push_back(object); };
		add(new PathObject(l518, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
		add(closedPath(a521, { MapCoord(0.0, 10.0), MapCoord(10.0, 10.0), MapCoord(10.0, 20.0) }));
		add(new PathObject(l505, { MapCoord(0.0, 30.0), MapCoord(10.0, 30.0) }));
		auto* point = new PointObject(p201);
		point->setPosition(MapCoord(20.0, 20.0));
		add(point);
		return objects;
	}

	/** Adds a line of a real symbol and three objects of the placeholders; returns the three. */
	static std::vector<const Object*> fillUnsymbolized(Map& map)
	{
		auto* real = map.getSymbol(0);
		map.addObject(new PathObject(real, { MapCoord(0.0, -10.0), MapCoord(10.0, -10.0) }), 0);
		auto* line = new PathObject(Map::getUndefinedLine(), { MapCoord(0.0, -20.0), MapCoord(10.0, -20.0) });
		map.addObject(line, 0);
		auto* text = new TextObject(Map::getUndefinedText());
		text->setText(QStringLiteral("?"));
		map.addObject(text, 0);
		auto* second = new MapPart(QStringLiteral("second"), &map);
		map.addPart(second, map.getNumParts());
		auto* point = new PointObject(Map::getUndefinedPoint());
		second->addObject(point);
		return { line, text, point };
	}
};


QTEST_MAIN(EditingViewTest)
#include "editing_view_t.moc"
