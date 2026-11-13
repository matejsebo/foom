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

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QString>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/object_query.h"
#include "core/ops/align.h"
#include "tools/lasso_tool.h"
#include "tools/move_parallel_tool.h"
#include "tools/reshape_tool.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "global.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_find_feature.h"
#include "gui/map/map_widget.h"
#include "templates/paint_on_template_feature.h"
#include "tools/edit_point_tool.h"
#include "tools/edit_tool.h"
#include "undo/undo_manager.h"

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


QTEST_MAIN(ToolsTest)
