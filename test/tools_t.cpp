/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2015-2020, 2025 Kai Pastor
 *    Copyright 2025 Matthias Kühlewein
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "tools_t.h"

#include <cmath>

#include <Qt>
#include <QtGlobal>
#include <memory>

#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QComboBox>  // foom
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPolygonF>  // foom
#include <QJsonArray>  // foom
#include <QMessageBox>  // foom
#include <QMouseEvent>
#include <QTimer>  // foom
#include <QPoint>
#include <QPointF>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QVariant>
#include <QString>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QToolBar>  // foom
#include <QToolButton>  // foom

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "layout/generator.h"  // foom
#include "layout/layout_part.h"  // foom
#include "tools/generator_place_tool.h"  // foom
#include "core/map_printer.h"
#include "core/map_transform.h"
#include "core/symbol_tools.h"
#include "core/symbol_tree.h"
#include "core/objects/object.h"
#include "core/objects/object_query.h"
#include "core/ops/align.h"
#include "core/ops/to_curve.h"  // foom
#include "tools/distribute_points_tool.h"  // foom
#include "core/ops/sized_shape.h"  // foom
#include "templates/template.h"  // foom
#include "templates/template_image.h"  // foom
#include "templates/template_map.h"  // foom
#include "core/bookmarks.h"
#include "core/extents.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/profile.h"
#include "elevation/raster_products.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "course/route_course.h"  // foom
#include "elevation/point_cloud_products.h"
#include "gui/map/elevation_dialogs.h"  // foom
#include "gui/map/extent_dialog.h"
#include "check/issue.h"
#include "check/legibility.h"
#include "check/map_check.h"
#include "gui/map/legibility_dialog.h"
#include "gui/map/rectangle_box_dialog.h"
#include "gui/map/topology_dialogs.h"
#include "core/ops/rectangle_box.h"
#include "tools/legibility_feedback.h"
#include <QPainter>  // foom
#include <QRegularExpression>  // foom
#include "tools/draw_circle_tool.h"  // foom
#include "tools/draw_freehand_tool.h"  // foom
#include "tools/draw_rectangle_tool.h"  // foom
#include "tools/rectangle_box_tool.h"  // foom
#include "tools/stairway_tool.h"  // foom
#include "settings.h"
#include "core/georeferencing.h"
#include "core/map_view.h"
#include "gui/map/view_history.h"
#include "gui/widgets/shortcut_settings.h"
#include "gui/map/numeric_entry_bar.h"
#include "gui/widgets/symbol_groups_widget.h"
#include "gui/widgets/symbol_widget.h"
#include "tools/affine_transform_tool.h"
#include "tools/local_warp_tool.h"
#include "tools/rubbersheet_tool.h"
#include "tools/point_pairs_window.h"  // foom
#include "gui/map/map_transform_dialogs.h"  // foom
#include <QCheckBox>  // foom
#include <QComboBox>  // foom
#include <QDoubleSpinBox>  // foom
#include <QPointer>  // foom
#include <QSpinBox>  // foom
#include "tools/draw_path_tool.h"
#include "tools/lasso_tool.h"
#include "tools/sketch_tools.h"
#include "sketch/sketch.h"
#include "gui/map/sketch_dialog.h"
#include "gui/widgets/symbol_dropdown.h"
#include <QListWidget>
#include <QPushButton>
#include <QRectF>
#include <QSignalSpy>
#include "tools/move_parallel_tool.h"
#include "tools/reshape_tool.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/objects/text_object.h"
#include "core/ops/replace_text.h"
#include "global.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/symbols/symbol_setting_dialog.h"  // foom
#include "gui/map/map_find_feature.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/template_list_widget.h"  // foom
#include "templates/paint_on_template_feature.h"
#include "tools/edit_point_tool.h"
#include "tools/edit_tool.h"
#include "undo/undo_manager.h"
#include "util/util.h"
#ifdef MAPPER_TEST_GDAL  // foom
#  include <cpl_string.h>
#  include <gdal.h>
#  include "gdal/gdal_template.h"
#endif

using namespace OpenOrienteering;


/// Creates a test map and provides pointers to specific map elements.
/// NOTE: delete the map manually in case its ownership is not transferred to a MapEditorController or similar!
struct TestMap
{
	Map* map;
	
	LineSymbol* line_symbol;
	PathObject* line_object;
	
	TestMap();
	~TestMap();
};


/// Creates a test map editor and provides related pointers.
struct TestMapEditor
{
	MainWindow* window;
	MapEditorController* editor;
	MapWidget* map_widget;
	
	TestMapEditor(Map* map);
	TestMapEditor(const TestMapEditor&) = delete;
	TestMapEditor& operator=(const TestMapEditor&) = delete;
	~TestMapEditor();
	
	void simulateClick(const QPoint& pos);
	void simulateClick(const QPointF& pos);
	void simulateDrag(const QPoint& start_pos, const QPoint& end_pos);
	void simulateDrag(const QPointF& start_pos, const QPointF& end_pos);
};


// ### TestMap ###

TestMap::TestMap()
{
	MapCoord coord;
	
	map = new Map();
	
	auto black = new MapColor();
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setOpacity(1.0f);
	black->setName(QString::fromLatin1("black"));
	map->addColor(black, 0);
	
	line_symbol = new LineSymbol();
	line_symbol->setLineWidth(1);
	line_symbol->setColor(black);
	map->addSymbol(line_symbol, 0);
	
	line_object = new PathObject(line_symbol);
	line_object->addCoordinate(MapCoord(10, 10));
	coord = MapCoord(20, 10);
	coord.setCurveStart(true);
	line_object->addCoordinate(coord);
	line_object->addCoordinate(MapCoord(20, 20));
	line_object->addCoordinate(MapCoord(30, 20));
	line_object->addCoordinate(MapCoord(30, 10));
	map->addObject(line_object);
	
	// TODO: fill map with more content as needed
}

TestMap::~TestMap() = default;


// ### TestMapEditor ###

TestMapEditor::TestMapEditor(Map* map)
{
	window = new MainWindow();
	editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	map_widget = editor->getMainWidget();
}

TestMapEditor::~TestMapEditor()
{
	// The window may still be referred to by tools which are scheduled for
	// deleteLater(), so we need to postpone the window deletion, too.
	window->deleteLater();
}

void TestMapEditor::simulateClick(const QPoint& pos)
{
	QTest::mouseClick(map_widget, Qt::LeftButton, {}, pos);
}

void TestMapEditor::simulateClick(const QPointF& pos)
{
	simulateClick(pos.toPoint());
}

void TestMapEditor::simulateDrag(const QPoint& start_pos, const QPoint& end_pos)
{
	QTest::mousePress(map_widget, Qt::LeftButton, {}, start_pos);
	
	// NOTE: the implementation of QTest::mouseMove() does not seem to work (tries to set the real cursor position ...)
	//QTest::mouseMove(map_widget, end_pos);
	// Use manual workaround instead which sends an event directly:
	QMouseEvent event(QEvent::MouseMove, end_pos, map_widget->mapToGlobal(end_pos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(map_widget, &event);
	
	QTest::mouseRelease(map_widget, Qt::LeftButton, {}, end_pos);
}

void TestMapEditor::simulateDrag(const QPointF& start_pos, const QPointF& end_pos)
{
	simulateDrag(start_pos.toPoint(), end_pos.toPoint());
}


// ### ToolsTest ###

void ToolsTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	// foom: the settings which the tests change go to a folder of their own.
	static QTemporaryDir settings_dir;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
}


void ToolsTest::editTool()
{
	// Initialization
	TestMap map;
	TestMapEditor editor(map.map);
	EditTool* tool = new EditPointTool(editor.editor, nullptr);	// TODO: Refactor EditTool: MapEditorController and SymbolWidget pointers could be unnecessary
	editor.editor->setTool(tool);
	
	// Move the first coordinate of the line object
	const MapWidget* map_widget = editor.map_widget;
	const PathObject* object = map.line_object;
	
	QPointF drag_start_pos = map_widget->mapToViewport(object->getCoordinate(0));
	QPointF drag_end_pos = drag_start_pos + QPointF(0, -50);
	
	// Clear selection.
	map.map->clearObjectSelection(false);
	QVERIFY(map.map->selectedObjects().empty());
	
	// Click to select the object
	editor.simulateClick(drag_start_pos);
	QCOMPARE(map.map->getFirstSelectedObject(), object);
	
	// Drag the coordinate to the new position
	editor.simulateDrag(drag_start_pos, drag_end_pos);
	
	// Check position deviation
	QPointF difference = map_widget->mapToViewport(object->getCoordinate(0)) - drag_end_pos;
	QCOMPARE(qMax(qAbs(difference.x()), 0.1), 0.1);
	QCOMPARE(qMax(qAbs(difference.y()), 0.1), 0.1);
	
	// Cleanup
	editor.editor->setTool(nullptr);
}



void ToolsTest::paintOnTemplateFeature()
{
	// Designed for images of 100 mm at 10 pixel per mm.
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(20000), 200);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(15000), 100);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(10000), 100);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(7500) , 100);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(5000) , 50);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(4000) , 50);
	QCOMPARE(PaintOnTemplateFeature::alignmentBase(1000) , 10);
	
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(-347,  10), -350);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(-347,  20), -340);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(-347,  50), -350);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(-347, 100), -300);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(347,  10), 350);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(347,  20), 340);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(347,  50), 350);
	QCOMPARE(PaintOnTemplateFeature::roundToMultiple(347, 100), 300);
}


void ToolsTest::testFindObjects()
{
	auto* map = new Map;
	{
		auto* normal_point_symbol = new PointSymbol();
		map->addSymbol(normal_point_symbol, 0);
		
		auto* hidden_point_symbol = new PointSymbol();
		hidden_point_symbol->setHidden(true);
		map->addSymbol(hidden_point_symbol, 1);
		
		auto* protected_point_symbol = new PointSymbol();
		protected_point_symbol->setProtected(true);
		map->addSymbol(protected_point_symbol, 2);
		
		auto add_object = [map](Symbol* symbol, const char* label) {
			auto* object = new PointObject(symbol);
			object->setTag(QLatin1String("match"), QLatin1String(label));
			map->addObject(object);
		};
		add_object(normal_point_symbol, "yes");     // expected match
		add_object(normal_point_symbol, "no");
		add_object(normal_point_symbol, "yes");     // expected match
		add_object(hidden_point_symbol, "yes");
		add_object(normal_point_symbol, "yes");     // expected match
		add_object(protected_point_symbol, "yes");
	}
	
	TestMapEditor editor(map);  // taking ownership
	
	ObjectQuery query {QLatin1String("match"), ObjectQuery::OperatorIs, QLatin1String("yes")};
	QVERIFY(query);
	
	MapFindFeature::findAllMatchingObjects(*editor.editor, query);
	QCOMPARE(map->getNumSelectedObjects(), 3);
	
	MapFindFeature::findNextMatchingObject(*editor.editor, query);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	auto* first_match = map->getFirstSelectedObject();
	
	MapFindFeature::findNextMatchingObject(*editor.editor, query);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	QVERIFY(map->getFirstSelectedObject() != first_match);
	
	MapFindFeature::findNextMatchingObject(*editor.editor, query);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	QVERIFY(map->getFirstSelectedObject() != first_match);
	
	MapFindFeature::findNextMatchingObject(*editor.editor, query);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	QVERIFY(map->getFirstSelectedObject() == first_match);
}


/*
 * We select a non-standard QPA because we don't need a real GUI window.
 */
namespace  {
	auto const Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


void ToolsTest::toPolyline()
{
	// the design notes, "To polyline", Accept (editor).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("to-polyline");
	QVERIFY(action);
	
	auto* object = map.line_object;
	auto const uuid = object->uuid();
	QVERIFY(object->getRawCoordinateVector()[1].isCurveStart());
	map.map->addObjectToSelection(object, true);
	QVERIFY(action->isEnabled());
	auto const undo_steps = map.map->undoManager().undoStepCount();
	
	editor.editor->toPolyline(0.05);
	auto* part = map.map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 1);
	auto* flat = part->getObject(0);
	QVERIFY(flat != object);
	QCOMPARE(flat->uuid(), uuid);
	QVERIFY(flat->getRawCoordinateVector().size() > 4);
	for (auto const& coord : flat->getRawCoordinateVector())
		QVERIFY(!coord.isCurveStart());
	QVERIFY(map.map->selectedObjects() == (Map::ObjectSelection{ flat }));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0), object);
	QVERIFY(object->getRawCoordinateVector()[1].isCurveStart());
	
	map.map->clearObjectSelection(true);
	QVERIFY(!action->isEnabled());
}


void ToolsTest::toCurve()
{
	// the design notes, "Polyline to curve", Accept (editor).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("to-curve");
	QVERIFY(action);
	
	auto* object = map.line_object;
	auto const uuid = object->uuid();
	auto const old_coords = object->getRawCoordinateVector();
	QVERIFY(!old_coords[0].isCurveStart());
	map.map->addObjectToSelection(object, true);
	QVERIFY(action->isEnabled());
	auto const undo_steps = map.map->undoManager().undoStepCount();
	
	editor.editor->toCurve(ops::ToCurveParams());
	auto* part = map.map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 1);
	auto* curved = part->getObject(0);
	QVERIFY(curved != object);
	QCOMPARE(curved->uuid(), uuid);
	auto const& new_coords = curved->getRawCoordinateVector();
	QCOMPARE(new_coords.size(), old_coords.size() + 2);
	QVERIFY(new_coords[0].isCurveStart());
	for (std::size_t i = 1; i <= 4; ++i)
		QVERIFY(new_coords[new_coords.size() - i].isPositionEqualTo(old_coords[old_coords.size() - i]));
	QVERIFY(map.map->selectedObjects() == (Map::ObjectSelection{ curved }));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0), object);
	
	map.map->clearObjectSelection(true);
	QVERIFY(!action->isEnabled());
}


void ToolsTest::relinkTemplatesCommand()
{
	// the design notes, "Relink by folder", Accept (editor).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const old_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("old/a.png"));
	auto const new_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("new/a.png"));
	QVERIFY(QDir().mkpath(QFileInfo(new_path).absolutePath()));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(new_path, "PNG"));
	
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("templates-relink"));
	map.map->addTemplate(-1, std::make_unique<TemplateImage>(old_path, map.map));
	auto* temp = map.map->getTemplate(0);
	QVERIFY(!temp->loadTemplateFile());
	QCOMPARE(temp->getTemplateState(), Template::Invalid);
	
	QCOMPARE(editor.editor->relinkTemplates(QDir(dir.path()).absoluteFilePath(QStringLiteral("old")),
	                                        QDir(dir.path()).absoluteFilePath(QStringLiteral("new"))), 1);
	QCOMPARE(map.map->getTemplate(0)->getTemplateState(), Template::Loaded);
}


void ToolsTest::favouriteTemplatesCommand()
{
	// the design notes, "Favourite templates", Accept (editor).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("view-favourites-only");
	QVERIFY(action);
	QVERIFY(action->isCheckable());
	auto* view = editor.editor->getMainWidget()->getMapView();
	QVERIFY(!view->areFavouriteTemplatesOnly());
	action->trigger();
	QVERIFY(view->areFavouriteTemplatesOnly());
	QVERIFY(action->isChecked());
	action->trigger();
	QVERIFY(!view->areFavouriteTemplatesOnly());
}


void ToolsTest::embedTemplateCommand()
{
	// the design notes, "Embedded raster templates", Accept (editor).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("a.png"));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(path, "PNG"));
	
	TestMap map;
	TestMapEditor editor(map.map);
	map.map->addTemplate(-1, std::make_unique<TemplateImage>(path, map.map));
	auto* temp = qobject_cast<TemplateImage*>(map.map->getTemplate(0));
	QVERIFY(temp);
	QVERIFY(temp->loadTemplateFile());
	QString error;
	QVERIFY2(editor.editor->setTemplateEmbedded(temp, true, &error), qPrintable(error));
	QVERIFY(temp->isEmbedded());
	QVERIFY(editor.editor->setTemplateEmbedded(temp, false, &error));
	QVERIFY(!temp->isEmbedded());
	
	map.map->addTemplate(-1, std::make_unique<TemplateMap>(QDir(dir.path()).absoluteFilePath(QStringLiteral("b.omap")), map.map));
	error.clear();
	QVERIFY(!editor.editor->setTemplateEmbedded(map.map->getTemplate(1), true, &error));
	QVERIFY(!error.isEmpty());
}


void ToolsTest::downloadWindowCommand()
{
	// the design notes, "Download window on the map", Accept (2).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* widget = editor.map_widget;
	widget->resize(400, 300);
	QVERIFY(editor.editor->downloadWindow().isEmpty());

	auto const center = widget->getMapView()->center();
	auto const c = QPointF(center.x(), center.y());
	QPolygonF const outline{ QVector<QPointF>{ c + QPointF(-5, -5), c + QPointF(5, -5), c + QPointF(5, 5), c + QPointF(-5, 5) } };
	editor.editor->setDownloadWindow(outline);
	QCOMPARE(editor.editor->downloadWindow(), outline);

	auto const paint = [&]() {
		QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		editor.editor->drawTemplateAttributions(&painter, widget);
		painter.end();
		return image;
	};
	auto const edge = widget->mapToViewport(QPointF(c.x(), c.y() - 5)).toPoint();
	auto const inside = widget->mapToViewport(c).toPoint();
	auto const outside = widget->mapToViewport(c + QPointF(9, 9)).toPoint();
	QVERIFY(QRect(QPoint(0, 0), widget->size()).contains(outside));
	auto image = paint();
	QVERIFY(qAlpha(image.pixel(edge)) > 100);
	QVERIFY(qAlpha(image.pixel(inside)) > 0);
	QVERIFY(qAlpha(image.pixel(inside)) < 100);
	QCOMPARE(qAlpha(image.pixel(outside)), 0);

	editor.editor->setDownloadWindow({});
	QVERIFY(editor.editor->downloadWindow().isEmpty());
	image = paint();
	QCOMPARE(qAlpha(image.pixel(edge)), 0);
}

void ToolsTest::templateSeparationCommand()
{
	// the design notes, "Templates on a spot colour plate", Accept (editor).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("a.png"));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(path, "PNG"));
	
	TestMap map;
	TestMapEditor editor(map.map);
	auto* spot = new MapColor(QStringLiteral("Brown"), map.map->getNumColors());
	spot->setSpotColorName(QStringLiteral("BROWN"));
	map.map->addColor(spot, map.map->getNumColors());
	auto* process = new MapColor(QStringLiteral("Process"), map.map->getNumColors());
	map.map->addColor(process, map.map->getNumColors());
	map.map->addTemplate(-1, std::make_unique<TemplateImage>(path, map.map));
	auto* temp = map.map->getTemplate(0);
	
	QString error;
	QVERIFY2(editor.editor->setTemplateSeparation(temp, spot, &error), qPrintable(error));
	QCOMPARE(temp->separationColor(), spot);
	QVERIFY(!editor.editor->setTemplateSeparation(temp, process, &error));
	QVERIFY(!error.isEmpty());
	QCOMPARE(temp->separationColor(), spot);
	map.map->addTemplate(-1, std::make_unique<TemplateMap>(QDir(dir.path()).absoluteFilePath(QStringLiteral("b.omap")), map.map));
	error.clear();
	QVERIFY(!editor.editor->setTemplateSeparation(map.map->getTemplate(1), spot, &error));
	QVERIFY(!error.isEmpty());
	QVERIFY(editor.editor->setTemplateSeparation(temp, nullptr, &error));
	QVERIFY(!temp->separationColor());
}


