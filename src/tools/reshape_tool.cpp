/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "reshape_tool.h"

#include <limits>
#include <utility>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QCursor>
#include <QFlags>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QString>

#include "settings.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/ops/apply_op.h"
#include "core/path_coord.h"
#include "core/ops/reshape.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/draw_line_and_area_tool.h"
#include "tools/draw_path_tool.h"
#include "tools/tool.h"


namespace OpenOrienteering {

ReshapeTool::ReshapeTool(MapEditorController* editor, QAction* tool_action)
: MapEditorTool(editor, Other, tool_action)
{}

ReshapeTool::~ReshapeTool()
{
	delete path_tool;
}

void ReshapeTool::init()
{
	updateDirtyRect();
	updateStatusText();
	MapEditorTool::init();
}

const QCursor& ReshapeTool::getCursor() const
{
	static auto const cursor = scaledToScreen(QCursor{ QPixmap(QString::fromLatin1(":/images/cursor-draw-path.png")), 11, 11 });
	return cursor;
}

void ReshapeTool::finishEditing()
{
	Q_ASSERT(editingInProgress());
	if (path_tool)
		path_tool->finishEditing();
	MapEditorTool::finishEditing();
}

bool ReshapeTool::mousePressEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (path_tool)
		return path_tool->mousePressEvent(event, map_coord, widget);
	
	if (!(event->buttons() & Qt::LeftButton))
		return false;
	
	// The stroke must start on a line or area border.
	if (!findTarget(map_coord, snapDistance()))
		return false;
	
	path_tool = new DrawPathTool(editor, nullptr, true, false);
	connect(path_tool, &DrawLineAndAreaTool::dirtyRectChanged, this, &ReshapeTool::pathDirtyRectChanged);
	connect(path_tool, &DrawLineAndAreaTool::pathAborted, this, &ReshapeTool::pathAborted);
	connect(path_tool, &DrawLineAndAreaTool::pathFinished, this, &ReshapeTool::pathFinished);
	
	path_tool->init();
	path_tool->mousePressEvent(event, map_coord, widget);
	Q_ASSERT(path_tool->editingInProgress());
	setEditingInProgress(true);
	updateStatusText();
	return true;
}

bool ReshapeTool::mouseMoveEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (path_tool)
		return path_tool->mouseMoveEvent(event, map_coord, widget);
	return false;
}

bool ReshapeTool::mouseReleaseEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (path_tool)
		return path_tool->mouseReleaseEvent(event, map_coord, widget);
	return false;
}

bool ReshapeTool::mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (path_tool)
		return path_tool->mouseDoubleClickEvent(event, map_coord, widget);
	return false;
}

void ReshapeTool::leaveEvent(QEvent* event)
{
	if (path_tool)
		path_tool->leaveEvent(event);
}

bool ReshapeTool::keyPressEvent(QKeyEvent* event)
{
	if (path_tool)
		return path_tool->keyPressEvent(event);
	return false;
}

bool ReshapeTool::keyReleaseEvent(QKeyEvent* event)
{
	if (path_tool)
		return path_tool->keyReleaseEvent(event);
	return false;
}

void ReshapeTool::focusOutEvent(QFocusEvent* event)
{
	if (path_tool)
		path_tool->focusOutEvent(event);
}

void ReshapeTool::draw(QPainter* painter, MapWidget* widget)
{
	map()->drawSelection(painter, true, widget, nullptr);
	if (path_tool)
		path_tool->draw(painter, widget);
}

bool ReshapeTool::reshapeWith(const MapCoordVector& stroke, bool other_arc)
{
	// the design notes, "Reshape", editor paragraph.
	auto const snap_distance = snapDistance();
	auto* target = stroke.empty() ? nullptr : findTarget(MapCoordF(stroke.front()), snap_distance);
	QString error;
	if (!target)
	{
		error = tr("The stroke must start on a line or area border.");
	}
	else if (findTarget(MapCoordF(stroke.back()), snap_distance) != target)
	{
		error = tr("The stroke must end on the object it started on.");
	}
	else
	{
		ops::ReshapeParams params;
		params.stroke = stroke;
		params.snap_distance = snap_distance;
		params.other_arc = other_arc;
		auto result = ops::reshape(*map(), { target }, params);
		error = result.error;
		if (error.isEmpty())
		{
			ops::applyOp(*map(), std::move(result));
			return true;
		}
	}
	QMessageBox::warning(window(), tr("Reshape"), error);
	return false;
}

PathObject* ReshapeTool::findTarget(const MapCoordF& map_coord, double snap_distance) const
{
	SelectionInfoVector hits;
	map()->findObjectsAt(map_coord, snap_distance, true, false, false, false, hits);
	PathObject* nearest = nullptr;
	auto nearest_distance_sq = std::numeric_limits<double>::infinity();
	for (auto const& hit : hits)
	{
		if (hit.second->getType() != Object::Path)
			continue;
		auto* path = hit.second->asPath();
		if (map()->isObjectSelected(path))
			return path;
		auto const distance_sq = path->findClosestPointTo(map_coord).distance_squared;
		if (distance_sq < nearest_distance_sq)
		{
			nearest = path;
			nearest_distance_sq = distance_sq;
		}
	}
	return nearest;
}

double ReshapeTool::snapDistance() const
{
	auto const* view = editor->getMainWidget()->getMapView();
	return 0.001 * view->pixelToLength(Settings::getInstance().getMapEditorSnapDistancePx());
}

void ReshapeTool::updateDirtyRect(const QRectF* path_rect)
{
	QRectF rect;
	if (path_rect)
		rect = *path_rect;
	map()->includeSelectionRect(rect);
	
	if (rect.isValid())
		map()->setDrawingBoundingBox(rect, 6, true);
	else
		map()->clearDrawingBoundingBox();
}

void ReshapeTool::pathDirtyRectChanged(const QRectF& rect)
{
	updateDirtyRect(&rect);
}

void ReshapeTool::pathAborted()
{
	path_tool->deleteLater();
	path_tool = nullptr;
	setEditingInProgress(false);
	updateDirtyRect();
	updateStatusText();
}

void ReshapeTool::pathFinished(PathObject* stroke)
{
	auto const other_arc = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier);
	auto const coords = stroke->getRawCoordinateVector();
	// Don't signal selection change before editing was ended with pathAborted.
	pathAborted();
	reshapeWith(coords, other_arc);
}

void ReshapeTool::updateStatusText()
{
	if (!path_tool)
		setStatusBarText(tr("<b>Click or drag</b> on a line or area border: Start the new stretch. "));
}


}  // namespace OpenOrienteering
