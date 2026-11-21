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
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QMouseEvent>
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

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/map_transform.h"
#include "core/objects/object.h"
#include "core/objects/object_query.h"
#include "core/ops/align.h"
#include "core/bookmarks.h"
#include "core/extents.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "gui/map/extent_dialog.h"
#include "check/issue.h"
#include "check/legibility.h"
#include "check/map_check.h"
#include "gui/map/legibility_dialog.h"
#include "gui/map/rectangle_box_dialog.h"
#include "gui/map/topology_dialogs.h"
#include "core/ops/rectangle_box.h"
#include "tools/legibility_feedback.h"
#include "settings.h"
#include "core/georeferencing.h"
#include "core/map_view.h"
#include "gui/map/view_history.h"
#include "gui/widgets/shortcut_settings.h"
#include "gui/map/numeric_entry_bar.h"
#include "gui/widgets/symbol_widget.h"
#include "tools/affine_transform_tool.h"
#include "tools/local_warp_tool.h"
#include "tools/rubbersheet_tool.h"
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
#include "gui/map/map_find_feature.h"
#include "gui/map/map_widget.h"
#include "templates/paint_on_template_feature.h"
#include "tools/edit_point_tool.h"
#include "tools/edit_tool.h"
#include "undo/undo_manager.h"
#include "util/util.h"

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
	QVERIFY2(editor.editor->importPointClouds({ cloud }, GriddingParams(), products, &error), qPrintable(error));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_vegetation_height.tif"))));
	QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("plane_vegetation.tif"))));
	QCOMPARE(map.map->getNumTemplates(), templates + 2);
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
	auto const outline = tool->params().perimeter;
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
	QVERIFY(params.perimeter.size() >= 4);
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
	QVERIFY(editor.editor->fixIssueRow(0));
	QVERIFY(!map.map->findObject(blank->uuid()));
	// The check ran again: the rule is gone, and the dock has dropped the filter for it.
	for (auto const& issue : editor.editor->issueModel()->issues())
		QVERIFY(issue.rule != rule::text_empty);
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
	editor.editor->runMapCheck();
	QVERIFY(!has_short_line());
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