void ToolsTest::templateEffectsCommand()
{
	// the design notes, "Block out, dim and colour infrared", Accept (editor).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("a.png"));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(path, "PNG"));

	TestMap map;
	TestMapEditor editor(map.map);
	map.map->addTemplate(-1, std::make_unique<TemplateImage>(path, map.map));
	auto* temp = map.map->getTemplate(0);
	QVERIFY(temp->loadTemplateFile());
	map.map->addTemplate(-1, std::make_unique<TemplateMap>(QDir(dir.path()).absoluteFilePath(QStringLiteral("b.omap")), map.map));
	auto* map_template = map.map->getTemplate(1);

	editor.editor->setTemplateBlockOut(temp, true);
	QVERIFY(temp->isBlockOut());
	editor.editor->setTemplateBlockOut(map_template, true);
	QVERIFY(map_template->isBlockOut());

	QString error;
	QVERIFY2(editor.editor->setTemplateDim(temp, 0.3, &error), qPrintable(error));
	QCOMPARE(temp->dim(), 0.3);
	QVERIFY(!editor.editor->setTemplateDim(map_template, 0.3, &error));
	QVERIFY(!error.isEmpty());
	QCOMPARE(map_template->dim(), 0.0);

	error.clear();
	QVERIFY(!editor.editor->setTemplateInfrared(temp, true, &error));
	QVERIFY(!error.isEmpty());
	QVERIFY(!TemplateListWidget::infraredOffered(temp));
#ifdef MAPPER_TEST_GDAL
	auto const write_tiff = [](const QString& tiff, int bands) {
		GDALAllRegister();
		char** options = nullptr;
		options = CSLSetNameValue(options, "PHOTOMETRIC", "RGB");
		if (bands == 4)
			options = CSLSetNameValue(options, "ALPHA", "UNSPECIFIED");
		auto dataset = GDALCreate(GDALGetDriverByName("GTiff"), tiff.toUtf8().constData(), 4, 4, bands, GDT_Byte, options);
		CSLDestroy(options);
		if (!dataset)
			return false;
		if (bands == 4)
			GDALSetRasterColorInterpretation(GDALGetRasterBand(dataset, 4), GCI_Undefined);
		GDALClose(dataset);
		return true;
	};
	auto const cir = QDir(dir.path()).absoluteFilePath(QStringLiteral("cir.tif"));
	auto const rgb = QDir(dir.path()).absoluteFilePath(QStringLiteral("rgb.tif"));
	QVERIFY(write_tiff(cir, 4));
	QVERIFY(write_tiff(rgb, 3));
	map.map->addTemplate(-1, std::make_unique<GdalTemplate>(cir, map.map));
	auto* four = map.map->getTemplate(2);
	QVERIFY(four->loadTemplateFile());
	map.map->addTemplate(-1, std::make_unique<GdalTemplate>(rgb, map.map));
	auto* three = map.map->getTemplate(3);
	QVERIFY(three->loadTemplateFile());
	QVERIFY(TemplateListWidget::infraredOffered(four));
	QVERIFY(!TemplateListWidget::infraredOffered(three));
	error.clear();
	QVERIFY2(editor.editor->setTemplateInfrared(four, true, &error), qPrintable(error));
	QVERIFY(four->isInfrared());
	QVERIFY(!editor.editor->setTemplateInfrared(three, true, &error));
	QVERIFY(!three->isInfrared());
#endif
}


void ToolsTest::stairwayTool()
{
	// the design notes, "Stairway", Accept (4).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("draw-stairway");
	QVERIFY(action);
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	QVERIFY(action->isEnabled());
	auto const objects_before = map.map->getNumObjects();
	editor.editor->drawStairway(1.0);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(10.0, 0.0));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(10.0, 0.0));
	move(viewport(10.0, 4.0));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(10.0, 4.0));
	QTest::keyClick(widget, Qt::Key_Return);
	QCOMPARE(map.map->getNumObjects(), objects_before + 5);
	QCOMPARE(map.map->getNumSelectedObjects(), 5);
	auto group = 0;
	for (auto const* object : map.map->selectedObjects())
	{
		QVERIFY(object->group() != 0);
		if (group == 0)
			group = object->group();
		QCOMPARE(object->group(), group);
	}
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects_before);
	editor.editor->setTool(nullptr);
}

void ToolsTest::openRectangle()
{
	// the design notes, "Open rectangles", Accept.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* open = editor.editor->getAction("draw-rectangle-open");
	QVERIFY(open);
	QVERIFY(open->isCheckable());
	QVERIFY(!open->isChecked());
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	auto const drawRectangle = [&]() -> const PathObject* {
		auto const objects_before = map.map->getCurrentPart()->getNumObjects();
		editor.editor->drawRectangleClicked();
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
		move(viewport(10.0, 0.0));
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(10.0, 0.0));
		move(viewport(10.0, 5.0));
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(10.0, 5.0));
		QTest::keyClick(widget, Qt::Key_Return);
		auto const* part = map.map->getCurrentPart();
		if (part->getNumObjects() != objects_before + 1)
			return nullptr;
		auto const* object = part->getObject(part->getNumObjects() - 1);
		editor.editor->setTool(nullptr);
		return object->getType() == Object::Path ? object->asPath() : nullptr;
	};
	
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	open->trigger();
	QVERIFY(open->isChecked());
	auto const* path = drawRectangle();
	QVERIFY(path);
	QVERIFY(!path->parts().front().isClosed());
	QCOMPARE(path->getCoordinateCount(), MapCoordVector::size_type(4));
	// The clicks lie on whole pixels.
	auto const near = [path](int i, double x, double y) { return std::abs(path->getCoordinate(i).x() - x) < 0.2 && std::abs(path->getCoordinate(i).y() - y) < 0.2; };
	QVERIFY(near(0, 0.0, 0.0));
	QVERIFY(near(1, 10.0, 0.0));
	QVERIFY(near(2, 10.0, 5.0));
	QVERIFY(near(3, 0.0, 5.0));
	
	open->trigger();
	path = drawRectangle();
	QVERIFY(path);
	QVERIFY(path->parts().front().isClosed());
	
	open->trigger();
	auto* area = new AreaSymbol();
	area->setColor(map.map->getColor(0));
	map.map->addSymbol(area, map.map->getNumSymbols());
	editor.editor->getSymbolWidget()->selectSingleSymbol(area);
	path = drawRectangle();
	QVERIFY(path);
	QVERIFY(path->parts().front().isClosed());
}

void ToolsTest::frameCommand()
{
	// the design notes, "Frame around the map", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("draw-frame"));
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	auto const objects_before = map.map->getNumObjects();
	editor.editor->drawFrame(1.0);
	QCOMPARE(map.map->getNumObjects(), objects_before + 1);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects_before);
}


void ToolsTest::pointsAlongTool()
{
	// the design notes, "Points along a drawn path", Accept.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* point = new PointSymbol();
	map.map->addSymbol(point, map.map->getNumSymbols());
	auto* action = editor.editor->getAction("draw-points-along");
	QVERIFY(action);
	editor.editor->getSymbolWidget()->selectSingleSymbol(point);
	QVERIFY(action->isEnabled());
	
	auto const objects_before = map.map->getNumObjects();
	DistributePointsTool::Settings settings;
	settings.num_points_per_line = 5;
	settings.points_at_ends = true;
	editor.editor->drawPointsAlong(settings);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(20.0, 0.0));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(20.0, 0.0));
	QTest::keyClick(widget, Qt::Key_Return);
	QCOMPARE(map.map->getNumObjects(), objects_before + 5);
	QCOMPARE(map.map->getNumSelectedObjects(), 5);
	std::vector<double> xs;
	for (auto const* object : map.map->selectedObjects())
	{
		QCOMPARE(object->getType(), Object::Point);
		QCOMPARE(object->getSymbol(), point);
		QVERIFY(std::abs(object->asPoint()->getCoordF().y()) < 0.2);
		xs.push_back(object->asPoint()->getCoordF().x());
	}
	std::sort(xs.begin(), xs.end());
	for (std::size_t i = 0; i < xs.size(); ++i)
		QVERIFY2(std::abs(xs[i] - 5.0 * double(i)) < 0.2, qPrintable(QString::number(xs[i])));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects_before);
	editor.editor->setTool(nullptr);
}

void ToolsTest::sizedShapeTool()
{
	// the design notes, "Shapes of a given size", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("draw-sized-shape");
	QVERIFY(action);
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	QVERIFY(action->isEnabled());
	auto const objects_before = map.map->getNumObjects();
	ops::SizedShapeParams params;
	params.kind = ops::SizedShapeParams::Circle;
	params.width = 2.0;
	editor.editor->drawSizedShape(params);
	auto* widget = editor.map_widget;
	QTest::mouseClick(widget, Qt::LeftButton, {}, widget->mapToViewport(MapCoord(10.0, 10.0)).toPoint());
	QCOMPARE(map.map->getNumObjects(), objects_before + 1);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	auto* shape = map.map->getFirstSelectedObject();
	shape->update();
	auto const centre = shape->getExtent().center();
	QVERIFY2(std::abs(centre.x() - 10.0) < 0.2 && std::abs(centre.y() - 10.0) < 0.2, qPrintable(QStringLiteral("%1 %2").arg(centre.x()).arg(centre.y())));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects_before);
	editor.editor->setTool(nullptr);
}

void ToolsTest::generatorPlaceTool()
{
	// the design notes, "Generators", third change, Accept (2) and (3).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* widget = editor.map_widget;
	auto const scale_bar = QStringLiteral("gen-scale-bar");
	// A message box of the generator would wait for a click: close it, and fail with its text.
	QString message;
	QTimer watchdog;
	QObject::connect(&watchdog, &QTimer::timeout, [&message] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
		{
			message = box->text();
			box->reject();
		}
	});
	watchdog.start(200);
	auto const parts_before = map.map->getNumParts();
	auto const part_before = map.map->getCurrentPartIndex();
	auto const objects_before = map.map->getNumObjects();
	auto const steps_before = map.map->undoManager().undoStepCount();
	
	QVERIFY(editor.editor->placeGenerator(scale_bar, defaultGeneratorParams(scale_bar, *map.map)));
	QVERIFY(qobject_cast<GeneratorPlaceTool*>(editor.editor->getTool()));
	QCOMPARE(map.map->getNumObjects(), objects_before);
	QTest::mouseMove(widget, widget->mapToViewport(MapCoord(15.0, 25.0)).toPoint());
	QTest::mouseClick(widget, Qt::LeftButton, {}, widget->mapToViewport(MapCoord(10.0, 20.0)).toPoint());
	QVERIFY(!qobject_cast<GeneratorPlaceTool*>(editor.editor->getTool()));
	QCOMPARE(map.map->getNumParts(), parts_before + 1);
	auto const layout = layoutPartIndex(*map.map);
	QVERIFY(layout >= 0);
	QCOMPARE(int(map.map->getCurrentPartIndex()), layout);
	auto const* part = map.map->getPart(std::size_t(layout));
	QVERIFY(part->getNumObjects() > 6);
	QCOMPARE(map.map->getNumSelectedObjects(), part->getNumObjects());
	auto const* frame = part->getObject(0);
	auto const corner = MapCoordF(frame->getRawCoordinateVector().front());
	QVERIFY2(std::abs(corner.x() - 10.0) < 0.2 && std::abs(corner.y() - 20.0) < 0.2, qPrintable(QStringLiteral("%1 %2").arg(corner.x()).arg(corner.y())));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumParts(), parts_before);
	QCOMPARE(map.map->getNumObjects(), objects_before);
	// Accept (3): the part made for the bar went with it, so the part before is current again.
	QCOMPARE(map.map->getCurrentPartIndex(), part_before);
	
	// With a layout part already there, undo makes the part it changed current, as every undo does.
	map.map->addPart(new MapPart(QStringLiteral("Layout"), map.map), std::size_t(map.map->getNumParts()));
	map.map->getPart(std::size_t(map.map->getNumParts() - 1))->setRole(MapPart::Role::Layout);
	map.map->setCurrentPartIndex(part_before);
	QVERIFY(editor.editor->placeGenerator(scale_bar, defaultGeneratorParams(scale_bar, *map.map)));
	QTest::mouseClick(widget, Qt::LeftButton, {}, widget->mapToViewport(MapCoord(10.0, 20.0)).toPoint());
	QCOMPARE(int(map.map->getCurrentPartIndex()), layoutPartIndex(*map.map));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(int(map.map->getCurrentPartIndex()), layoutPartIndex(*map.map));
	QCOMPARE(map.map->getNumObjects(), objects_before);
	map.map->setCurrentPartIndex(part_before);
	map.map->removePart(std::size_t(layoutPartIndex(*map.map)));
	
	// Esc changes nothing.
	auto const steps = map.map->undoManager().undoStepCount();
	QVERIFY(editor.editor->placeGenerator(scale_bar, defaultGeneratorParams(scale_bar, *map.map)));
	QVERIFY(qobject_cast<GeneratorPlaceTool*>(editor.editor->getTool()));
	QTest::keyClick(widget, Qt::Key_Escape);
	QVERIFY(!qobject_cast<GeneratorPlaceTool*>(editor.editor->getTool()));
	QCOMPARE(map.map->getNumObjects(), objects_before);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps);
	Q_UNUSED(steps_before)
	
	// North lines have no anchor: they are added at once.
	auto const north_lines = QStringLiteral("gen-north-lines");
	auto north_params = defaultGeneratorParams(north_lines, *map.map);
	// The test map's line is tiny: an outline of 100 mm, lines 20 mm apart on paper.
	north_params.insert(QStringLiteral("spacing_m"), 0.02 * map.map->getScaleDenominator());
	north_params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
	north_params.insert(QStringLiteral("outline"), QJsonArray{ QJsonArray{ 0, 0 }, QJsonArray{ 100, 0 }, QJsonArray{ 100, 100 }, QJsonArray{ 0, 100 } });
	auto const placed = editor.editor->placeGenerator(north_lines, north_params);
	QVERIFY2(placed, qPrintable(message));
	QVERIFY(!qobject_cast<GeneratorPlaceTool*>(editor.editor->getTool()));
	QVERIFY(map.map->getNumObjects() > objects_before);
	editor.editor->setTool(nullptr);
}

void ToolsTest::freehandSmoothing()
{
	// the design notes, "Freehand smoothing", Accept.
	TestMap map;
	TestMapEditor editor(map.map);
	char const* const ids[4] = { "freehand-smoothing-off", "freehand-smoothing-slight", "freehand-smoothing-medium", "freehand-smoothing-strong" };
	for (auto const* id : ids)
		QVERIFY2(editor.editor->getAction(id), id);
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const drawArc = [&]() -> const PathObject* {
		auto const objects_before = map.map->getCurrentPart()->getNumObjects();
		editor.editor->drawFreehandClicked();
		QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
		for (int i = 1; i <= 64; ++i)
		{
			auto const t = M_PI * i / 64;
			auto const pos = viewport(10.0 - 10.0 * std::cos(t), 10.0 * std::sin(t));
			QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(widget, &event);
		}
		QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(20.0, 0.0));
		auto const* part = map.map->getCurrentPart();
		editor.editor->setTool(nullptr);
		if (part->getNumObjects() != objects_before + 1)
			return nullptr;
		return part->getObject(part->getNumObjects() - 1)->asPath();
	};
	auto const curvesOf = [](const PathObject* path) {
		auto curves = 0;
		for (auto const& coord : path->getRawCoordinateVector())
			curves += coord.isCurveStart() ? 1 : 0;
		return curves;
	};
	auto const endsNear = [](const PathObject* path) {
		auto const& first = path->getRawCoordinateVector().front();
		auto const& last = path->getRawCoordinateVector().back();
		return std::abs(first.x()) < 0.3 && std::abs(first.y()) < 0.3 && std::abs(last.x() - 20.0) < 0.3 && std::abs(last.y()) < 0.3;
	};
	
	editor.editor->getAction(ids[0])->trigger();
	QVERIFY(editor.editor->getAction(ids[0])->isChecked());
	QCOMPARE(QSettings().value(QStringLiteral("Editor/freehand_smoothing")).toInt(), 0);
	auto const* plain = drawArc();
	QVERIFY(plain);
	QCOMPARE(curvesOf(plain), 0);
	QVERIFY(endsNear(plain));
	auto const plain_count = plain->getCoordinateCount();
	
	editor.editor->getAction(ids[2])->trigger();
	QVERIFY(editor.editor->getAction(ids[2])->isChecked());
	QVERIFY(!editor.editor->getAction(ids[0])->isChecked());
	QCOMPARE(QSettings().value(QStringLiteral("Editor/freehand_smoothing")).toInt(), 2);
	auto const* smooth = drawArc();
	QVERIFY(smooth);
	QVERIFY(curvesOf(smooth) > 0);
	QVERIFY(endsNear(smooth));
	QVERIFY2(smooth->getCoordinateCount() - 2 * MapCoordVector::size_type(curvesOf(smooth)) <= plain_count,
	         qPrintable(QStringLiteral("%1 %2").arg(smooth->getCoordinateCount()).arg(plain_count)));
}

void ToolsTest::autoJoin()
{
	// the design notes, "Join while drawing", Accept.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("auto-join");
	QVERIFY(action);
	QVERIFY(action->isCheckable());
	QVERIFY(!action->isChecked());
	auto* part = map.map->getCurrentPart();
	// A map of its own line alone.
	while (part->getNumObjects() > 0)
		map.map->deleteObject(part->getObject(0));
	auto* old_line = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.map->addObject(old_line);
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	auto const drawLine = [&](double from_x) {
		editor.editor->drawPathClicked();
		// The start lies exactly at the place: clicks fall on whole pixels.
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(from_x, 0.0));
		move(viewport(20.0, 0.0));
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(20.0, 0.0));
		QTest::keyClick(widget, Qt::Key_Return, Qt::ControlModifier);  // Return alone closes the path
		editor.editor->setTool(nullptr);
	};
	
	action->trigger();
	QVERIFY(action->isChecked());
	auto const pixel = widget->getMapView()->pixelToLength(1) / 1000.0;  // mm
	QVERIFY2(pixel < 0.3, qPrintable(QString::number(pixel)));  // a click lies within half a pixel of its place
	drawLine(10.1);
	QCOMPARE(part->getNumObjects(), 1);
	auto const* joined = part->getObject(0)->asPath();
	QCOMPARE(joined->getSymbol(), map.line_symbol);
	auto const& coords = joined->getRawCoordinateVector();
	auto const x_min = std::min(coords.front().x(), coords.back().x());
	auto const x_max = std::max(coords.front().x(), coords.back().x());
	QVERIFY2(std::abs(x_min) < 0.2 && std::abs(x_max - 20.0) < 0.2, qPrintable(QStringLiteral("%1 %2").arg(x_min).arg(x_max)));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0)->asPath()->getCoordinateCount(), MapCoordVector::size_type(2));
	
	drawLine(11.0);
	QCOMPARE(part->getNumObjects(), 2);
	QVERIFY(map.map->undoManager().undo());
	
	action->trigger();
	QVERIFY(!action->isChecked());
	drawLine(10.1);
	QCOMPARE(part->getNumObjects(), 2);
}


void ToolsTest::mirror()
{
	// the design notes, "Mirror", editor paragraph.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("mirror");
	QVERIFY(action);
	
	auto* object = map.line_object;
	auto const uuid = object->uuid();
	auto const coords = object->getRawCoordinateVector();
	map.map->addObjectToSelection(object, true);
	QVERIFY(action->isEnabled());
	auto const undo_steps = map.map->undoManager().undoStepCount();
	
	editor.editor->mirror(90.0, false);
	auto* part = map.map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 1);
	auto* flipped = part->getObject(0);
	QVERIFY(flipped != object);
	QCOMPARE(flipped->uuid(), uuid);
	QVERIFY(flipped->getRawCoordinateVector() != coords);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getObject(0), object);
	QCOMPARE(object->getRawCoordinateVector(), coords);
	
	map.map->clearObjectSelection(true);
	QVERIFY(!action->isEnabled());
}


void ToolsTest::numericTransforms()
{
	// the design notes, "Numeric transforms", editor paragraph.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* move_act = editor.editor->getAction("move-by");
	auto* rotate_act = editor.editor->getAction("rotate-by");
	auto* stretch_act = editor.editor->getAction("stretch");
	QVERIFY(move_act && rotate_act && stretch_act);
	
	auto* object = map.line_object;
	auto const uuid = object->uuid();
	auto const coords = object->getRawCoordinateVector();
	map.map->addObjectToSelection(object, true);
	QVERIFY(move_act->isEnabled() && rotate_act->isEnabled() && stretch_act->isEnabled());
	auto const undo_steps = map.map->undoManager().undoStepCount();
	auto* part = map.map->getCurrentPart();
	
	editor.editor->moveBy(1.0, 2.0, false);
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0)->uuid(), uuid);
	QCOMPARE(part->getObject(0)->getRawCoordinateVector().front(), MapCoord(coords.front().x() + 1.0, coords.front().y() + 2.0));
	editor.editor->rotateBy(90.0, false, false);
	editor.editor->stretch(2.0, 0.5, false);
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0)->uuid(), uuid);
	QVERIFY(part->getObject(0)->getRawCoordinateVector() != coords);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 3);
	
	for (int i = 0; i < 3; ++i)
		QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getObject(0), object);
	QCOMPARE(object->getRawCoordinateVector(), coords);
	
	map.map->clearObjectSelection(true);
	QVERIFY(!move_act->isEnabled() && !rotate_act->isEnabled() && !stretch_act->isEnabled());
}


void ToolsTest::alignAndDistribute()
{
	// the design notes, "Align and distribute", editor paragraph.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* align_act = editor.editor->getAction("align-left");
	auto* distribute_act = editor.editor->getAction("distribute-h");
	QVERIFY(align_act && distribute_act);
	for (auto id : { "align-hcenter", "align-right", "align-top", "align-vcenter", "align-bottom", "distribute-v" })
		QVERIFY(editor.editor->getAction(id));
	
	// The fixture line spans x 10..30, y 10..20.
	auto* first = map.line_object;
	auto* second = new PathObject(map.line_symbol, { MapCoord(30.0, 30.0), MapCoord(40.0, 30.0) });
	map.map->addObject(second);
	auto* third = new PathObject(map.line_symbol, { MapCoord(100.0, 50.0), MapCoord(110.0, 50.0) });
	map.map->addObject(third);
	auto* part = map.map->getCurrentPart();
	auto const second_uuid = second->uuid();
	auto const third_uuid = third->uuid();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	
	map.map->addObjectToSelection(first, true);
	QVERIFY(!align_act->isEnabled() && !distribute_act->isEnabled());
	map.map->addObjectToSelection(second, true);
	QVERIFY(align_act->isEnabled() && !distribute_act->isEnabled());
	map.map->addObjectToSelection(third, true);
	QVERIFY(align_act->isEnabled() && distribute_act->isEnabled());
	
	// Centres x 20, 35, 105: the second moves to 62.5.
	editor.editor->distribute(true);
	QCOMPARE(part->getNumObjects(), 3);
	auto const* spaced = map.map->findObject(second_uuid);
	QVERIFY(spaced && spaced != second);
	QCOMPARE(spaced->getRawCoordinateVector(), (MapCoordVector{ MapCoord(57.5, 30.0), MapCoord(67.5, 30.0) }));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	
	// Top edges to the first-selected object's y 10.
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(first, true);
	map.map->addObjectToSelection(const_cast<Object*>(spaced), true);
	map.map->addObjectToSelection(third, true);
	editor.editor->align(ops::AlignParams::Top);
	QCOMPARE(part->getNumObjects(), 3);
	QCOMPARE(map.map->findObject(second_uuid)->getRawCoordinateVector(), (MapCoordVector{ MapCoord(57.5, 10.0), MapCoord(67.5, 10.0) }));
	QCOMPARE(map.map->findObject(third_uuid)->getRawCoordinateVector(), (MapCoordVector{ MapCoord(100.0, 10.0), MapCoord(110.0, 10.0) }));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 2);
	
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(second_uuid), second);
	QCOMPARE(second->getRawCoordinateVector(), (MapCoordVector{ MapCoord(30.0, 30.0), MapCoord(40.0, 30.0) }));
	QCOMPARE(map.map->findObject(third_uuid), third);
}


void ToolsTest::reshape()
{
	// the design notes, "Reshape", editor paragraphs.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* line = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) });
	map.map->addObject(line);
	auto const uuid = line->uuid();
	auto* part = map.map->getCurrentPart();
	auto const num_objects = part->getNumObjects();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	
	auto* action = editor.editor->getAction("reshape");
	QVERIFY(action);
	action->trigger();
	auto* tool = dynamic_cast<ReshapeTool*>(editor.editor->getTool());
	QVERIFY(tool);
	
	// A click away from every object starts nothing (the snap distance is 10 mm in this view).
	auto const* map_widget = editor.map_widget;
	editor.simulateClick(map_widget->mapToViewport(MapCoord(15.0, -15.0)));
	QVERIFY(!tool->editingInProgress());
	
	// Three clicks on and above the line, then Return.
	editor.simulateClick(map_widget->mapToViewport(MapCoord(10.0, 0.0)));
	QVERIFY(tool->editingInProgress());
	editor.simulateClick(map_widget->mapToViewport(MapCoord(15.0, 5.0)));
	editor.simulateClick(map_widget->mapToViewport(MapCoord(20.0, 0.0)));
	QTest::keyClick(editor.map_widget, Qt::Key_Return);
	QVERIFY(!tool->editingInProgress());
	QCOMPARE(part->getNumObjects(), num_objects);
	auto const* reshaped = map.map->findObject(uuid);
	QVERIFY(reshaped && reshaped != line);
	auto const& coords = reshaped->asPath()->getRawCoordinateVector();
	QCOMPARE(coords.size(), MapCoordVector::size_type(5));
	QCOMPARE(coords.front(), MapCoord(0.0, 0.0));
	QCOMPARE(coords.back(), MapCoord(30.0, 0.0));
	auto const expected = MapCoordVector{ MapCoord(10.0, 0.0), MapCoord(15.0, 5.0), MapCoord(20.0, 0.0) };
	for (std::size_t i = 0; i < 3; ++i)
	{
		QVERIFY(std::abs(coords[i+1].x() - expected[i].x()) < 0.2);
		QVERIFY(std::abs(coords[i+1].y() - expected[i].y()) < 0.2);
	}
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(uuid), line);
	
	editor.editor->setTool(nullptr);
}


void ToolsTest::moveParallel()
{
	// the design notes, "Parallel offset", editor paragraphs.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* line = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) });
	map.map->addObject(line);
	auto const uuid = line->uuid();
	auto* part = map.map->getCurrentPart();
	auto const num_objects = part->getNumObjects();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	auto* tool_act = editor.editor->getAction("move-parallel");
	auto* by_act = editor.editor->getAction("move-parallel-by");
	QVERIFY(tool_act && by_act);
	map.map->clearObjectSelection(true);
	QVERIFY(!tool_act->isEnabled() && !by_act->isEnabled());
	map.map->addObjectToSelection(line, true);
	QVERIFY(tool_act->isEnabled() && by_act->isEnabled());
	
	// The command.
	editor.editor->moveParallelBy(2.0, false);
	QCOMPARE(part->getNumObjects(), num_objects);
	auto const* moved = map.map->findObject(uuid);
	QVERIFY(moved && moved != line);
	QCOMPARE(moved->asPath()->getRawCoordinateVector(), (MapCoordVector{ MapCoord(0.0, -2.0), MapCoord(20.0, -2.0) }));
	editor.editor->moveParallelBy(2.0, true);
	QCOMPARE(part->getNumObjects(), num_objects + 1);
	QCOMPARE(map.map->findObject(uuid), moved);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 2);
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(uuid), line);
	QCOMPARE(part->getNumObjects(), num_objects);
	
	// The tool: drag a point of the line 5 mm upwards.
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(line, true);
	tool_act->trigger();
	QVERIFY(dynamic_cast<MoveParallelTool*>(editor.editor->getTool()));
	auto const* map_widget = editor.map_widget;
	editor.simulateDrag(map_widget->mapToViewport(MapCoord(10.0, 0.0)), map_widget->mapToViewport(MapCoord(10.0, -5.0)));
	QCOMPARE(part->getNumObjects(), num_objects);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	auto const* dragged = map.map->findObject(uuid);
	QVERIFY(dragged);
	auto const& coords = dragged->asPath()->getRawCoordinateVector();
	QCOMPARE(coords.size(), MapCoordVector::size_type(2));
	QVERIFY(std::abs(coords.front().y() + 5.0) < 0.2);
	QVERIFY(std::abs(coords.back().y() + 5.0) < 0.2);
	QVERIFY(std::abs(coords.front().x()) < 0.2 && std::abs(coords.back().x() - 20.0) < 0.2);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(uuid)->asPath()->getRawCoordinateVector(), (MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) }));
	
	editor.editor->setTool(nullptr);
}


void ToolsTest::vertexType()
{
	// the design notes, "Vertex types", editor paragraph.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* normal_act = editor.editor->getAction("set-vertex-type-normal");
	auto* dash_act = editor.editor->getAction("set-vertex-type-dash");
	auto* keep_act = editor.editor->getAction("set-vertex-type-keep-ends");
	QVERIFY(normal_act && dash_act && keep_act);
	QVERIFY(keep_act->isCheckable());
	
	auto* line = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(20.0, 0.0) });
	map.map->addObject(line);
	auto const uuid = line->uuid();
	auto* part = map.map->getCurrentPart();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	map.map->clearObjectSelection(true);
	QVERIFY(!normal_act->isEnabled() && !dash_act->isEnabled());
	map.map->addObjectToSelection(line, true);
	QVERIFY(normal_act->isEnabled() && dash_act->isEnabled());
	
	editor.editor->setVertexType(true, true);
	auto const* flagged = map.map->findObject(uuid);
	QVERIFY(flagged && flagged != line);
	auto const& coords = flagged->asPath()->getRawCoordinateVector();
	QVERIFY(!coords[0].isDashPoint() && coords[1].isDashPoint() && !coords[2].isDashPoint());
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(uuid), line);
	QCOMPARE(part->getNumObjects(), 2);
}


void ToolsTest::sketchTools()
{
	// the design notes, "Sketch layer", second change.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	for (auto id : { "sketch-pen", "sketch-eraser", "sketch-note", "sketch-manage", "sketch-flatten" })
		QVERIFY2(editor.editor->getAction(id), id);
	
	// The pen: press, two moves, release.
	SketchPenTool::setPen(3, 2);
	editor.editor->getAction("sketch-pen")->trigger();
	QVERIFY(dynamic_cast<SketchPenTool*>(editor.editor->getTool()));
	QCOMPARE(sketchPartIndex(*map.map), -1);
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 40.0));
	move(viewport(20.0, 40.0));
	move(viewport(40.0, 40.0));
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(40.0, 40.0));
	auto const index = sketchPartIndex(*map.map);
	QVERIFY(index >= 0);
	auto const* part = map.map->getPart(std::size_t(index));
	QCOMPARE(part->getNumObjects(), 1);
	auto const* stroke = part->getObject(0);
	QCOMPARE(stroke->getSymbol(), sketchSymbol(*map.map, 3, 2));
	stroke->update();
	QVERIFY2(qAbs(stroke->getExtent().left() - 0.0) < 1.5, qPrintable(QString::number(stroke->getExtent().left())));
	QVERIFY2(qAbs(stroke->getExtent().right() - 40.0) < 1.5, qPrintable(QString::number(stroke->getExtent().right())));
	QVERIFY(qAbs(stroke->getExtent().center().y() - 40.0) < 1.0);
	QCOMPARE(map.map->getCurrentPartIndex(), std::size_t(0));
	
	// The eraser: a click cuts the stroke, a drag removes the right piece.
	editor.editor->getAction("sketch-eraser")->trigger();
	QVERIFY(dynamic_cast<SketchEraserTool*>(editor.editor->getTool()));
	editor.simulateClick(viewport(20.0, 40.0));
	QCOMPARE(part->getNumObjects(), 2);
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(21.0, 40.0));
	move(viewport(30.0, 40.0));
	move(viewport(41.0, 40.0));
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(41.0, 40.0));
	QCOMPARE(part->getNumObjects(), 1);
	part->getObject(0)->update();
	QVERIFY(part->getObject(0)->getExtent().right() < 20.0);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), 2);
	
	// The note, in the pen's colour.
	editor.editor->getAction("sketch-note")->trigger();
	auto* note_tool = dynamic_cast<SketchNoteTool*>(editor.editor->getTool());
	QVERIFY(note_tool);
	QCOMPARE(note_tool->addNote(MapCoordF(10, 60), QStringLiteral("  ")), 0);
	QVERIFY(note_tool->addNote(MapCoordF(10, 60), QStringLiteral("Gate")) != 0);
	QCOMPARE(part->getNumObjects(), 4);
	QCOMPARE(sketchItems(*map.map).back().label, QStringLiteral("Gate"));
	QCOMPARE(sketchItems(*map.map).back().objects.front()->getSymbol()->asText()->getColor()->getName(), sketchColorName(3));
	editor.editor->setTool(nullptr);
	
	// The list.
	SketchManageDialog dialog(*map.map);
	auto* list = dialog.findChild<QListWidget*>(QStringLiteral("items"));
	auto* symbol_box = dialog.findChild<SymbolDropDown*>(QStringLiteral("symbol"));
	auto* convert = dialog.findChild<QPushButton*>(QStringLiteral("convert"));
	auto* message = dialog.findChild<QLabel*>(QStringLiteral("message"));
	QVERIFY(list && symbol_box && convert && message);
	QCOMPARE(list->count(), 3);
	QVERIFY(list->item(2)->text().contains(QLatin1String("Gate")));
	QSignalSpy shown(&dialog, &SketchManageDialog::showExtent);
	list->setCurrentRow(2);
	QCOMPARE(shown.count(), 1);
	QVERIFY(shown.first().first().toRectF().contains(QPointF(10, 60)));
	// A note does not become a line.
	symbol_box->setSymbol(map.line_symbol);
	QCOMPARE(symbol_box->symbol(), static_cast<const Symbol*>(map.line_symbol));
	convert->click();
	QVERIFY(!message->text().isEmpty());
	QCOMPARE(list->count(), 3);
	// A stroke does.
	auto const in_map = map.map->getPart(0)->getNumObjects();
	list->setCurrentRow(0);
	convert->click();
	QVERIFY2(message->text().isEmpty(), qPrintable(message->text()));
	QCOMPARE(list->count(), 2);
	QCOMPARE(part->getNumObjects(), 3);
	QCOMPARE(map.map->getPart(0)->getNumObjects(), in_map + 1);
	
	// Delete: the selected row goes, as one undo step; with no rows the button is grey.
	auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("delete"));
	QVERIFY(remove);
	QVERIFY(remove->isEnabled());
	auto const steps = map.map->undoManager().undoStepCount();
	list->setCurrentRow(0);
	remove->click();
	QCOMPARE(list->count(), 1);
	QCOMPARE(part->getNumObjects(), 2);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	remove->click();
	QCOMPARE(list->count(), 0);
	QCOMPARE(part->getNumObjects(), 0);
	QVERIFY(!remove->isEnabled());
}

void ToolsTest::sketchToolbar()
{
	// the design notes, "Sketch layer", third change.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* toolbar = editor.window->findChild<QToolBar*>(QStringLiteral("Sketch toolbar"));
	QVERIFY(toolbar);
	QVERIFY(toolbar->isHidden());
	for (auto id : { "sketch-pen", "sketch-eraser", "sketch-note", "sketch-behind", "sketch-manage", "sketch-flatten" })
	{
		auto* action = editor.editor->getAction(id);
		QVERIFY2(action, id);
		QVERIFY2(toolbar->actions().contains(action), id);
		QVERIFY2(!action->icon().isNull(), id);
	}
	
	// The pen's colour and width, each a button with a menu.
	auto* color_button = toolbar->findChild<QToolButton*>(QStringLiteral("sketch-color"));
	auto* width_button = toolbar->findChild<QToolButton*>(QStringLiteral("sketch-width"));
	QVERIFY(color_button && color_button->menu());
	QVERIFY(width_button && width_button->menu());
	QCOMPARE(color_button->menu()->actions().size(), sketch_color_count);
	QCOMPARE(width_button->menu()->actions().size(), sketch_width_count);
	color_button->menu()->actions().at(0)->trigger();
	QCOMPARE(SketchPenTool::penColor(), 0);
	QCOMPARE(color_button->icon().pixmap(16, 16).toImage().pixelColor(8, 8).rgb(), sketchColorRgb(0).rgb());
	color_button->menu()->actions().at(5)->trigger();
	QCOMPARE(SketchPenTool::penColor(), 5);
	QVERIFY(color_button->menu()->actions().at(5)->isChecked());
	QCOMPARE(color_button->icon().pixmap(16, 16).toImage().pixelColor(8, 8).rgb(), sketchColorRgb(5).rgb());
	width_button->menu()->actions().at(0)->trigger();
	QCOMPARE(SketchPenTool::penWidth(), 0);
	auto const thin_icon = width_button->icon().pixmap(32, 32).toImage();
	width_button->menu()->actions().at(2)->trigger();
	QCOMPARE(SketchPenTool::penWidth(), 2);
	QVERIFY(width_button->icon().pixmap(32, 32).toImage() != thin_icon);
	
	// Draw behind map.
	auto* behind = editor.editor->getAction("sketch-behind");
	QVERIFY(behind->isCheckable());
	QVERIFY(!behind->isEnabled());
	QVERIFY(addSketchStroke(*map.map, { MapCoordF(0, 0), MapCoordF(10, 0) }, 0, 1));
	QVERIFY(behind->isEnabled());
	QVERIFY(!behind->isChecked());
	behind->trigger();
	QVERIFY(behind->isChecked());
	QVERIFY(sketchDrawnBehind(*map.map));
	QCOMPARE(map.map->getColor(map.map->getNumColors() - 1)->getName(), sketchColorName(0));
	behind->trigger();
	QVERIFY(!behind->isChecked());
	QCOMPARE(map.map->getColor(0)->getName(), sketchColorName(0));
	// Undoing the stroke takes the part away: the action is grey again.
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(!behind->isEnabled());
}

void ToolsTest::lasso()
{
	// the design notes, "Lasso selection".
	TestMap map;
	TestMapEditor editor(map.map);
	auto* point_symbol = new PointSymbol();
	map.map->addSymbol(point_symbol, 1);
	auto* area_symbol = new AreaSymbol();
	map.map->addSymbol(area_symbol, 2);
	auto* point = new PointObject(point_symbol);
	point->setPosition(MapCoord(5.0, 5.0));
	map.map->addObject(point);
	auto* line = new PathObject(map.line_symbol, { MapCoord(20.0, 0.0), MapCoord(30.0, 0.0) });
	map.map->addObject(line);
	auto* area = new PathObject(area_symbol);
	for (auto const& coord : { MapCoord(0.0, 20.0), MapCoord(10.0, 20.0), MapCoord(10.0, 30.0), MapCoord(0.0, 30.0) })
		area->addCoordinate(coord);
	area->parts().front().setClosed(true);
	map.map->addObject(area);
	auto* far_point = new PointObject(point_symbol);
	far_point->setPosition(MapCoord(50.0, 50.0));
	map.map->addObject(far_point);
	map.map->clearObjectSelection(true);
	
	auto* action = editor.editor->getAction("select-lasso");
	QVERIFY(action);
	action->trigger();
	auto* tool = dynamic_cast<LassoTool*>(editor.editor->getTool());
	QVERIFY(tool);
	auto const selection = [&map]() { return map.map->selectedObjects(); };
	
	auto const triangle = std::vector<MapCoordF>{ MapCoordF(0.0, 0.0), MapCoordF(10.0, 0.0), MapCoordF(5.0, 10.0) };
	QVERIFY(tool->selectPolygon(triangle, false));
	QVERIFY(selection() == (Map::ObjectSelection{ point }));
	QVERIFY(tool->selectPolygon({ MapCoordF(25.0, -2.0), MapCoordF(27.0, -2.0), MapCoordF(27.0, 2.0), MapCoordF(25.0, 2.0) }, false));
	QVERIFY(selection() == (Map::ObjectSelection{ line }));
	QVERIFY(tool->selectPolygon({ MapCoordF(3.0, 23.0), MapCoordF(6.0, 23.0), MapCoordF(6.0, 26.0), MapCoordF(3.0, 26.0) }, false));
	QVERIFY(selection() == (Map::ObjectSelection{ area }));
	QVERIFY(tool->selectPolygon({ MapCoordF(-5.0, -5.0), MapCoordF(35.0, -5.0), MapCoordF(35.0, 35.0), MapCoordF(-5.0, 35.0) }, false));
	QVERIFY(selection() == (Map::ObjectSelection{ map.line_object, point, line, area }));
	// Toggling with the triangle removes the point.
	QVERIFY(tool->selectPolygon(triangle, true));
	QVERIFY(selection() == (Map::ObjectSelection{ map.line_object, line, area }));
	// Fewer than three points select nothing and change nothing.
	QVERIFY(!tool->selectPolygon({ MapCoordF(0.0, 0.0), MapCoordF(10.0, 0.0) }, true));
	map.map->clearObjectSelection(true);
	
	// The same triangle by mouse: press, two moves, release.
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(10.0, 0.0));
	move(viewport(5.0, 10.0));
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(5.0, 10.0));
	QVERIFY(selection() == (Map::ObjectSelection{ point }));
	
	editor.editor->setTool(nullptr);
}


void ToolsTest::groups()
{
	// the design notes, "Groups", editor paragraphs.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* first = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) });
	map.map->addObject(first);
	auto* second = new PathObject(map.line_symbol, { MapCoord(0.0, 40.0), MapCoord(20.0, 40.0) });
	map.map->addObject(second);
	auto const first_uuid = first->uuid();
	auto const second_uuid = second->uuid();
	auto* group_act = editor.editor->getAction("group");
	auto* ungroup_act = editor.editor->getAction("ungroup");
	auto* select_group_act = editor.editor->getAction("select-group");
	QVERIFY(group_act && ungroup_act && select_group_act);
	map.map->clearObjectSelection(true);
	QVERIFY(!group_act->isEnabled() && !ungroup_act->isEnabled() && !select_group_act->isEnabled());
	map.map->addObjectToSelection(first, true);
	QVERIFY(!group_act->isEnabled() && ungroup_act->isEnabled() && select_group_act->isEnabled());
	map.map->addObjectToSelection(second, true);
	QVERIFY(group_act->isEnabled());
	
	auto const undo_steps = map.map->undoManager().undoStepCount();
	editor.editor->groupSelection(QStringLiteral("fence"));
	auto const group = map.map->findObject(first_uuid)->group();
	QVERIFY(group > 0);
	QCOMPARE(map.map->findObject(second_uuid)->group(), group);
	QCOMPARE(map.map->objectGroupName(group), QStringLiteral("fence"));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(first->group(), 0);
	QCOMPARE(second->group(), 0);
	QVERIFY(map.map->undoManager().redo());
	auto* grouped_first = map.map->findObject(first_uuid);
	auto* grouped_second = map.map->findObject(second_uuid);
	QCOMPARE(grouped_first->group(), group);
	
	// A click on a grouped object selects the group, Alt+click the object.
	editor.editor->setTool(new EditPointTool(editor.editor, nullptr));
	map.map->clearObjectSelection(true);
	auto const* widget = editor.map_widget;
	auto const on_first = widget->mapToViewport(MapCoord(10.0, 0.0)).toPoint();
	editor.simulateClick(on_first);
	QVERIFY(map.map->selectedObjects() == (Map::ObjectSelection{ grouped_first, grouped_second }));
	map.map->clearObjectSelection(true);
	QTest::mouseClick(editor.map_widget, Qt::LeftButton, Qt::AltModifier, on_first);
	QVERIFY(map.map->selectedObjects() == (Map::ObjectSelection{ grouped_first }));
	editor.editor->selectGroup();
	QVERIFY(map.map->selectedObjects() == (Map::ObjectSelection{ grouped_first, grouped_second }));
	
	editor.editor->ungroupSelection();
	QCOMPARE(map.map->findObject(first_uuid)->group(), 0);
	QCOMPARE(map.map->findObject(second_uuid)->group(), 0);
	editor.editor->setTool(nullptr);
}


void ToolsTest::numericEntry()
{
	// the design notes, "Numeric entry bar".
	TestMap map;
	TestMapEditor editor(map.map);
	auto* bar = editor.editor->numericEntryBar();
	QVERIFY(bar);
	auto* toggle = editor.editor->getAction("numeric-entry");
	QVERIFY(toggle && toggle->isCheckable());
	auto const near = [](const MapCoordF& a, const MapCoordF& b, double tolerance) { return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance; };
	
	// Without georeferencing: mm, y up; length and angle from a reference.
	QCOMPARE(map.map->getGeoreferencing().getState(), Georeferencing::Local);
	MapCoordF vertex;
	QString error;
	QVERIFY(NumericEntryBar::computeVertex(*map.map, NumericEntryBar::Position, 10.0, 20.0, false, nullptr, vertex, error));
	QVERIFY(near(vertex, MapCoordF(10.0, -20.0), 1e-9));
	auto const origin = MapCoordF(0.0, 0.0);
	QVERIFY(NumericEntryBar::computeVertex(*map.map, NumericEntryBar::LengthAndAngle, 10.0, 90.0, false, &origin, vertex, error));
	QVERIFY(near(vertex, MapCoordF(10.0, 0.0), 1e-9));
	QVERIFY(NumericEntryBar::computeVertex(*map.map, NumericEntryBar::LengthAndAngle, 10.0, 0.0, false, &origin, vertex, error));
	QVERIFY(near(vertex, MapCoordF(0.0, -10.0), 1e-9));
	QVERIFY(!NumericEntryBar::computeVertex(*map.map, NumericEntryBar::LengthAndAngle, 10.0, 0.0, false, nullptr, vertex, error));
	QVERIFY(!error.isEmpty());
	
	// With a UTM georeferencing: easting and northing, metres on the ground, grid angle.
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0));
	if (georef.setProjectedCRS(QStringLiteral("UTM"), QStringLiteral("+proj=utm +zone=14 +datum=WGS84 +units=m +no_defs")) && georef.getState() == Georeferencing::Geospatial)
	{
		Map geo_map;
		geo_map.setGeoreferencing(georef);
		QVERIFY(NumericEntryBar::computeVertex(geo_map, NumericEntryBar::Position, 500100.0, 5000000.0, false, nullptr, vertex, error));
		QVERIFY(near(vertex, MapCoordF(10.0, 0.0), 1e-6));
		QVERIFY(NumericEntryBar::computeVertex(geo_map, NumericEntryBar::Position, 500000.0, 5000100.0, false, nullptr, vertex, error));
		QVERIFY(near(vertex, MapCoordF(0.0, -10.0), 1e-6));
		QVERIFY(NumericEntryBar::computeVertex(geo_map, NumericEntryBar::LengthAndAngle, 100.0, 90.0, false, &origin, vertex, error));
		QVERIFY(near(vertex, MapCoordF(10.0, 0.0), 1e-6));
		QVERIFY(std::abs(NumericEntryBar::gridNorthAngle(geo_map.getGeoreferencing())) < 1e-6);
		
		georef.setGrivation(10.0);
		geo_map.setGeoreferencing(georef);
		auto const& rotated = geo_map.getGeoreferencing();
		QVERIFY(std::abs(std::abs(NumericEntryBar::gridNorthAngle(rotated)) - 10.0) < 1e-6);
		QVERIFY(NumericEntryBar::computeVertex(geo_map, NumericEntryBar::LengthAndAngle, 100.0, 0.0, true, &origin, vertex, error));
		auto const expected = rotated.toMapCoordF(QPointF(500000.0, 5000100.0));
		QVERIFY2(near(vertex, expected, 1e-6), qPrintable(QString::fromLatin1("%1 %2 vs %3 %4").arg(vertex.x()).arg(vertex.y()).arg(expected.x()).arg(expected.y())));
	}
	else
	{
		QWARN("UTM georeferencing not available, the projected checks were skipped.");
	}
	
	// In the editor: three vertices through the bar, then Ctrl+Return.
	editor.editor->setTool(new EditPointTool(editor.editor, nullptr));
	bar->setMode(NumericEntryBar::Position);
	bar->setValues(QStringLiteral("0"), QStringLiteral("0"));
	QVERIFY(!bar->accept());  // the edit tool takes no numeric vertices
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	QCOMPARE(editor.editor->activeSymbol(), static_cast<Symbol*>(map.line_symbol));
	auto* part = map.map->getCurrentPart();
	auto const num_objects = part->getNumObjects();
	editor.editor->setTool(new DrawPathTool(editor.editor, nullptr, false, true));
	auto* tool = editor.editor->getTool();
	QVERIFY(tool && tool->acceptsNumericVertices());
	QVERIFY(bar->accept());
	QVERIFY(tool->editingInProgress());
	bar->setValues(QStringLiteral("10"), QStringLiteral("0"));
	QVERIFY(bar->accept());
	bar->setMode(NumericEntryBar::LengthAndAngle);
	bar->setValues(QStringLiteral("10"), QStringLiteral("90"));
	QVERIFY(bar->accept());
	QTest::keyClick(editor.map_widget, Qt::Key_Return, Qt::ControlModifier);
	QVERIFY(!tool->editingInProgress());
	QCOMPARE(part->getNumObjects(), num_objects + 1);
	auto const* drawn = part->getObject(num_objects);
	QCOMPARE(drawn->getType(), Object::Path);
	auto const& drawn_coords = drawn->getRawCoordinateVector();
	QCOMPARE(drawn_coords.size(), MapCoordVector::size_type(3));
	QVERIFY(drawn_coords[0].isPositionEqualTo(MapCoord(0.0, 0.0)));
	QVERIFY(drawn_coords[1].isPositionEqualTo(MapCoord(10.0, 0.0)));
	QVERIFY(drawn_coords[2].isPositionEqualTo(MapCoord(20.0, 0.0)));
	QCOMPARE(drawn->getSymbol(), static_cast<const Symbol*>(map.line_symbol));
	
	editor.editor->setTool(nullptr);
}


void ToolsTest::viewHistory()
{
	// the design notes, "Bookmarks and view history".
	ViewHistory history;
	QVERIFY(!history.canGoBack() && !history.canGoForward());
	auto const entry = [](double x, double zoom) { return ViewHistory::Entry{ MapCoord(x, 0.0), zoom }; };
	QVERIFY(history.record(entry(1.0, 1.0)));
	QVERIFY(history.record(entry(2.0, 1.0)));
	QVERIFY(history.record(entry(3.0, 1.0)));
	QVERIFY(!history.record(entry(3.0, 1.0)));  // the current entry again
	QCOMPARE(history.size(), std::size_t(3));
	QVERIFY(history.back() && history.back());
	QVERIFY(!history.back());
	auto const* forward = history.forward();
	QVERIFY(forward && *forward == entry(2.0, 1.0));
	QVERIFY(history.record(entry(4.0, 2.0)));  // drops the entry at 3
	QCOMPARE(history.size(), std::size_t(3));
	QVERIFY(!history.canGoForward());
	for (int i = 0; i < 40; ++i)
		history.record(entry(100.0 + i, 1.0));
	QCOMPARE(history.size(), ViewHistory::capacity);
	QVERIFY(history.back() && *history.back() == entry(137.0, 1.0));
	
	TestMap map;
	TestMapEditor editor(map.map);
	auto* back_act = editor.editor->getAction("view-back");
	auto* forward_act = editor.editor->getAction("view-forward");
	QVERIFY(back_act && forward_act);
	auto* view = editor.map_widget->getMapView();
	view->setCenter(MapCoord(0.0, 0.0));
	view->setZoom(1.0);
	editor.editor->recordView();
	view->setCenter(MapCoord(10.0, 10.0));
	view->setZoom(4.0);
	editor.editor->recordView();
	QVERIFY(back_act->isEnabled() && !forward_act->isEnabled());
	editor.editor->viewBack();
	QCOMPARE(view->center(), MapCoord(0.0, 0.0));
	QCOMPARE(view->getZoom(), 1.0);
	QVERIFY(forward_act->isEnabled());
	editor.editor->viewForward();
	QCOMPARE(view->center(), MapCoord(10.0, 10.0));
	QCOMPARE(view->getZoom(), 4.0);
	QVERIFY(!forward_act->isEnabled());
}

void ToolsTest::bookmarks()
{
	// the design notes, "Bookmarks and view history".
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("bookmark-add") && editor.editor->getAction("bookmark-manage"));
	auto* view = editor.map_widget->getMapView();
	view->setCenter(MapCoord(5.0, -5.0));
	view->setZoom(2.0);
	map.map->setHasUnsavedChanges(false);
	editor.editor->addBookmark(QStringLiteral("start"));
	QCOMPARE(map.map->bookmarks().size(), std::size_t(1));
	QCOMPARE(map.map->bookmarks().front().name, QStringLiteral("start"));
	QCOMPARE(map.map->bookmarks().front().center, MapCoord(5.0, -5.0));
	QCOMPARE(map.map->bookmarks().front().zoom, 2.0);
	QVERIFY(map.map->hasUnsavedChanges());
	view->setCenter(MapCoord(50.0, 50.0));
	view->setZoom(8.0);
	editor.editor->jumpToBookmark(0);
	QCOMPARE(view->center(), MapCoord(5.0, -5.0));
	QCOMPARE(view->getZoom(), 2.0);
	editor.editor->jumpToBookmark(7);  // out of range: nothing happens
	QCOMPARE(view->center(), MapCoord(5.0, -5.0));
}


void ToolsTest::shortcuts()
{
	// the design notes, "Shortcut editor".
	TestMap map;
	TestMapEditor editor(map.map);
	auto const& actions = editor.editor->actions();
	auto* draw_path = editor.editor->getAction("drawpath");
	QVERIFY(draw_path);
	QCOMPARE(draw_path->objectName(), QStringLiteral("drawpath"));
	QCOMPARE(editor.editor->getAction("mirror")->objectName(), QStringLiteral("mirror"));
	QCOMPARE(editor.editor->defaultShortcut("drawpath"), QKeySequence(QStringLiteral("P")));
	QCOMPARE(draw_path->shortcut(), QKeySequence(QStringLiteral("P")));
	
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QSettings settings(dir.filePath(QStringLiteral("shortcuts.ini")), QSettings::IniFormat);
	auto const sequence = QKeySequence(QStringLiteral("Ctrl+Shift+P"));
	QKeySequence loaded;
	QVERIFY(!ShortcutSettings::load(settings, "drawpath", loaded));
	ShortcutSettings::store(settings, "drawpath", sequence);
	QVERIFY(ShortcutSettings::load(settings, "drawpath", loaded));
	QCOMPARE(loaded, sequence);
	QCOMPARE(ShortcutSettings::apply(settings, actions), 1);
	QCOMPARE(draw_path->shortcut(), sequence);
	
	// Conflicts: the sequence is in use, unless the user itself is excluded.
	QCOMPARE(ShortcutSettings::conflict(actions, sequence, nullptr), draw_path);
	QVERIFY(!ShortcutSettings::conflict(actions, sequence, draw_path));
	QVERIFY(!ShortcutSettings::conflict(actions, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F12")), nullptr));
	QVERIFY(!ShortcutSettings::conflict(actions, QKeySequence(), nullptr));
	
	// JSON round trip.
	auto const json = ShortcutSettings::toJson(actions);
	QVERIFY(json.contains("\"drawpath\": \"Ctrl+Shift+P\""));
	QSettings imported(dir.filePath(QStringLiteral("imported.ini")), QSettings::IniFormat);
	QVERIFY(ShortcutSettings::fromJson(imported, json) > 0);
	QVERIFY(ShortcutSettings::load(imported, "drawpath", loaded));
	QCOMPARE(loaded, sequence);
	QCOMPARE(ShortcutSettings::fromJson(imported, "not json"), -1);
	
	// Removing the stored shortcuts leaves the defaults to the editor.
	ShortcutSettings::removeAll(settings);
	QCOMPARE(ShortcutSettings::apply(settings, actions), 0);
	draw_path->setShortcut(editor.editor->defaultShortcut("drawpath"));
	QCOMPARE(draw_path->shortcut(), QKeySequence(QStringLiteral("P")));
}


void ToolsTest::findReplaceText()
{
	// the design notes, "Find and replace text", editor paragraphs.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("find-replace-text"));
	auto* text_symbol = new TextSymbol();
	map.map->addSymbol(text_symbol, 1);
	auto* other_symbol = new TextSymbol();
	map.map->addSymbol(other_symbol, 2);
	std::vector<TextObject*> texts;
	for (auto const* content : { "Hill 1", "hill top", "Hillside" })
	{
		auto* text = new TextObject(text_symbol);
		text->setText(QString::fromLatin1(content));
		text->setAnchorPosition(MapCoord(10.0 * texts.size(), 0.0));
		map.map->addObject(text);
		texts.push_back(text);
	}
	auto* other = new TextObject(other_symbol);
	other->setText(QStringLiteral("hill of the other symbol"));
	map.map->addObject(other);
	auto* part = map.map->getCurrentPart();
	auto const num_objects = part->getNumObjects();
	map.map->clearObjectSelection(true);
	
	ops::ReplaceTextParams params;
	params.find = QStringLiteral("hill");
	params.replacement = QStringLiteral("Knoll");
	auto const selected = [&map]() { return map.map->getFirstSelectedObject(); };
	QVERIFY(editor.editor->findNextText(params, false));
	QCOMPARE(selected(), static_cast<Object*>(texts[0]));
	QVERIFY(editor.editor->findNextText(params, false));
	QCOMPARE(selected(), static_cast<Object*>(texts[1]));
	QVERIFY(editor.editor->findNextText(params, false));
	QCOMPARE(selected(), static_cast<Object*>(texts[2]));
	QVERIFY(editor.editor->findNextText(params, false));
	QCOMPARE(selected(), static_cast<Object*>(other));
	QVERIFY(editor.editor->findNextText(params, false));
	QCOMPARE(selected(), static_cast<Object*>(texts[0]));
	ops::ReplaceTextParams missing;
	missing.find = QStringLiteral("nothing like it");
	QVERIFY(!editor.editor->findNextText(missing, false));
	
	// Replace in the selection, then in the rest of the scope.
	auto const undo_steps = map.map->undoManager().undoStepCount();
	auto const first_uuid = texts[0]->uuid();
	QCOMPARE(editor.editor->replaceText(params, false, true), 1);
	QCOMPARE(map.map->findObject(first_uuid)->asText()->getText(), QStringLiteral("Knoll 1"));
	QCOMPARE(editor.editor->replaceText(params, false, false), 3);
	QCOMPARE(other->getText(), QStringLiteral("hill of the other symbol"));  // the object was replaced
	QCOMPARE(map.map->findObject(other->uuid())->asText()->getText(), QStringLiteral("Knoll of the other symbol"));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 2);
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->findObject(first_uuid)->asText()->getText(), QStringLiteral("Hill 1"));
	
	// The scope of the selected symbols.
	editor.editor->getSymbolWidget()->selectSingleSymbol(other_symbol);
	QCOMPARE(editor.editor->replaceText(params, true, false), 1);
	QCOMPARE(map.map->findObject(first_uuid)->asText()->getText(), QStringLiteral("Hill 1"));
	QCOMPARE(map.map->findObject(other->uuid())->asText()->getText(), QStringLiteral("Knoll of the other symbol"));
	
	// Delete the selected matching object.
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(map.map->findObject(first_uuid), true);
	QCOMPARE(editor.editor->deleteMatchingText(params, false, true), 1);
	QCOMPARE(part->getNumObjects(), num_objects - 1);
	QVERIFY(!map.map->findObject(first_uuid));
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), num_objects);
	QVERIFY(map.map->findObject(first_uuid));
}


void ToolsTest::extents()
{
	// the design notes, "Named extents and the paper frame", editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("extent-define") && editor.editor->getAction("extent-delete"));
	QVERIFY(!editor.editor->getAction("extent-delete")->isEnabled());
	// Proposing an extent does not give the map a print area.
	QVERIFY(!map.map->hasPrinterConfig());
	auto const proposed = editor.editor->newExtent();
	QCOMPARE(proposed.name, QStringLiteral("Extent 1"));
	QCOMPARE(proposed.rect_mm, static_cast<const Map&>(*map.map).printerConfig().print_area);
	QVERIFY(!map.map->hasPrinterConfig());
	auto const undo_steps = map.map->undoManager().undoStepCount();
	Extent sheet;
	sheet.name = QStringLiteral("sheet");
	sheet.rect_mm = QRectF(0.0, 0.0, 100.0, 80.0);
	editor.editor->defineExtent(sheet);
	QCOMPARE(map.map->extents().size(), std::size_t(1));
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(editor.editor->getAction("extent-delete")->isEnabled());
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->extents().empty());
	QVERIFY(map.map->undoManager().redo());
	QCOMPARE(map.map->extents().size(), std::size_t(1));
	
	sheet.rect_mm = QRectF(10.0, 10.0, 50.0, 40.0);
	editor.editor->defineExtent(sheet);  // the same name replaces
	QCOMPARE(map.map->extents().size(), std::size_t(1));
	QCOMPARE(map.map->extents().front().rect_mm, QRectF(10.0, 10.0, 50.0, 40.0));
	
	editor.editor->setActiveExtent(QStringLiteral("sheet"));
	QCOMPARE(editor.editor->activeExtent(), QStringLiteral("sheet"));
	QVERIFY(!editor.editor->deleteExtent(QStringLiteral("no such extent")));
	QVERIFY(editor.editor->deleteExtent(QStringLiteral("sheet")));
	QVERIFY(map.map->extents().empty());
	QVERIFY(editor.editor->activeExtent().isEmpty());
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->extents().size(), std::size_t(1));
	// Extents which come by undo, by import or from the canvas dialog: the Extents menu sees them when it opens.
	QMenu* extents_menu = nullptr;
	for (auto* widget : editor.editor->getAction("extent-delete")->associatedWidgets())
	{
		if (auto* menu = qobject_cast<QMenu*>(widget))
			extents_menu = menu;
	}
	QVERIFY(extents_menu);
	emit extents_menu->aboutToShow();
	QVERIFY(editor.editor->getAction("extent-delete")->isEnabled());
	map.map->setExtents({});
	emit extents_menu->aboutToShow();
	QVERIFY(!editor.editor->getAction("extent-delete")->isEnabled());
	QCOMPARE(importExtents(*map.map, { sheet }), 1);
	emit extents_menu->aboutToShow();
	QVERIFY(editor.editor->getAction("extent-delete")->isEnabled());
	
	// The dialog's "From current view" is the map area the widget shows.
	ExtentDialog dialog(editor.editor, Extent{}, editor.window);
	dialog.fromCurrentView();
	auto const* widget = editor.map_widget;
	auto const shown = QRectF(QPointF(widget->viewportToMapF(widget->rect().topLeft())), QPointF(widget->viewportToMapF(widget->rect().bottomRight()))).normalized();
	auto const entered = dialog.extent().rect_mm;
	QVERIFY(std::abs(entered.left() - shown.left()) < 0.011 && std::abs(entered.top() - shown.top()) < 0.011);
	QVERIFY(std::abs(entered.width() - shown.width()) < 0.011 && std::abs(entered.height() - shown.height()) < 0.011);
	QVERIFY(shown.width() > 1.0 && shown.height() > 1.0);
	
	// "From print area" neither.
	dialog.fromPrintArea();
	QVERIFY(!map.map->hasPrinterConfig());
	auto const area = static_cast<const Map&>(*map.map).printerConfig().print_area;
	QVERIFY(std::abs(dialog.extent().rect_mm.width() - area.width()) < 0.011);
	QVERIFY(std::abs(dialog.extent().rect_mm.height() - area.height()) < 0.011);
}


void ToolsTest::demFeatures()
{
	// the design notes, "Knolls and small depressions", Accept (4).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-features"));
	// A cone of 2 m and a pit of 2 m on flat ground, 1 m cells.
	std::vector<float> values(41 * 21);
	for (int row = 0; row < 21; ++row)
	{
		for (int col = 0; col < 41; ++col)
		{
			auto const up = std::hypot(col - 10.0, row - 10.0);
			auto const down = std::hypot(col - 30.0, row - 10.0);
			values[std::size_t(row) * 41 + std::size_t(col)] = float(100.0 + (up < 6 ? 2.0 * (1 - up / 6) : 0.0) - (down < 6 ? 2.0 * (1 - down / 6) : 0.0));
		}
	}
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(41, 21, QPointF(0.0, 21.0), 1.0, std::move(values)), QStringLiteral("cone.asc"));
	auto const objects = map.map->getNumObjects();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	QCOMPARE(editor.editor->extractLandforms(LandformParams{}), 2);
	QCOMPARE(map.map->getNumObjects(), objects + 2);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects);
}


void ToolsTest::demCliffs()
{
	// the design notes, "Cliffs from the slope gradient", Accept (7).
	TestMap map;
	TestMapEditor editor(map.map);
	// A block of 8 m, 10 by 10 cells, on flat ground.
	std::vector<float> values(40 * 40, 100.0f);
	for (int row = 15; row < 25; ++row)
	{
		for (int col = 15; col < 25; ++col)
			values[std::size_t(row) * 40 + std::size_t(col)] = 108.0f;
	}
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(40, 40, QPointF(0.0, 40.0), 1.0, std::move(values)), QStringLiteral("block.asc"));
	auto const objects = map.map->getNumObjects();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	LandformParams params;
	params.cliffs = true;
	QCOMPARE(editor.editor->extractLandforms(params), 1);
	QCOMPARE(map.map->getNumObjects(), objects + 1);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects);
}


void ToolsTest::demTrees()
{
	// the design notes, "Trees", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	LandformParams params;
	params.trees = true;
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(50, 30, QPointF(0.0, 30.0), 1.0, std::vector<float>(50 * 30, 100.0f)), QStringLiteral("dtm.asc"));
	QCOMPARE(editor.editor->extractLandforms(params), 0);
	// Two trees of 12 m, 20 m apart.
	std::vector<float> values(50 * 30);
	for (int row = 0; row < 30; ++row)
	{
		for (int col = 0; col < 50; ++col)
		{
			auto const a = std::hypot(col - 15.0, row - 15.0);
			auto const b = std::hypot(col - 35.0, row - 15.0);
			values[std::size_t(row) * 50 + std::size_t(col)] = float(100.0 + 12.0 * std::max(std::exp(-a * a / 4.5), std::exp(-b * b / 4.5)));
		}
	}
	map.map->elevationSet().setModel(ElevationSet::Surface, std::make_unique<ElevationModel>(50, 30, QPointF(0.0, 30.0), 1.0, std::move(values)), QStringLiteral("dsm.asc"));
	auto const objects = map.map->getNumObjects();
	auto const undo_steps = map.map->undoManager().undoStepCount();
	QCOMPARE(editor.editor->extractLandforms(params), 2);
	QCOMPARE(map.map->getNumObjects(), objects + 2);
	QCOMPARE(map.map->undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects);
}


void ToolsTest::demGridCommands()
{
	// the design notes, "Grid commands", Accept.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "dem-merge", "dem-difference", "dem-resize", "dem-smooth", "dem-close" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto& set = map.map->elevationSet();
	auto const flat = [](double x0, double value, int width = 11) {
		return std::make_unique<ElevationModel>(width, 11, QPointF(x0, 11.0), 1.0, std::vector<float>(std::size_t(width) * 11, float(value)));
	};
	
	// Merge.
	auto const a_path = dir.filePath(QStringLiteral("a.asc"));
	auto const b_path = dir.filePath(QStringLiteral("b.asc"));
	QVERIFY(flat(0.0, 1.0)->save(a_path));
	QVERIFY(flat(5.0, 2.0)->save(b_path));
	set.setModel(ElevationSet::Terrain, ElevationModel::open(a_path), a_path);
	QString error;
	QVERIFY2(editor.editor->mergeElevationFiles({ b_path }, &error), qPrintable(error));
	auto const* merged = set.model(ElevationSet::Terrain);
	QVERIFY(merged);
	QCOMPARE(merged->extent(), QRectF(0.0, 0.0, 16.0, 11.0));
	QCOMPARE(merged->at(QPointF(2.5, 5.5)), 1.0);
	QCOMPARE(merged->at(QPointF(8.5, 5.5)), 2.0);
	auto const merged_href = set.href(ElevationSet::Terrain);
	QVERIFY(merged_href != a_path);
	QVERIFY(merged_href.endsWith(QLatin1String(".tif")));
	auto const reopened = ElevationModel::open(merged_href);
	QVERIFY(reopened);
	QCOMPARE(reopened->at(QPointF(8.5, 5.5)), 2.0);
	QCOMPARE(ElevationModel::open(a_path)->extent(), QRectF(0.0, 0.0, 11.0, 11.0));
	QVERIFY(map.map->hasUnsavedChanges());
	
	// Failures leave the set as it was.
	auto const coarse_path = dir.filePath(QStringLiteral("coarse.asc"));
	QVERIFY(std::make_unique<ElevationModel>(5, 5, QPointF(0.0, 10.0), 2.0, std::vector<float>(25, 3.0f))->save(coarse_path));
	QVERIFY(!editor.editor->mergeElevationFiles({ coarse_path }, &error));
	QVERIFY(!editor.editor->elevationDifference(&error));
	QCOMPARE(set.href(ElevationSet::Terrain), merged_href);
	QVERIFY(set.href(ElevationSet::Canopy).isEmpty());
	
	// Difference.
	set.setModel(ElevationSet::Terrain, flat(0.0, 1.0), a_path);
	set.setModel(ElevationSet::Surface, flat(0.0, 5.0), dir.filePath(QStringLiteral("s.asc")));
	QVERIFY2(editor.editor->elevationDifference(&error), qPrintable(error));
	auto const canopy = ElevationModel::open(set.href(ElevationSet::Canopy));
	QVERIFY(canopy);
	QCOMPARE(canopy->info().minimum, 4.0);
	QCOMPARE(canopy->info().maximum, 4.0);
	
	// Crop.
	QVERIFY2(editor.editor->cropElevation(QRectF(2.0, 2.0, 3.0, 3.0), &error), qPrintable(error));
	auto const* cropped = set.model(ElevationSet::Terrain);
	QCOMPARE(cropped->width(), 3);
	QCOMPARE(cropped->height(), 3);
	QCOMPARE(cropped->extent(), QRectF(2.0, 2.0, 3.0, 3.0));
	
	// Smooth.
	auto spike = flat(0.0, 10.0);
	spike->setValue(5, 5, 26.0f);
	set.setModel(ElevationSet::Terrain, std::move(spike), a_path);
	QVERIFY2(editor.editor->smoothElevation(1, &error), qPrintable(error));
	auto const* smooth = set.model(ElevationSet::Terrain);
	QCOMPARE(double(smooth->value(5, 5)), 14.0);
	QCOMPARE(double(smooth->value(4, 5)), 12.0);
	
	// Close.
	editor.editor->closeElevation();
	QVERIFY(set.isEmpty());
	QVERIFY(map.map->hasUnsavedChanges());
}


void ToolsTest::demShowFrame()
{
	// the design notes, "The grid's frame", Accept (5).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("dem-show-frame");
	QVERIFY(action);
	QVERIFY(action->isCheckable());
	QVERIFY(!action->isChecked());
	QVERIFY(editor.editor->shownElevationFrames().empty());

	auto georef = map.map->getGeoreferencing();
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 14"), QStringLiteral("+proj=utm +zone=14 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5200000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	map.map->setGeoreferencing(georef);

	std::vector<float> values(20 * 10, 400.0f);
	auto grid = std::make_unique<ElevationModel>(20, 10, QPointF(500000.0, 5200050.0), 5.0, std::move(values), QStringLiteral("EPSG:32614"));
	auto const path = dir.filePath(QStringLiteral("utm14.tif"));
	QVERIFY(grid->save(path));
	map.map->elevationSet().setHref(ElevationSet::Terrain, path);
	map.map->setHasUnsavedChanges(false);
	auto const objects = map.map->getNumObjects();

	action->trigger();
	QVERIFY(action->isChecked());
	auto const frames = editor.editor->shownElevationFrames();
	QCOMPARE(int(frames.size()), 1);
	QCOMPARE(frames[0].kind, ElevationSet::Terrain);
	QCOMPARE(frames[0].ring.size(), 64);
	QVERIFY(qAbs(frames[0].ring[0].x()) < 0.01 && qAbs(frames[0].ring[0].y() + 5.0) < 0.01);
	QVERIFY(qAbs(frames[0].ring[32].x() - 10.0) < 0.01 && qAbs(frames[0].ring[32].y()) < 0.01);
	editor.map_widget->repaint();  // The overlay draws without trouble.
	QCOMPARE(map.map->getNumObjects(), objects);
	QVERIFY(!map.map->hasUnsavedChanges());

	editor.editor->closeElevation();
	QVERIFY(action->isChecked());
	QVERIFY(editor.editor->shownElevationFrames().empty());

	map.map->elevationSet().setHref(ElevationSet::Terrain, path);
	QCOMPARE(int(editor.editor->shownElevationFrames().size()), 1);
	action->trigger();
	QVERIFY(!action->isChecked());
	QVERIFY(editor.editor->shownElevationFrames().empty());
}


void ToolsTest::demHillShadeDialog()
{
	// the design notes, "Bicubic resampling for hill shading", Accept (6).
	HillShadeDialog dialog(2.0, nullptr);
	QCOMPARE(dialog.params().resampling, HillShadeParams::Bicubic);
	QCheckBox* bicubic = nullptr;
	for (auto* check : dialog.findChildren<QCheckBox*>())
		if (check->text() == QLatin1String("Bicubic resampling"))
			bicubic = check;
	QVERIFY(bicubic);
	QVERIFY(bicubic->isChecked());
	bicubic->setChecked(false);
	QCOMPARE(dialog.params().resampling, HillShadeParams::Bilinear);

	// the design notes, "The gradient on the corners of 2 by 2 cells", Accept (5).
	auto const corners_check = [](QDialog& d) -> QCheckBox* {
		for (auto* check : d.findChildren<QCheckBox*>())
			if (check->text() == QLatin1String("Gradient on cell corners (legacy mode)"))
				return check;
		return nullptr;
	};
	QCOMPARE(dialog.params().gradient, GradientMethod::Corners);
	auto* corners = corners_check(dialog);
	QVERIFY(corners);
	QVERIFY(corners->isChecked());
	corners->setChecked(false);
	QCOMPARE(dialog.params().gradient, GradientMethod::Horn);
	SlopeDialog slope_dialog(2.0, nullptr);
	QCOMPARE(slope_dialog.params().gradient, GradientMethod::Corners);
	corners = corners_check(slope_dialog);
	QVERIFY(corners);
	corners->setChecked(false);
	QCOMPARE(slope_dialog.params().gradient, GradientMethod::Horn);
}


void ToolsTest::demHypsometric()
{
	// the design notes, "Hypsometric map", Accept (4).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-hypsometric"));
	std::vector<float> values(20 * 10);
	for (std::size_t k = 0; k < values.size(); ++k)
		values[k] = float(100 + k % 20);
	auto const grid_path = dir.filePath(QStringLiteral("hills.asc"));
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(20, 10, QPointF(0.0, 10.0), 1.0, std::move(values)), grid_path);
	auto const templates = map.map->getNumTemplates();
	QString error;
	auto const path = editor.editor->writeHypsometricMap(HypsometricParams{}, true, &error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	QCOMPARE(path, dir.filePath(QStringLiteral("hills_hypsometric.tif")));
	QVERIFY(QFileInfo(path).size() > 0);
	QCOMPARE(map.map->getNumTemplates(), templates + 1);
}


void ToolsTest::demVegetationHeight()
{
	// the design notes, "Vegetation height from the map's grids", Accept.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-vegetation-height"));
	auto const flat = [](double value) {
		return std::make_unique<ElevationModel>(10, 10, QPointF(0.0, 10.0), 1.0, std::vector<float>(100, float(value)));
	};
	auto& set = map.map->elevationSet();
	set.setModel(ElevationSet::Terrain, flat(1.0), dir.filePath(QStringLiteral("dtm.asc")));
	auto const templates = map.map->getNumTemplates();
	QString error;
	QVERIFY(editor.editor->writeVegetationHeightMap(true, &error).isEmpty());
	QVERIFY(!error.isEmpty());
	QCOMPARE(map.map->getNumTemplates(), templates);
	
	set.setModel(ElevationSet::Surface, flat(5.0), dir.filePath(QStringLiteral("dsm.asc")));
	error.clear();
	auto const path = editor.editor->writeVegetationHeightMap(true, &error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	QCOMPARE(path, dir.filePath(QStringLiteral("dtm_vegetation_height.tif")));
	QCOMPARE(map.map->getNumTemplates(), templates + 1);
	// The first band is red: that of 4 m.
	auto const four = vegetationHeightRaster(*flat(4.0), defaultHeightClasses()).image.pixel(5, 5);
	auto const red = ElevationModel::open(path);
	QVERIFY(red);
	QCOMPARE(int(red->value(5, 5)), qRed(four));
	
	set.setModel(ElevationSet::Canopy, flat(20.0), dir.filePath(QStringLiteral("chm.asc")));
	QVERIFY(!editor.editor->writeVegetationHeightMap(false, &error).isEmpty());
	auto const canopy = ElevationModel::open(path);
	QVERIFY(canopy);
	QCOMPARE(int(canopy->value(5, 5)), qRed(vegetationHeightRaster(*flat(20.0), defaultHeightClasses()).image.pixel(5, 5)));
	QVERIFY(qRed(four) != qRed(vegetationHeightRaster(*flat(20.0), defaultHeightClasses()).image.pixel(5, 5)));
	
	// "Vegetation height in grey and its classes in files", Accept (3).
	set.clear();
	set.setModel(ElevationSet::Terrain, flat(1.0), dir.filePath(QStringLiteral("dtm.asc")));
	set.setModel(ElevationSet::Surface, flat(16.0), dir.filePath(QStringLiteral("dsm.asc")));
	VegetationHeightOptions options;
	options.greyscale = true;
	QVERIFY(!editor.editor->writeVegetationHeightMap(options, false, &error).isEmpty());
	auto grey = ElevationModel::open(path);
	QVERIFY(grey);
	QVERIFY2(std::abs(grey->value(5, 5) - 128.0) <= 1.0, qPrintable(QString::number(grey->value(5, 5))));
	auto const class_file = dir.filePath(QStringLiteral("classes.txt"));
	{
		QFile file(class_file);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("from;to;from_colour;to_colour\n0;30;#000000;#ffffff\n");
	}
	options = {};
	QVERIFY2(loadHeightClasses(class_file, options.classes, &error), qPrintable(error));
	QVERIFY(!editor.editor->writeVegetationHeightMap(options, false, &error).isEmpty());
	auto ramp = ElevationModel::open(path);
	QVERIFY(ramp);
	QVERIFY2(std::abs(ramp->value(5, 5) - 128.0) <= 1.0, qPrintable(QString::number(ramp->value(5, 5))));
}


void ToolsTest::demProfile()
{
	// the design notes, "Profile", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-profile"));
	auto const& georef = map.map->getGeoreferencing();
	auto const a = georef.toProjectedCoords(MapCoordF(10.0, 50.0));
	auto const b = georef.toProjectedCoords(MapCoordF(30.0, 50.0));
	auto const left = std::floor(std::min(a.x(), b.x())) - 50.0;
	auto const top = std::ceil(std::max(a.y(), b.y())) + 50.0;
	auto const width = int(std::abs(b.x() - a.x())) + 100;
	auto const height = int(std::abs(b.y() - a.y())) + 100;
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(width, height, QPointF(left, top), 1.0,
	                                 std::vector<float>(std::size_t(width) * std::size_t(height), 100.0f)), QStringLiteral("flat.asc"));
	QString error;
	map.map->clearObjectSelection(false);
	QVERIFY(editor.editor->elevationProfileOfSelection(&error).samples.empty());
	QVERIFY(!error.isEmpty());
	auto* line = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(10.0, 50.0), MapCoord(30.0, 50.0) });
	map.map->addObject(line);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(line, true);
	error.clear();
	auto const profile = editor.editor->elevationProfileOfSelection(&error);
	QVERIFY2(!profile.samples.empty(), qPrintable(error));
	QVERIFY2(std::abs(profile.length - std::hypot(b.x() - a.x(), b.y() - a.y())) < 0.01, qPrintable(QString::number(profile.length)));
	QCOMPARE(profile.minimum, 100.0);
	QCOMPARE(profile.maximum, 100.0);
}


void ToolsTest::demMergeContours()
{
	// the design notes, "Merging contour lines", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-merge-contours"));
	auto* contour = new LineSymbol();
	contour->setLineWidth(0.14);
	map.map->addSymbol(contour, map.map->getNumSymbols());
	auto const line = [&map](const Symbol* s, const MapCoord& a, const MapCoord& b) {
		auto* object = new PathObject(s, { a, b });
		object->setTag(QStringLiteral("ele"), QStringLiteral("410"));
		map.map->addObject(object);
		return object;
	};
	line(contour, MapCoord(50.0, 50.0), MapCoord(60.0, 50.0));
	line(contour, MapCoord(60.1, 50.0), MapCoord(70.0, 50.0));
	line(map.line_symbol, MapCoord(70.0, 50.0), MapCoord(80.0, 50.0));
	auto* widget = editor.editor->getSymbolWidget();
	QVERIFY(widget);
	widget->selectSingleSymbol(contour);
	auto const objects = map.map->getNumObjects();
	auto const steps = map.map->undoManager().undoStepCount();
	QCOMPARE(editor.editor->mergeContourLines(0.25), 1);
	QCOMPARE(map.map->getNumObjects(), objects - 1);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects);
}


void ToolsTest::demContourLevels()
{
	// the design notes, "Levels in the range", Accept (4).
	ContourDialog dialog(500.05, 540.359, nullptr);
	QCOMPARE(dialog.levelCount(false), 40);
	QCOMPARE(dialog.levelCount(true), 16);
	auto* label = dialog.findChild<QLabel*>(QStringLiteral("unsmoothed_levels"));
	QVERIFY(label);
	QVERIFY2(label->text().contains(QLatin1String("40")), qPrintable(label->text()));
	auto* interval = dialog.findChild<QComboBox*>(QStringLiteral("unsmoothed_interval"));
	QVERIFY(interval);
	interval->setCurrentIndex(interval->findData(5.0));
	QCOMPARE(dialog.levelCount(false), 8);
	QVERIFY2(label->text().contains(QLatin1String("8")), qPrintable(label->text()));
	auto* smoothed_label = dialog.findChild<QLabel*>(QStringLiteral("smoothed_levels"));
	QVERIFY(smoothed_label);
	QVERIFY2(smoothed_label->text().contains(QLatin1String("16")), qPrintable(smoothed_label->text()));
}


void ToolsTest::demEmbedGrid()
{
	// the design notes, "The embedded grid for course setting", Accept (5) to (7).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "dem-embed", "dem-remove-embedded" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto& set = map.map->elevationSet();
	QString error;
	QVERIFY(!editor.editor->embedCourseElevation(0.0, &error));
	QVERIFY(!error.isEmpty());
	QVERIFY(!set.embeddedGrid());

	// A plane of 1 m cells rising 0.1 m per metre to the east, embedded at 2 m.
	std::vector<float> values(40 * 30);
	for (int r = 0; r < 30; ++r)
		for (int c = 0; c < 40; ++c)
			values[std::size_t(r) * 40 + std::size_t(c)] = float(200.0 + 0.1 * (c + 0.5));
	auto const path = dir.filePath(QStringLiteral("plane.tif"));
	QVERIFY(std::make_unique<ElevationModel>(40, 30, QPointF(0.0, 30.0), 1.0, values)->save(path));
	set.setModel(ElevationSet::Terrain, ElevationModel::open(path), path);
	map.map->setHasUnsavedChanges(false);
	QVERIFY2(editor.editor->embedCourseElevation(2.0, &error), qPrintable(error));
	auto const* embedded = set.embeddedGrid();
	QVERIFY(embedded);
	QCOMPARE(embedded->cellSize(), 2.0);
	QCOMPARE(embedded->width(), 20);
	QCOMPARE(embedded->height(), 15);
	QCOMPARE(embedded->extent(), QRectF(0.0, 0.0, 40.0, 30.0));
	QVERIFY(std::abs(embedded->at(QPointF(21.0, 15.0)) - 202.1) <= 0.06);
	QVERIFY(map.map->hasUnsavedChanges());

	// A smaller cell than the grid's keeps the grid's.
	QVERIFY(editor.editor->embedCourseElevation(0.5, &error));
	QCOMPARE(set.embeddedGrid()->cellSize(), 1.0);

	// Closing the grid files keeps it, and the route analysis takes its heights.
	editor.editor->closeElevation();
	QVERIFY(set.isEmpty());
	QVERIFY(set.embeddedGrid());
	auto const heights = routeHeightsOf(*map.map);
	QVERIFY(bool(heights));
	auto const place = map.map->getGeoreferencing().toMapCoordF(QPointF(21.0, 15.0));
	QVERIFY(std::abs(heights(place) - 202.1) <= 0.06);
	QCOMPARE(routeGridCellOf(*map.map), 1.0);

	// Removing it.
	map.map->setHasUnsavedChanges(false);
	QVERIFY(editor.editor->removeEmbeddedElevation());
	QVERIFY(!set.embeddedGrid());
	QVERIFY(map.map->hasUnsavedChanges());
	QVERIFY(!editor.editor->removeEmbeddedElevation());
	QVERIFY(!bool(routeHeightsOf(*map.map)));
}


void ToolsTest::selectionCommands()
{
	// the design notes, "Selection queries", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "select-duplicates", "select-self-intersecting", "select-invalid", "select-by-date", "select-by-property", "select-by-index", "select-next" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto* second = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(50.0, 50.0), MapCoord(60.0, 50.0) });
	map.map->addObject(second);
	auto* third = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(50.0, 50.0), MapCoord(60.0, 50.0) });
	map.map->addObject(third);
	auto const* part = map.map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 3);
	
	QCOMPARE(editor.editor->selectByIndex(2), 1);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->isObjectSelected(part->getObject(1)));
	editor.editor->selectNext();
	QVERIFY(map.map->isObjectSelected(part->getObject(2)));
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	editor.editor->selectNext();
	QVERIFY(map.map->isObjectSelected(part->getObject(0)));
	
	QCOMPARE(editor.editor->selectDuplicates(), 1);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->isObjectSelected(third));
}


void ToolsTest::savedSelections()
{
	// the design notes, "Saved selections", Accept (1) to (3) and (5).
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "selection-save", "selection-load", "selection-delete" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto* second = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(50.0, 50.0), MapCoord(60.0, 50.0) });
	map.map->addObject(second);
	auto* third = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(70.0, 50.0), MapCoord(80.0, 50.0) });
	map.map->addObject(third);
	map.map->setHasUnsavedChanges(false);
	
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(second, false);
	map.map->addObjectToSelection(third, true);
	editor.editor->saveSelection(QStringLiteral("north"));
	QCOMPARE(int(map.map->savedSelections().size()), 1);
	QCOMPARE(int(map.map->savedSelections().front().uuids.size()), 2);
	QVERIFY(map.map->hasUnsavedChanges());
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(third, true);
	editor.editor->saveSelection(QStringLiteral("north"));
	QCOMPARE(int(map.map->savedSelections().size()), 1);
	QCOMPARE(map.map->savedSelections().front().uuids, std::vector<QUuid>{ third->uuid() });
	
	map.map->clearObjectSelection(true);
	QCOMPARE(editor.editor->loadSavedSelection(QStringLiteral("north")), 1);
	QVERIFY(map.map->isObjectSelected(third));
	QCOMPARE(editor.editor->loadSavedSelection(QStringLiteral("nowhere")), 0);
	map.map->clearObjectSelection(true);
	map.map->deleteObject(third);
	QCOMPARE(editor.editor->loadSavedSelection(QStringLiteral("north")), 0);
	
	editor.editor->deleteSavedSelection(QStringLiteral("north"));
	QVERIFY(map.map->savedSelections().empty());
}


void ToolsTest::interpolateCommand()
{
	// the design notes, "Interpolate", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("interpolate"));
	auto* a = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(50.0, 50.0), MapCoord(90.0, 50.0) });
	map.map->addObject(a);
	auto* b = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(50.0, 60.0), MapCoord(90.0, 60.0) });
	map.map->addObject(b);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(a, false);
	map.map->addObjectToSelection(b, true);
	auto const objects = map.map->getNumObjects();
	auto const steps = map.map->undoManager().undoStepCount();
	editor.editor->interpolate(1, false);
	QCOMPARE(map.map->getNumObjects(), objects + 1);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
}


void ToolsTest::objectInformationCommand()
{
	// the design notes, "Object information", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("object-info");
	QVERIFY(action);
	map.map->clearObjectSelection(true);
	QVERIFY(!action->isEnabled());
	map.map->addObjectToSelection(map.line_object, true);
	QVERIFY(action->isEnabled());
}


void ToolsTest::symbolTools()
{
	// the design notes, "Selecting symbols, replacing fonts and colours, renumbering", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "symbols-replace-font", "symbols-replace-color", "symbols-renumber" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto* second = new LineSymbol();
	map.map->addSymbol(second, map.map->getNumSymbols());
	auto* third = new LineSymbol();
	map.map->addSymbol(third, map.map->getNumSymbols());
	auto* widget = editor.editor->getSymbolWidget();
	QVERIFY(widget);
	widget->selectSymbols({ map.line_symbol, third });
	QCOMPARE(widget->selectedSymbolsCount(), 2);
	QVERIFY(widget->isSymbolSelected(map.line_symbol));
	QVERIFY(widget->isSymbolSelected(third));
	QVERIFY(!widget->isSymbolSelected(second));
}


void ToolsTest::statusSetCommands()
{
	// the design notes, "Status sets", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "symbols-status-save", "symbols-status-apply", "symbols-status-delete" })
		QVERIFY2(editor.editor->getAction(id), id);
	map.line_symbol->setNumberComponent(0, 101);
	map.line_symbol->setHidden(true);
	editor.editor->saveStatusSet(QStringLiteral("field"));
	QCOMPARE(int(map.map->statusSets().size()), 1);
	editor.editor->saveStatusSet(QStringLiteral("field"));
	QCOMPARE(int(map.map->statusSets().size()), 1);
	map.line_symbol->setHidden(false);
	QCOMPARE(editor.editor->applyStatusSet(QStringLiteral("field")), 1);
	QVERIFY(map.line_symbol->isHidden());
	editor.editor->deleteStatusSet(QStringLiteral("field"));
	QVERIFY(map.map->statusSets().empty());
}


void ToolsTest::symbolCompareCommand()
{
	// the design notes, "Compare with file", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("symbols-compare"));
}


void ToolsTest::convertToPointCommand()
{
	// the design notes, "Convert objects", Accept (4).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("convert-to-point"));
	auto* point_symbol = new PointSymbol();
	map.map->addSymbol(point_symbol, map.map->getNumSymbols());
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(map.line_object, true);
	// Selecting an object selects its symbol in the pane; the point symbol is chosen after.
	editor.editor->getSymbolWidget()->selectSingleSymbol(point_symbol);
	auto const objects = map.map->getNumObjects();
	auto const steps = map.map->undoManager().undoStepCount();
	editor.editor->convertToPoints(0.0, 0.0);
	QCOMPARE(map.map->getNumObjects(), objects);
	QCOMPARE(map.map->getCurrentPart()->getObject(map.map->getCurrentPart()->getNumObjects() - 1)->getType(), Object::Point);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
}


void ToolsTest::symbolDescriptionCommands()
{
	// the design notes, "Descriptions file", Accept (4).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("symbols-descriptions-export"));
	QVERIFY(editor.editor->getAction("symbols-descriptions-import"));
}


void ToolsTest::favouritesFilter()
{
	// the design notes, "Favourites", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	map.line_symbol->setNumberComponent(0, 101);
	auto* second = new LineSymbol();
	second->setNumberComponent(0, 102);
	map.map->addSymbol(second, map.map->getNumSymbols());
	auto* third = new LineSymbol();
	third->setNumberComponent(0, 103);
	map.map->addSymbol(third, map.map->getNumSymbols());
	addFavourites(*map.map, { map.line_symbol, third });
	auto* widget = editor.editor->getSymbolWidget();
	widget->setFavouritesOnly(true);
	QVERIFY(widget->favouritesOnly());
	QVERIFY(widget->shownSymbols() == (std::vector<const Symbol*>{ map.line_symbol, third }));
	widget->setFavouritesOnly(false);
	QCOMPARE(int(widget->shownSymbols().size()), map.map->getNumPublicSymbols());
}


void ToolsTest::symbolGroupsDock()
{
	// the design notes, "Symbol tree", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("symbols-groups"));
	map.line_symbol->setNumberComponent(0, 101);
	auto* index = new LineSymbol();
	index->setNumberComponent(0, 102);
	map.map->addSymbol(index, map.map->getNumSymbols());
	auto* other = new LineSymbol();
	other->setNumberComponent(0, 501);
	map.map->addSymbol(other, map.map->getNumSymbols());
	auto* widget = editor.editor->getSymbolWidget();
	widget->setGroupFilter(true, QStringList{ QStringLiteral("101") });
	QVERIFY(widget->shownSymbols() == std::vector<const Symbol*>{ map.line_symbol });
	widget->setGroupFilter(false, {});
	QCOMPARE(int(widget->shownSymbols().size()), map.map->getNumPublicSymbols());
	
	addSymbolGroup(*map.map, QStringLiteral("Contours"));
	addSymbolGroup(*map.map, QStringLiteral("Index"), 0);
	addToSymbolGroup(*map.map, 0, { map.line_symbol });
	addToSymbolGroup(*map.map, 1, { index });
	SymbolGroupsWidget groups(*map.map, widget);
	QCOMPARE(groups.tree()->topLevelItemCount(), 1);
	QCOMPARE(groups.tree()->topLevelItem(0)->childCount(), 1);
	QCOMPARE(groups.tree()->topLevelItem(0)->child(0)->childCount(), 1);
	groups.selectGroup(0);
	QVERIFY(widget->shownSymbols() == (std::vector<const Symbol*>{ map.line_symbol, index }));
	groups.selectGroup(-1);
	QCOMPARE(int(widget->shownSymbols().size()), map.map->getNumPublicSymbols());
}


void ToolsTest::symbolUpdateCommand()
{
	// the design notes, "Update from file", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("symbols-update"));
}


void ToolsTest::pasteStampsObjects()
{
	// the design notes, "Object identity and timestamps", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	map.line_object->setIdentity(map.line_object->uuid(), 0, 0);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(map.line_object, true);
	editor.editor->copy();
	auto const before = Object::currentTime();
	editor.editor->paste(0);
	auto const* pasted = map.map->getFirstSelectedObject();
	QVERIFY(pasted);
	QVERIFY(pasted != map.line_object);
	QCOMPARE(pasted->createdTime(), qint64(0));
	QVERIFY(pasted->modifiedTime() >= before);
	QCOMPARE(map.line_object->modifiedTime(), qint64(0));
}


void ToolsTest::demPointCloud()
{
	// the design notes, "Reading point clouds and gridding them", Accept (9).
	if (!PointCloud::supported())
		QSKIP("Built without LASzip");
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-point-cloud"));
	// Ground on a plane rising 0.1 m per metre eastwards, four points per square metre.
	std::vector<CloudPoint> points;
	for (int i = 0; i < 40; ++i)
	{
		for (int j = 0; j < 40; ++j)
		{
			CloudPoint p;
			p.x = 1000.25 + 0.5 * i;
			p.y = 2000.25 + 0.5 * j;
			p.z = 300.0 + 0.1 * (p.x - 1000.0);
			p.classification = 2;
			points.push_back(p);
		}
	}
	QTemporaryDir dir;
	auto const cloud = dir.filePath(QStringLiteral("plane.laz"));
	QString error;
	QVERIFY2(PointCloud::write(cloud, points, {}, 0, &error), qPrintable(error));
	auto const templates = map.map->getNumTemplates();
	PointCloudProducts products;
	products.vegetation_height = true;
	products.vegetation_map = true;
	// "Feature map, intensity and classification maps", Accept (4).
	products.feature_map = true;
	products.intensity_map = true;
	products.classification_map = true;
	QVERIFY2(editor.editor->importPointClouds({ cloud }, GriddingParams(), products, &error), qPrintable(error));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_vegetation_height.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_vegetation.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_features.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_intensity.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_classes.tif"))));
	QCOMPARE(map.map->getNumTemplates(), templates + 5);
	auto& set = map.map->elevationSet();
	QCOMPARE(QFileInfo(set.href(ElevationSet::Terrain)).fileName(), QStringLiteral("plane_DTM.tif"));
	QCOMPARE(QFileInfo(set.href(ElevationSet::Surface)).fileName(), QStringLiteral("plane_DSM.tif"));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_DTM.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_DSM.tif"))));
	QVERIFY(map.map->hasUnsavedChanges());
	auto const* terrain = set.model(ElevationSet::Terrain);
	QVERIFY(terrain);
	QVERIFY(set.model(ElevationSet::Surface));
	QCOMPARE(terrain->width(), 20);
	auto const c = terrain->cellCenter(5, 7);
	QVERIFY(std::abs(terrain->value(5, 7) - (300.0 + 0.1 * (c.x() - 1000.0))) < 0.05);
	
	QVERIFY(!editor.editor->importPointClouds({ dir.filePath(QStringLiteral("absent.laz")) }, GriddingParams(), PointCloudProducts(), &error));
	QVERIFY(!error.isEmpty());
}


void ToolsTest::demVegetationStatistics()
{
	// the design notes, "Vegetation statistics", Accept (4).
	if (!PointCloud::supported())
		QSKIP("Built without LASzip");
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("dem-vegetation-statistics"));
	auto const& georef = map.map->getGeoreferencing();
	auto const origin = georef.toProjectedCoords(MapCoordF(0.0, 0.0));
	auto const x0 = std::floor(origin.x());
	auto const y0 = std::floor(origin.y());
	// An open block and a fight block of 10 by 10 m, 20 returns per square metre.
	std::vector<CloudPoint> points;
	for (int b = 0; b < 2; ++b)
	{
		for (int col = 0; col < 10; ++col)
		{
			for (int row = 0; row < 10; ++row)
			{
				for (int k = 0; k < 20; ++k)
				{
					CloudPoint p;
					p.x = x0 + 10 * b + col + 0.1 + 0.2 * (k % 5);
					p.y = y0 + row + 0.1 + 0.2 * (k / 5);
					auto const ground = b == 0 || k < 8;
					p.z = ground ? 100.0 : 102.0;
					p.classification = ground ? 2 : 5;
					points.push_back(p);
				}
			}
		}
	}
	QTemporaryDir dir;
	auto const cloud = dir.filePath(QStringLiteral("blocks.laz"));
	QString error;
	QVERIFY2(PointCloud::write(cloud, points, {}, 0, &error), qPrintable(error));
	auto grid = PointCloud::grid({ cloud }, GriddingParams());
	QVERIFY2(grid.dtm, qPrintable(grid.error));
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::move(grid.dtm), dir.filePath(QStringLiteral("blocks_DTM.tif")));

	map.map->clearObjectSelection(false);
	QVERIFY(editor.editor->vegetationStatisticsOfSelection({ cloud }, VegetationParams(), &error).classes.empty());
	QVERIFY(!error.isEmpty());

	auto const sample = [&](const QString& name, double left) {
		auto* symbol = new AreaSymbol();
		symbol->setName(name);
		map.map->addSymbol(symbol, map.map->getNumSymbols());
		MapCoordVector coords;
		for (auto const& corner : { QPointF(left, 3.0), QPointF(left + 4.0, 3.0), QPointF(left + 4.0, 7.0), QPointF(left, 7.0) })
			coords.push_back(MapCoord(georef.toMapCoordF(QPointF(x0 + corner.x(), y0 + corner.y()))));
		auto* area = new PathObject(symbol, coords);
		area->closeAllParts();
		map.map->addObject(area);
		map.map->addObjectToSelection(area, false);
	};
	sample(QStringLiteral("Open land"), 3.0);
	sample(QStringLiteral("Fight"), 13.0);
	error.clear();
	auto const statistics = editor.editor->vegetationStatisticsOfSelection({ cloud }, VegetationParams(), &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(int(statistics.classes.size()), 2);
	for (auto const& c : statistics.classes)
	{
		QCOMPARE(c.cells, qint64(16));
		if (c.name == QLatin1String("Open land"))
			QVERIFY(std::abs(c.vegetation_mean - -1.0) < 1e-9);
		else
		{
			QCOMPARE(c.name, QStringLiteral("Fight"));
			QVERIFY(std::abs(c.vegetation_mean - 0.2) < 1e-9);
		}
	}
}


void ToolsTest::mapTransforms()
{
	// the design notes, "Move, stretch and mirror", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("map-move"));
	QVERIFY(editor.editor->getAction("map-stretch"));
	QVERIFY(editor.editor->getAction("map-mirror"));
	auto const first = MapCoordF(map.line_object->getRawCoordinateVector().front());
	auto const expected = MapCoordF(mapMoveTransform(*map.map, 40.0, -20.0).map(QPointF(first)));
	auto const steps = map.map->undoManager().undoStepCount();
	QVERIFY(editor.editor->moveMap(40.0, -20.0, true));
	auto moved = MapCoordF(map.line_object->getRawCoordinateVector().front());
	QVERIFY(std::abs(moved.x() - expected.x()) < 0.001 && std::abs(moved.y() - expected.y()) < 0.001);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	QVERIFY(editor.editor->stretchMap(2.0, 0.5, false, false, true));
	auto const stretched = MapCoordF(map.line_object->getRawCoordinateVector().front());
	QVERIFY(std::abs(stretched.x() - 2 * moved.x()) < 0.001 && std::abs(stretched.y() - 0.5 * moved.y()) < 0.001);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 2);
}


void ToolsTest::mapAffineTool()
{
	// the design notes, "Affine by point pairs", Accept (3).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("map-affine");
	QVERIFY(action);
	auto* widget = editor.map_widget;
	auto const click = [&](double x, double y) { editor.simulateClick(widget->mapToViewport(MapCoord(x, y))); };
	auto const original = MapCoordF(map.line_object->getRawCoordinateVector().front());
	
	// Escape: no change.
	action->trigger();
	QVERIFY(dynamic_cast<AffineTransformTool*>(editor.editor->getTool()));
	click(0.0, 0.0);
	click(10.0, 0.0);
	click(20.0, 0.0);
	click(30.0, 0.0);
	QTest::keyClick(widget, Qt::Key_Escape);
	QVERIFY(!dynamic_cast<AffineTransformTool*>(editor.editor->getTool()));
	// The tool is deleted later; so is the unchecking of its action.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCOMPARE(MapCoordF(map.line_object->getRawCoordinateVector().front()), original);
	
	// Two pairs, the second point taken back once, and Enter.
	auto const steps = map.map->undoManager().undoStepCount();
	action->trigger();
	auto* tool = dynamic_cast<AffineTransformTool*>(editor.editor->getTool());
	QVERIFY(tool);
	click(0.0, 0.0);
	click(10.0, 0.0);
	click(25.0, 5.0);
	QTest::keyClick(widget, Qt::Key_Backspace);
	click(20.0, 0.0);
	click(30.0, 0.0);
	auto const pairs = tool->pairs();
	QCOMPARE(int(pairs.size()), 2);
	QVERIFY(std::abs(pairs[1].map.x() - 20.0) < 0.5 && std::abs(pairs[1].map.y()) < 0.5);
	auto const expected = MapCoordF(fitPointPairs(pairs).transform.map(QPointF(original)));
	QTest::keyClick(widget, Qt::Key_Return);
	QVERIFY(!dynamic_cast<AffineTransformTool*>(editor.editor->getTool()));
	auto const moved = MapCoordF(map.line_object->getRawCoordinateVector().front());
	QVERIFY(std::abs(moved.x() - expected.x()) < 0.001 && std::abs(moved.y() - expected.y()) < 0.001);
	QVERIFY(std::abs(moved.x() - original.x() - 10.0) < 0.5 && std::abs(moved.y() - original.y()) < 0.5);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
}


void ToolsTest::mapRubbersheetTool()
{
	// the design notes, "Rubbersheeting", Accept (6).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("map-rubbersheet");
	QVERIFY(action);
	auto* widget = editor.map_widget;
	auto const click = [&](double x, double y) { editor.simulateClick(widget->mapToViewport(MapCoord(x, y))); };
	
	// Without a selection: the objects' bounding box grown by 1 mm.
	map.map->clearObjectSelection(false);
	action->trigger();
	auto* tool = dynamic_cast<RubbersheetTool*>(editor.editor->getTool());
	QVERIFY(tool);
	QRectF bounds;
	map.map->applyOnAllObjects([&bounds](const Object* object) {
		for (auto const& c : object->getRawCoordinateVector())
			rectIncludeSafe(bounds, QPointF(MapCoordF(c)));
	});
	QCOMPARE(int(tool->params().perimeter.size()), 1);
	auto const outline = tool->params().perimeter.front();
	QCOMPARE(int(outline.size()), 4);
	QRectF perimeter_box;
	for (auto const& c : outline)
		rectIncludeSafe(perimeter_box, QPointF(c));
	QVERIFY(std::abs(perimeter_box.left() - bounds.left() + 1.0) < 0.001);
	QVERIFY(std::abs(perimeter_box.bottom() - bounds.bottom() - 1.0) < 0.001);
	QTest::keyClick(widget, Qt::Key_Escape);
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	
	// A selected square is the perimeter.
	auto* square = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(-30.0, -30.0), MapCoord(30.0, -30.0), MapCoord(30.0, 30.0), MapCoord(-30.0, 30.0) });
	square->closeAllParts();
	map.map->addObject(square);
	auto* probe = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(60.0, 0.0) });
	map.map->addObject(probe);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(square, true);
	action->trigger();
	tool = dynamic_cast<RubbersheetTool*>(editor.editor->getTool());
	QVERIFY(tool);
	click(0.0, 0.0);
	click(5.0, 0.0);
	auto const params = tool->params();
	QCOMPARE(int(params.pairs.size()), 1);
	QCOMPARE(int(params.perimeter.size()), 1);
	QVERIFY(params.perimeter.front().size() >= 4);
	auto const steps = map.map->undoManager().undoStepCount();
	QVERIFY(editor.editor->rubbersheetMapWith(params));
	auto const inside = MapCoordF(probe->getRawCoordinateVector()[0]);
	auto const shift = params.pairs.front().reference - params.pairs.front().map;
	QVERIFY(std::abs(inside.x() - shift.x()) < 0.001 && std::abs(inside.y() - shift.y()) < 0.001);
	QVERIFY(std::abs(inside.x() - 5.0) < 0.5);
	QCOMPARE(MapCoordF(probe->getRawCoordinateVector()[1]), MapCoordF(60.0, 0.0));
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	QTest::keyClick(widget, Qt::Key_Escape);
}


void ToolsTest::mapLocalWarpTool()
{
	// the design notes, "Local transformation", Accept (4).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("map-local-warp"));
	auto* widget = editor.map_widget;
	auto* probe = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(30.0, 0.0) });
	map.map->addObject(probe);
	LocalWarpParams settings;
	settings.radius = 20.0;
	editor.editor->startLocalWarp(settings);
	QVERIFY(dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	auto const steps = map.map->undoManager().undoStepCount();
	editor.simulateDrag(widget->mapToViewport(MapCoord(0.0, 0.0)).toPoint(), widget->mapToViewport(MapCoord(5.0, 0.0)).toPoint());
	auto const moved = MapCoordF(probe->getRawCoordinateVector()[0]);
	QVERIFY2(std::abs(moved.x() - 5.0) < 0.5 && std::abs(moved.y()) < 0.5, qPrintable(QStringLiteral("%1 %2").arg(moved.x()).arg(moved.y())));
	QCOMPARE(MapCoordF(probe->getRawCoordinateVector()[1]), MapCoordF(30.0, 0.0));
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	// The tool stays; a release beyond the radius changes nothing.
	QVERIFY(dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	editor.simulateDrag(widget->mapToViewport(MapCoord(5.0, 0.0)).toPoint(), widget->mapToViewport(MapCoord(30.0, 0.0)).toPoint());
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	QTest::keyClick(widget, Qt::Key_Escape);
}


void ToolsTest::mapReproject()
{
	// the design notes, "Change of coordinate system", Accept (5).
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("map-reproject"));
	Georeferencing planar;
	planar.setScaleDenominator(10000);
	QVERIFY(planar.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"), { QStringLiteral("26954") }));
	planar.setMapRefPoint(MapCoord(0.0, 0.0));
	planar.setProjectedRefPoint(QPointF(955000.0, 525000.0));
	map.map->setGeoreferencing(planar);
	auto const steps = map.map->undoManager().undoStepCount();
	QVERIFY(editor.editor->reprojectMap(QStringLiteral("EPSG"), QStringLiteral("EPSG:26913"), { QStringLiteral("26913") }));
	QCOMPARE(map.map->getGeoreferencing().getProjectedCRSSpec(), QStringLiteral("EPSG:26913"));
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
}


void ToolsTest::mapRotateMagneticAndCenter()
{
	// the design notes, "Rotation to magnetic north and centring", Accept (7).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* magnetic = editor.editor->getAction("map-rotate-magnetic");
	auto* center = editor.editor->getAction("map-center");
	QVERIFY(magnetic);
	QVERIFY(center);
	QMenu* transform_menu = nullptr;
	for (auto* widget : editor.editor->getAction("map-move")->associatedWidgets())
	{
		if (auto* menu = qobject_cast<QMenu*>(widget))
			transform_menu = menu;
	}
	QVERIFY(transform_menu);
	auto const actions = transform_menu->actions();
	auto const rotate_index = actions.indexOf(editor.editor->getAction("rotatemap"));
	QVERIFY(rotate_index > actions.indexOf(editor.editor->getAction("map-mirror")));
	QCOMPARE(actions.indexOf(magnetic), rotate_index + 1);
	QCOMPARE(actions.indexOf(center), actions.size() - 1);
	
	auto const steps = map.map->undoManager().undoStepCount();
	QVERIFY(editor.editor->rotateMapToMagneticNorth(3.3, false, true));
	QCOMPARE(map.map->getGeoreferencing().getDeclination(), 3.3);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	QVERIFY(editor.editor->centerMap(QPointF(1000.0, 2000.0)));
	auto const offset = mapOffset(*map.map);
	QVERIFY(std::abs(offset.x() - 1000.0) < 0.1 && std::abs(offset.y() - 2000.0) < 0.1);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 2);
}


void ToolsTest::mapAffineWindow()
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map", Accept (8).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("map-affine");
	auto* widget = editor.map_widget;
	auto const click = [&](double x, double y) { editor.simulateClick(widget->mapToViewport(MapCoord(x, y))); };
	auto const original = MapCoordF(map.line_object->getRawCoordinateVector().front());

	action->trigger();
	auto* tool = dynamic_cast<AffineTransformTool*>(editor.editor->getTool());
	QVERIFY(tool);
	QPointer<PointPairsWindow> window = tool->pairsWindow();
	QVERIFY(window);
	QVERIFY(window->isVisible());
	auto* table = window->table();
	click(0.0, 0.0);
	click(10.0, 0.0);
	click(20.0, 0.0);
	click(25.0, 5.0);
	QCOMPARE(table->rowCount(), 2);
	QCOMPARE(table->item(1, 1)->text(), QStringLiteral("2"));

	// Saving and loading.
	QTemporaryDir dir;
	auto const path = dir.filePath(QStringLiteral("pairs.txt"));
	QString error;
	QVERIFY2(tool->savePairs(path, &error), qPrintable(error));
	auto const saved = readRubbersheetPoints(path, *map.map, &error);
	QCOMPARE(saved.size(), std::size_t(2));
	for (std::size_t i = 0; i < saved.size(); ++i)
	{
		QVERIFY(std::abs(saved[i].map.x() - tool->pairs()[i].map.x()) < 0.01);
		QVERIFY(std::abs(saved[i].reference.y() - tool->pairs()[i].reference.y()) < 0.01);
	}
	window->findChild<QPushButton*>(QStringLiteral("remove_all"))->click();
	QCOMPARE(table->rowCount(), 0);
	QVERIFY(tool->pairs().empty());
	QVERIFY2(tool->loadPairs(path, &error), qPrintable(error));
	QCOMPARE(table->rowCount(), 2);
	QCOMPARE(int(tool->pairs().size()), 2);

	// The first pair left out: the translation of the second.
	table->item(0, 0)->setCheckState(Qt::Unchecked);
	QVERIFY(!tool->pairs()[0].used);
	auto const shift = tool->pairs()[1].reference - tool->pairs()[1].map;
	auto const steps = map.map->undoManager().undoStepCount();
	window->findChild<QPushButton*>(QStringLiteral("transform"))->click();
	QVERIFY(!dynamic_cast<AffineTransformTool*>(editor.editor->getTool()));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!window);
	auto const moved = MapCoordF(map.line_object->getRawCoordinateVector().front());
	QVERIFY(std::abs(moved.x() - original.x() - shift.x()) < 0.001 && std::abs(moved.y() - original.y() - shift.y()) < 0.001);
	QVERIFY(std::abs(shift.x() - 5.0) < 0.5 && std::abs(shift.y() - 5.0) < 0.5);
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
}


void ToolsTest::mapRubbersheetWindow()
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map", Accept (9).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* widget = editor.map_widget;
	auto const click = [&](double x, double y) { editor.simulateClick(widget->mapToViewport(MapCoord(x, y))); };
	auto* square = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(-30.0, -30.0), MapCoord(30.0, -30.0), MapCoord(30.0, 30.0), MapCoord(-30.0, 30.0) });
	square->closeAllParts();
	map.map->addObject(square);
	auto* probe = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(60.0, 0.0), MapCoord(100.0, 0.0) });
	map.map->addObject(probe);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(square, true);

	editor.editor->getAction("map-rubbersheet")->trigger();
	auto* tool = dynamic_cast<RubbersheetTool*>(editor.editor->getTool());
	QVERIFY(tool);
	QPointer<PointPairsWindow> window = tool->pairsWindow();
	QVERIFY(window && window->isVisible());
	auto* summary = window->findChild<QLabel*>(QStringLiteral("perimeter_summary"));
	QVERIFY(summary);
	QVERIFY2(summary->text().startsWith(QStringLiteral("1 polygon")), qPrintable(summary->text()));
	QCOMPARE(int(tool->params().perimeter.size()), 1);

	// A second polygon around (60, 0).
	auto* define = window->findChild<QPushButton*>(QStringLiteral("define"));
	define->click();
	QVERIFY(tool->isDefining());
	click(50.0, -10.0);
	click(70.0, -10.0);
	click(70.0, 10.0);
	click(50.0, 10.0);
	define->click();
	QVERIFY(!tool->isDefining());
	QCOMPARE(int(tool->params().perimeter.size()), 2);
	QCOMPARE(int(tool->params().perimeter.back().size()), 4);
	QVERIFY2(summary->text().startsWith(QStringLiteral("2 polygon")), qPrintable(summary->text()));

	click(0.0, 0.0);
	click(5.0, 0.0);
	QCOMPARE(int(tool->params().pairs.size()), 1);
	QCOMPARE(window->table()->rowCount(), 1);
	auto const steps = map.map->undoManager().undoStepCount();
	window->findChild<QPushButton*>(QStringLiteral("transform"))->click();
	QVERIFY(!dynamic_cast<RubbersheetTool*>(editor.editor->getTool()));
	QCOMPARE(map.map->undoManager().undoStepCount(), steps + 1);
	auto const& coords = probe->getRawCoordinateVector();
	QVERIFY(std::abs(MapCoordF(coords[0]).x() - 5.0) < 0.5);
	QVERIFY(std::abs(MapCoordF(coords[1]).x() - 65.0) < 0.5);
	QCOMPARE(MapCoordF(coords[2]), MapCoordF(100.0, 0.0));
	auto const message = editor.window->statusBar()->currentMessage();
	QVERIFY2(message.contains(QStringLiteral("largest displacement")), qPrintable(message));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!window);
}


void ToolsTest::mapLocalWarpDialog()
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map", Accept (10).
	TestMap map;
	TestMapEditor editor(map.map);
	auto* widget = editor.map_widget;
	auto* probe = new PathObject(map.line_symbol, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(30.0, 0.0) });
	map.map->addObject(probe);

	editor.editor->getAction("map-local-warp")->trigger();
	auto* dialog = editor.editor->localWarpDialog();
	QVERIFY(dialog);
	QVERIFY(dialog->isVisible());
	QVERIFY(!dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	auto* radius = dialog->findChild<QSpinBox*>(QStringLiteral("radius"));
	QVERIFY(radius);
	radius->setValue(20);
	dialog->findChild<QPushButton*>(QStringLiteral("start"))->click();
	QVERIFY(dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));

	editor.simulateDrag(widget->mapToViewport(MapCoord(0.0, 0.0)).toPoint(), widget->mapToViewport(MapCoord(5.0, 0.0)).toPoint());
	QCOMPARE(MapCoordF(probe->getRawCoordinateVector()[1]), MapCoordF(30.0, 0.0));

	// The radius changed in the dialog applies to the next drag.
	radius->setValue(40);
	editor.simulateDrag(widget->mapToViewport(MapCoord(0.0, 0.0)).toPoint(), widget->mapToViewport(MapCoord(5.0, 0.0)).toPoint());
	auto const moved = MapCoordF(probe->getRawCoordinateVector()[1]);
	QVERIFY2(std::abs(moved.x() - 31.25) < 0.3 && std::abs(moved.y()) < 0.3, qPrintable(QStringLiteral("%1 %2").arg(moved.x()).arg(moved.y())));

	QTest::keyClick(widget, Qt::Key_Escape);
	QVERIFY(!dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	QVERIFY(dialog->isVisible());
	
	// Seen on 2026-11-22: Escape right after Start, the dialog still with the focus, hid the dialog too.
	dialog->findChild<QPushButton*>(QStringLiteral("start"))->click();
	QVERIFY(dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	QTest::keyClick(dialog, Qt::Key_Escape);
	QVERIFY(!dynamic_cast<LocalWarpTool*>(editor.editor->getTool()));
	QVERIFY(dialog->isVisible());
	
	dialog->findChild<QPushButton*>(QStringLiteral("close"))->click();
	QVERIFY(!dialog->isVisible());
}


void ToolsTest::mapReprojectDialog()
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map", Accept (11).
	Georeferencing planar;
	planar.setScaleDenominator(4000);
	QVERIFY(planar.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"), { QStringLiteral("26954") }));
	planar.setMapRefPoint(MapCoord(0.0, 0.0));
	planar.setProjectedRefPoint(QPointF(955000.0, 525000.0));
	ReprojectDialog dialog(planar, nullptr);
	dialog.setTargetCRS(QStringLiteral("EPSG"), { QStringLiteral("26913") });
	QCOMPARE(dialog.crsId(), QStringLiteral("EPSG"));
	QCOMPARE(dialog.crsParameters(), std::vector<QString>{ QStringLiteral("26913") });
	QCOMPARE(dialog.targetOffset(), QPointF(498000.0, 4407000.0));
	QCOMPARE(dialog.findChild<QDoubleSpinBox*>(QStringLiteral("east_offset"))->value(), 498000.0);
	QCOMPARE(dialog.findChild<QDoubleSpinBox*>(QStringLiteral("north_offset"))->value(), 4407000.0);
}


void ToolsTest::mapCheck()
{
	// the design notes, "Map check", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("check-map") && editor.editor->getAction("issueswindow"));
	auto* blank = new TextObject(new TextSymbol());
	map.map->addSymbol(const_cast<Symbol*>(blank->getSymbol()), 1);
	blank->setText(QString{});
	map.map->addObject(blank);
	auto const count = editor.editor->runMapCheck();
	QVERIFY(count >= 1);
	QCOMPARE(editor.editor->issueModel()->rowCount(), count);
	QVERIFY(editor.editor->getAction("issueswindow")->isChecked());
	editor.editor->issueModel()->setRuleFilter(rule::text_empty);
	QCOMPARE(editor.editor->issueModel()->rowCount(), 1);
	auto cancelled = true;
	QVERIFY(editor.editor->fixIssueRow(0, &cancelled));
	QVERIFY(!cancelled);
	QVERIFY(!map.map->findObject(blank->uuid()));
	// The check ran again: the rule is gone, and the dock has dropped the filter for it.
	for (auto const& issue : editor.editor->issueModel()->issues())
		QVERIFY(issue.rule != rule::text_empty);

	// The assign-symbol fix takes the symbol selected in the symbol pane.
	auto* point_symbol = new PointSymbol();
	map.map->addSymbol(point_symbol, map.map->getNumSymbols());
	auto* wrong = new PathObject(point_symbol, { MapCoord(0.0, 50.0), MapCoord(10.0, 50.0) });
	map.map->addObject(wrong);
	auto const wrong_uuid = wrong->uuid();
	editor.editor->runMapCheck();
	editor.editor->issueModel()->setRuleFilter(rule::symbol_wrong_type);
	QCOMPARE(editor.editor->issueModel()->rowCount(), 1);
	editor.editor->getSymbolWidget()->selectSingleSymbol(point_symbol);
	QVERIFY(!editor.editor->assignSymbolToIssueRow(0));  // does not fit
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	QVERIFY(editor.editor->assignSymbolToIssueRow(0));
	QCOMPARE(map.map->findObject(wrong_uuid)->getSymbol(), static_cast<const Symbol*>(map.line_symbol));
	for (auto const& issue : editor.editor->issueModel()->issues())
		QVERIFY(issue.rule != rule::symbol_wrong_type);
	auto* dock_widget = editor.window->findChild<QPushButton*>(QStringLiteral("assign_symbol_button"));
	QVERIFY(dock_widget);

	// foom: the relink fix of template-missing.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const found = QDir(dir.path()).absoluteFilePath(QStringLiteral("a.png"));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(found, "PNG"));
	map.map->addTemplate(-1, std::make_unique<TemplateImage>(QDir(dir.path()).absoluteFilePath(QStringLiteral("gone/a.png")), map.map));
	map.map->getTemplate(map.map->getNumTemplates() - 1)->setTemplateState(Template::Invalid);
	editor.editor->runMapCheck();
	editor.editor->issueModel()->setRuleFilter(rule::template_missing);
	QCOMPARE(editor.editor->issueModel()->rowCount(), 1);
	QVERIFY(editor.editor->relinkIssueRow(0, found));
	QCOMPARE(map.map->getTemplate(map.map->getNumTemplates() - 1)->getTemplateState(), Template::Loaded);
	for (auto const& issue : editor.editor->issueModel()->issues())
		QVERIFY(issue.rule != rule::template_missing);
}

void ToolsTest::legibility()
{
	// the design notes, "Legibility", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("check-legibility"));
	map.line_symbol->setMinimumLength(2000);
	auto* short_line = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(1.0, 0.0) });
	map.map->addObject(short_line);

	LegibilityDialog dialog(editor.editor, editor.window);
	auto params = dialog.params();
	QCOMPARE(params.gap_mm, defaultGap(*map.map));
	QVERIFY(params.minimum_length && params.minimum_area && params.gap);
	QVERIFY(params.same_colour_group_only);
	QVERIFY(!params.between_contours);
	QVERIFY(params.symbols.empty());
	QVERIFY(params.extent.isNull());

	auto const count = editor.editor->runLegibilityCheck(params);
	QVERIFY(count >= 1);
	QVERIFY(editor.editor->getAction("issueswindow")->isChecked());
	auto const has_short_line = [&editor, short_line]() {
		for (auto const& issue : editor.editor->issueModel()->issues())
		{
			if (issue.rule == rule::line_short && issue.objects.contains(short_line->uuid()))
				return true;
		}
		return false;
	};
	QVERIFY(has_short_line());
	// Check again repeats the legibility scan, not the map check.
	editor.editor->checkAgain();
	QVERIFY(has_short_line());
	// foom: the results as selections and markers.
	auto const has_selection = [&map](const char* name) {
		for (auto const& selection : map.map->savedSelections())
		{
			if (selection.name == QLatin1String(name))
				return true;
		}
		return false;
	};
	QVERIFY(has_selection("Legibility: short lines"));
	map.map->addObject(new PathObject(map.line_symbol, { MapCoord(100.0, 0.0), MapCoord(110.0, 0.0) }));
	map.map->addObject(new PathObject(map.line_symbol, { MapCoord(105.0, 1.1), MapCoord(105.0, 10.0) }));
	editor.editor->runLegibilityCheck(params);
	QVERIFY(has_selection("Legibility: gaps"));
	QVERIFY(map.map->findPartIndexWithRole(MapPart::Role::Issues) >= 0);
	QVERIFY(map.map->getPart(std::size_t(map.map->findPartIndexWithRole(MapPart::Role::Issues)))->getNumObjects() >= 2);
	editor.editor->runMapCheck();
	QVERIFY(!has_short_line());
	
	// The minimum gap per symbol in the symbol editor.
	{
		SymbolSettingDialog symbol_dialog(map.line_symbol, map.map, nullptr);
		auto* gap_edit = symbol_dialog.findChild<QDoubleSpinBox*>(QStringLiteral("minimum_gap_edit"));
		QVERIFY(gap_edit);
		QCOMPARE(gap_edit->value(), 0.0);
		QVERIFY(!gap_edit->specialValueText().isEmpty());
		QVERIFY(!symbol_dialog.isSymbolModified());
		gap_edit->setValue(0.3);
		QVERIFY(symbol_dialog.isSymbolModified());
		QCOMPARE(symbol_dialog.getNewSymbol()->minimumGap(), 300);
	}
	map.line_symbol->setMinimumGap(450);
	{
		SymbolSettingDialog symbol_dialog(map.line_symbol, map.map, nullptr);
		QCOMPARE(symbol_dialog.findChild<QDoubleSpinBox*>(QStringLiteral("minimum_gap_edit"))->value(), 0.45);
	}
	map.line_symbol->setMinimumGap(0);
}

void ToolsTest::drawingFeedback()
{
	// the design notes, "Legibility", drawing-time feedback.
	TestMap map;
	TestMapEditor editor(map.map);
	auto& settings = Settings::getInstance();
	auto const old_check = settings.getSetting(Settings::MapEditor_LegibilityMinimumCheck);
	auto const old_halo = settings.getSetting(Settings::MapEditor_LegibilityHalo);
	auto const old_halo_mm = settings.getSetting(Settings::MapEditor_LegibilityHaloMM);
	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, true);

	map.line_symbol->setMinimumLength(2000);
	auto* area_symbol = new AreaSymbol();
	area_symbol->setMinimumArea(1000);
	map.map->addSymbol(area_symbol, 1);
	auto const* status = editor.window->statusBar()->findChild<QLabel*>();
	QVERIFY(status);

	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	editor.editor->setTool(new DrawPathTool(editor.editor, nullptr, false, true));
	auto* tool = editor.editor->getTool();
	tool->acceptNumericVertex(MapCoordF(0.0, 0.0));
	tool->acceptNumericVertex(MapCoordF(1.0, 0.0));
	QVERIFY2(status->text().contains(QLatin1String("1.00 / 2.00 mm")), qPrintable(status->text()));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QVERIFY(status->text().contains(QLatin1String("50 % short")));
	tool->acceptNumericVertex(MapCoordF(3.0, 0.0));
	QVERIFY2(status->text().contains(QLatin1String("3.00 / 2.00 mm")), qPrintable(status->text()));
	QVERIFY(!status->text().contains(QLatin1String("red")));
	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, false);
	tool->acceptNumericVertex(MapCoordF(4.0, 0.0));
	QVERIFY(!status->text().contains(QLatin1String(" / 2.00 mm")));
	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, true);
	editor.editor->setTool(nullptr);

	editor.editor->getSymbolWidget()->selectSingleSymbol(area_symbol);
	editor.editor->setTool(new DrawPathTool(editor.editor, nullptr, false, true));
	tool = editor.editor->getTool();
	tool->acceptNumericVertex(MapCoordF(0.0, 0.0));
	tool->acceptNumericVertex(MapCoordF(1.0, 0.0));
	tool->acceptNumericVertex(MapCoordF(1.0, 0.5));
	QVERIFY2(status->text().contains(QString::fromUtf8("0.25 / 1.00 mm\u00b2")), qPrintable(status->text()));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QVERIFY(status->text().contains(QLatin1String("75 % short")));
	editor.editor->setTool(nullptr);

	settings.setSettingInCache(Settings::MapEditor_LegibilityHaloMM, 0.0);
	QCOMPARE(legibilityHaloDistance(*map.map), defaultGap(*map.map));
	settings.setSettingInCache(Settings::MapEditor_LegibilityHaloMM, 0.3);
	QCOMPARE(legibilityHaloDistance(*map.map), 0.3);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, false);
	QVERIFY(!legibilityHaloEnabled());
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, true);
	QVERIFY(legibilityHaloEnabled());

	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, old_check);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, old_halo);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHaloMM, old_halo_mm);
}

void ToolsTest::drawingFeedbackShapes()
{
	// the design notes, "Legibility", the other drawing tools, Accept.
	TestMap map;
	TestMapEditor editor(map.map);
	auto& settings = Settings::getInstance();
	auto const old_check = settings.getSetting(Settings::MapEditor_LegibilityMinimumCheck);
	auto const old_halo = settings.getSetting(Settings::MapEditor_LegibilityHalo);
	auto const old_halo_mm = settings.getSetting(Settings::MapEditor_LegibilityHaloMM);
	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, true);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, true);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHaloMM, 2.0);

	map.line_symbol->setMinimumLength(40000);
	auto* area_symbol = new AreaSymbol();
	area_symbol->setMinimumArea(100000);
	map.map->addSymbol(area_symbol, 1);
	auto const* status = editor.window->statusBar()->findChild<QLabel*>();
	QVERIFY(status);

	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos, Qt::MouseButtons buttons) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, buttons, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	// The measured value of the readout with the given label, or -1.
	auto const measured = [status](const char* label) {
		QRegularExpression const pattern(QLatin1String("<b>") + QLatin1String(label) + QLatin1String(R"(</b> ([0-9.]+) / ([0-9.]+) mm)"));
		auto const match = pattern.match(status->text());
		return match.hasMatch() ? match.captured(1).toDouble() : -1.0;
	};
	// The pixel the active tool paints at (x, y) into a white image.
	auto const paintedAt = [&](double x, double y) {
		QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::white);
		QPainter painter(&image);
		editor.editor->getTool()->draw(&painter, widget);
		painter.end();
		auto const pos = viewport(x, y);
		return image.rect().contains(pos) ? image.pixel(pos) : qRgb(0, 0, 0);
	};
	auto const isHalo = [](QRgb pixel) {
		return pixel != qRgb(255, 255, 255) && qBlue(pixel) > qRed(pixel) + 20;
	};
	auto const startRectangle = [&](double far_y) {
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
		move(viewport(10.0, 0.0), Qt::NoButton);
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(10.0, 0.0));
		move(viewport(10.0, far_y), Qt::NoButton);
	};

	// (1) Rectangle tool, line symbol.
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	editor.editor->setTool(new DrawRectangleTool(editor.editor, nullptr, false));
	startRectangle(5.0);
	QVERIFY2(std::abs(measured("Length:") - 30.0) < 1.0, qPrintable(status->text()));
	QVERIFY(status->text().contains(QLatin1String("/ 40.00 mm")));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QVERIFY(status->text().contains(QLatin1String("short")));
	QVERIFY2(isHalo(paintedAt(5.0, -1.0)), qPrintable(QString::number(paintedAt(5.0, -1.0), 16)));
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, false);
	QCOMPARE(paintedAt(5.0, -1.0), qRgb(255, 255, 255));
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, true);
	QTest::keyClick(widget, Qt::Key_Return);
	QVERIFY2(!status->text().contains(QLatin1String("Length:")), qPrintable(status->text()));
	editor.editor->setTool(nullptr);

	// (2) Rectangle tool, area symbol.
	editor.editor->getSymbolWidget()->selectSingleSymbol(area_symbol);
	editor.editor->setTool(new DrawRectangleTool(editor.editor, nullptr, false));
	startRectangle(5.0);
	QVERIFY2(std::abs(measured("Area:") - 50.0) < 3.0, qPrintable(status->text()));
	QVERIFY(status->text().contains(QString::fromUtf8("/ 100.00 mm²")));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QTest::keyClick(widget, Qt::Key_Escape);
	editor.editor->setTool(nullptr);

	// (3) Circle tool, dragged.
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	editor.editor->setTool(new DrawCircleTool(editor.editor, nullptr, false));
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(10.0, 0.0), Qt::LeftButton);
	QVERIFY2(std::abs(measured("Length:") - 31.4) < 1.0, qPrintable(status->text()));
	QVERIFY(status->text().contains(QLatin1String("/ 40.00 mm")));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QVERIFY(isHalo(paintedAt(5.0, 6.0)));
	QVERIFY(isHalo(paintedAt(5.0, -6.0)));
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(10.0, 0.0));
	QVERIFY2(!status->text().contains(QLatin1String("Length:")), qPrintable(status->text()));
	editor.editor->setTool(nullptr);

	editor.editor->getSymbolWidget()->selectSingleSymbol(area_symbol);
	editor.editor->setTool(new DrawCircleTool(editor.editor, nullptr, false));
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(10.0, 0.0), Qt::LeftButton);
	QVERIFY2(std::abs(measured("Area:") - 78.5) < 3.0, qPrintable(status->text()));
	QVERIFY(status->text().contains(QString::fromUtf8("/ 100.00 mm²")));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(10.0, 0.0));
	editor.editor->setTool(nullptr);

	// (4) Freehand tool.
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	editor.editor->setTool(new DrawFreehandTool(editor.editor, nullptr, false));
	QTest::mousePress(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(5.0, 0.0), Qt::LeftButton);
	move(viewport(10.0, 0.0), Qt::LeftButton);
	QVERIFY2(std::abs(measured("Length:") - 10.0) < 1.0, qPrintable(status->text()));
	QVERIFY(status->text().contains(QLatin1String("/ 40.00 mm")));
	QVERIFY(status->text().contains(QLatin1String("red")));
	QVERIFY(status->text().contains(QLatin1String("short")));
	QVERIFY(isHalo(paintedAt(5.0, 1.0)));
	map.line_symbol->setMinimumLength(5000);
	move(viewport(11.0, 0.0), Qt::LeftButton);
	QVERIFY2(status->text().contains(QLatin1String("/ 5.00 mm")), qPrintable(status->text()));
	QVERIFY(!status->text().contains(QLatin1String("red")));
	map.line_symbol->setMinimumLength(40000);
	QTest::mouseRelease(widget, Qt::LeftButton, {}, viewport(11.0, 0.0));
	QVERIFY2(!status->text().contains(QLatin1String("Length:")), qPrintable(status->text()));
	editor.editor->setTool(nullptr);

	// (5) Stairway tool: neither readout nor halo.
	editor.editor->drawStairway(1.0);
	QVERIFY(dynamic_cast<StairwayTool*>(editor.editor->getTool()));
	startRectangle(4.0);
	QVERIFY2(!status->text().contains(QLatin1String("Length:")), qPrintable(status->text()));
	QCOMPARE(paintedAt(5.0, -1.0), qRgb(255, 255, 255));
	QTest::keyClick(widget, Qt::Key_Escape);
	editor.editor->setTool(nullptr);

	// (6) Rectangle box tool: both.
	ops::RectangleBoxParams params;
	editor.editor->drawRectangleBox(params);
	QVERIFY(dynamic_cast<RectangleBoxTool*>(editor.editor->getTool()));
	startRectangle(5.0);
	QVERIFY2(std::abs(measured("Length:") - 30.0) < 1.0, qPrintable(status->text()));
	QVERIFY(isHalo(paintedAt(5.0, -1.0)));
	QTest::keyClick(widget, Qt::Key_Escape);
	editor.editor->setTool(nullptr);

	settings.setSettingInCache(Settings::MapEditor_LegibilityMinimumCheck, old_check);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHalo, old_halo);
	settings.setSettingInCache(Settings::MapEditor_LegibilityHaloMM, old_halo_mm);
}

void ToolsTest::topology()
{
	// the design notes, "Topology commands", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	for (auto const* id : { "topo-join", "close-areas", "areas-ccw", "fix-overshoots", "intersections", "remove-duplicate-vertices" })
		QVERIFY2(editor.editor->getAction(id), id);
	auto* part = map.map->getCurrentPart();
	auto const num_objects = part->getNumObjects();
	auto* a = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	auto* b = new PathObject(map.line_symbol, { MapCoord(10.1, 0.0), MapCoord(20.0, 0.0) });
	map.map->addObject(a);
	map.map->addObject(b);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(a, false);
	map.map->addObjectToSelection(b, true);
	editor.editor->joinLines(0.25);
	QCOMPARE(part->getNumObjects(), num_objects + 1);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QCOMPARE(map.map->getFirstSelectedObject()->asPath()->getCoordinateCount(), MapCoordVector::size_type(3));

	auto* area_symbol = new AreaSymbol();
	map.map->addSymbol(area_symbol, 1);
	auto* open_area = new PathObject(area_symbol, { MapCoord(0.0, 50.0), MapCoord(10.0, 50.0), MapCoord(10.0, 60.0) });
	map.map->addObject(open_area);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(open_area, true);
	QCOMPARE(topologyScopeObjects(editor.editor, TopologyScope::Selection).size(), std::size_t(1));
	QCOMPARE(topologyScopeObjects(editor.editor, TopologyScope::AllObjects).size(), std::size_t(part->getNumObjects()));
	editor.editor->closeAreas(int(TopologyScope::Selection));
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->getFirstSelectedObject()->asPath()->parts().front().isClosed());
	QVERIFY(map.map->undoManager().undo());
}

void ToolsTest::buildings()
{
	// the design notes, "Make rectangular" and "Generalize buildings", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("make-rectangular") && editor.editor->getAction("generalize-buildings"));
	auto* area_symbol = new AreaSymbol();
	map.map->addSymbol(area_symbol, 1);
	auto* skewed = new PathObject(area_symbol);
	for (auto const& coord : { MapCoord(0.0, 0.0), MapCoord(10.0, 0.2), MapCoord(10.2, 5.0), MapCoord(0.0, 5.1) })
		skewed->addCoordinate(coord);
	skewed->parts().front().setClosed(true);
	map.map->addObject(skewed);
	auto* reference = new PathObject(map.line_symbol, { MapCoord(0.0, 20.0), MapCoord(10.0, 20.0) });
	map.map->addObject(reference);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(reference, false);
	map.map->addObjectToSelection(skewed, true);

	MakeRectangularDialog dialog(editor.editor, editor.window);
	auto params = dialog.params();
	QCOMPARE(params.tolerance_deg, 15.0);
	QVERIFY(!params.reference);
	params.reference = reference;
	editor.editor->makeRectangular(params);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QCOMPARE(map.map->getFirstSelectedObject()->asPath()->getCoordinateCount(), MapCoordVector::size_type(5));
	QVERIFY(map.map->findObject(reference->uuid()));

	GeneralizeBuildingsDialog generalize(editor.window);
	QVERIFY(generalize.simplify());
	QCOMPARE(generalize.rectangleParams().area_factor, 1.0);
	auto* mode_combo = generalize.findChild<QComboBox*>(QStringLiteral("rectangle_mode_combo"));
	QVERIFY(mode_combo);
	auto const corner_index = mode_combo->findData(int(ops::RectangleParams::Corner));
	QVERIFY(corner_index >= 0);
	mode_combo->setCurrentIndex(corner_index);
	QCOMPARE(generalize.rectangleParams().mode, ops::RectangleParams::Corner);
	ops::RectangleParams rectangle;
	rectangle.area_factor = 2.0;
	editor.editor->buildingRectangles(rectangle);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->undoManager().undo());
	QVERIFY(map.map->findObject(skewed->uuid()));
}

void ToolsTest::smooth()
{
	// the design notes, "Smooth", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	QVERIFY(editor.editor->getAction("topo-smooth"));
	MapCoordVector zigzag;
	for (int i = 0; i <= 10; ++i)
		zigzag.emplace_back(double(i), (i == 0 || i == 10) ? 0.0 : (i % 2 ? 0.05 : -0.05));
	auto* zig = new PathObject(map.line_symbol, zigzag);
	map.map->addObject(zig);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(zig, true);
	SmoothDialog dialog(editor.window);
	auto const params = dialog.params();
	QCOMPARE(params.tolerance, 0.1);
	QVERIFY(!params.keep_corners && !params.keep_inflections);
	editor.editor->smoothPaths(params);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QCOMPARE(map.map->getFirstSelectedObject()->asPath()->getCoordinateCount(), MapCoordVector::size_type(2));
	QVERIFY(map.map->undoManager().undo());
	
	// The contour option is offered only with a terrain grid.
	{
		SmoothDialog no_grid(editor.window, false);
		auto* check = no_grid.findChild<QCheckBox*>(QStringLiteral("contour_check"));
		QVERIFY(check);
		QVERIFY(!check->isEnabled());
		QVERIFY(!no_grid.params().contour);
		SmoothDialog with_grid(editor.window, true);
		auto* height = with_grid.findChild<QDoubleSpinBox*>(QStringLiteral("height_edit"));
		QVERIFY(height);
		QCOMPARE(height->value(), 3.0);
		QVERIFY(!height->isEnabled());
		with_grid.findChild<QCheckBox*>(QStringLiteral("contour_check"))->setChecked(true);
		QVERIFY(height->isEnabled());
		QVERIFY(with_grid.params().contour);
		QCOMPARE(with_grid.params().height_tolerance_m, 0.03);
	}
	
	// With a terrain grid whose contour of height 0 has a spike to (5, 0.08) on the map,
	// the contour option keeps the spike which plain smoothing removes.
	auto const& georef = map.map->getGeoreferencing();
	auto const spike = [](double x) { return 0.08 * std::max(0.0, 1.0 - std::abs(x - 5.0)); };
	auto const corner_a = georef.toProjectedCoords(MapCoordF(-1.0, -1.0));
	auto const corner_b = georef.toProjectedCoords(MapCoordF(11.0, 1.0));
	auto const metres_per_mm = std::abs(georef.toProjectedCoords(MapCoordF(1.0, 0.0)).x() - georef.toProjectedCoords(MapCoordF(0.0, 0.0)).x());
	auto const cell = metres_per_mm * 0.005;
	auto const west = std::min(corner_a.x(), corner_b.x());
	auto const north = std::max(corner_a.y(), corner_b.y());
	auto const width = int(std::ceil(std::abs(corner_b.x() - corner_a.x()) / cell));
	auto const height = int(std::ceil(std::abs(corner_b.y() - corner_a.y()) / cell));
	std::vector<float> values(std::size_t(width) * std::size_t(height));
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			auto const at = georef.toMapCoordF(QPointF(west + (col + 0.5) * cell, north - (row + 0.5) * cell));
			values[std::size_t(row) * std::size_t(width) + std::size_t(col)] = float(at.y() - spike(at.x()));
		}
	}
	map.map->elevationSet().setModel(ElevationSet::Terrain, std::make_unique<ElevationModel>(width, height, QPointF(west, north), cell, values), QStringLiteral("ridge.tif"));
	auto* contour = new PathObject(map.line_symbol, { MapCoord(0.0, 0.0), MapCoord(4.0, 0.0), MapCoord(5.0, 0.08), MapCoord(6.0, 0.0), MapCoord(10.0, 0.0) });
	map.map->addObject(contour);
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(contour, true);
	auto contour_params = params;
	contour_params.contour = true;
	editor.editor->smoothPaths(contour_params);
	QCOMPARE(map.map->getNumSelectedObjects(), 1);
	QVERIFY(map.map->getFirstSelectedObject()->asPath()->getCoordinateCount() > 2);
	QVERIFY(map.map->undoManager().undo());
	map.map->clearObjectSelection(false);
	map.map->addObjectToSelection(map.map->getCurrentPart()->getObject(map.map->getCurrentPart()->getNumObjects() - 1), true);
	editor.editor->smoothPaths(params);
	QCOMPARE(map.map->getFirstSelectedObject()->asPath()->getCoordinateCount(), MapCoordVector::size_type(2));
}

void ToolsTest::rectangleBox()
{
	// the design notes, "Rectangle objects", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* action = editor.editor->getAction("draw-rectangle-box");
	QVERIFY(action);
	auto* text_symbol = new TextSymbol();
	map.map->addSymbol(text_symbol, 1);
	editor.editor->getSymbolWidget()->selectSingleSymbol(map.line_symbol);
	QVERIFY(action->isEnabled());
	
	RectangleBoxDialog dialog(map.map, editor.window);
	auto params = dialog.params();
	QCOMPARE(params.corner_radius, 0.0);
	QCOMPARE(params.cell_width, 0.0);
	QVERIFY(!params.numbering);
	QCOMPARE(params.number_text, text_symbol);
	
	params.cell_width = 10.0;
	params.cell_height = 10.0;
	params.numbering = true;
	auto const objects_before = map.map->getNumObjects();
	editor.editor->drawRectangleBox(params);
	auto* widget = editor.map_widget;
	auto const viewport = [widget](double x, double y) { return widget->mapToViewport(MapCoord(x, y)).toPoint(); };
	auto const move = [widget](const QPoint& pos) {
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	};
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(0.0, 0.0));
	move(viewport(30.0, 0.0));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(30.0, 0.0));
	move(viewport(30.0, 20.0));
	QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(30.0, 20.0));
	QTest::keyClick(widget, Qt::Key_Return);  // drops the hover corner and finishes, as a right click does
	QCOMPARE(map.map->getNumObjects(), objects_before + 10);
	QCOMPARE(map.map->getNumSelectedObjects(), 10);
	auto group = 0;
	for (auto const* object : map.map->selectedObjects())
	{
		QVERIFY(object->group() != 0);
		if (group == 0)
			group = object->group();
		QCOMPARE(object->group(), group);
	}
	QVERIFY(map.map->undoManager().undo());
	QCOMPARE(map.map->getNumObjects(), objects_before);
	editor.editor->setTool(nullptr);
}

void ToolsTest::preferredTool()
{
	// the design notes, "Line symbol properties", the editor part.
	TestMap map;
	TestMapEditor editor(map.map);
	auto* preferring = new LineSymbol();
	preferring->setColor(map.map->getColor(0));
	preferring->setLineWidth(0.3);
	preferring->setPreferredTool(QStringLiteral("drawrectangle"));
	map.map->addSymbol(preferring, 1);
	editor.editor->setTool(new EditPointTool(editor.editor, nullptr));
	editor.editor->getSymbolWidget()->selectSingleSymbol(preferring);
	QVERIFY(editor.editor->getTool());
	QCOMPARE(editor.editor->getTool()->toolAction(), editor.editor->getAction("drawrectangle"));
	editor.editor->setTool(nullptr);
}


QTEST_MAIN(ToolsTest)
