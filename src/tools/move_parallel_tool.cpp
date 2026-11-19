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


#include "move_parallel_tool.h"

#include <limits>
#include <memory>
#include <utility>

#include <Qt>
#include <QtGlobal>
#include <QCursor>
#include <QLocale>
#include <QString>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/ops/offset.h"
#include "core/ops/op.h"
#include "tools/tool.h"


namespace OpenOrienteering {

MoveParallelTool::MoveParallelTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase { QCursor(Qt::SizeVerCursor), Other, editor, tool_action }
{
	// nothing else
}

MoveParallelTool::~MoveParallelTool() = default;

void MoveParallelTool::initImpl()
{
	objectSelectionChangedImpl();
}

void MoveParallelTool::updateStatusText()
{
	if (editingInProgress())
		setStatusBarText(tr("<b>Offset:</b> %1 mm").arg(QLocale().toString(distance, 'f', 2)));
	else
		setStatusBarText(tr("<b>Drag</b>: Move the selected lines and area borders parallel. "));
}

void MoveParallelTool::dragStart()
{
	// The selected path nearest the press is the reference for the distance.
	auto nearest_distance_sq = std::numeric_limits<double>::infinity();
	const PathObject* nearest = nullptr;
	for (auto const* object : map()->selectedObjects())
	{
		if (object->getType() != Object::Path)
			continue;
		auto const* path = object->asPath();
		path->update();
		auto const distance_sq = path->findClosestPointTo(click_pos_map).distance_squared;
		if (distance_sq < nearest_distance_sq)
		{
			nearest = path;
			nearest_distance_sq = distance_sq;
		}
	}
	if (!nearest)
	{
		cancelDragging();
		return;
	}
	reference.reset(static_cast<PathObject*>(nearest->duplicate()));
	start_distance = ops::signedDistance(*reference, click_pos_map);
	distance = 0.0;
	startEditing(map()->selectedObjects());
}

void MoveParallelTool::dragMove()
{
	if (!reference)
		return;
	resetEditedObjects();
	distance = ops::signedDistance(*reference, cur_pos_map) - start_distance;
	if (!qFuzzyIsNull(distance))
	{
		ops::OffsetParams params;
		params.distance = distance;
		for (auto* object : editedObjects())
		{
			auto result = ops::offset(*map(), { object }, params);
			if (result.error.isEmpty() && !result.out.empty())
				object->copyFrom(*result.out.front());
		}
	}
	updatePreviewObjects();
	updateStatusText();
}

void MoveParallelTool::dragFinish()
{
	if (reference)
	{
		finishEditing();
		reference.reset();
	}
	updateStatusText();
}

void MoveParallelTool::dragCanceled()
{
	if (reference)
	{
		abortEditing();
		reference.reset();
	}
	updateStatusText();
}

void MoveParallelTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	drawSelectionOrPreviewObjects(painter, widget);
}

void MoveParallelTool::objectSelectionChangedImpl()
{
	if (editingInProgress())
	{
		abortEditing();
		cancelDragging();
		reference.reset();
	}
	auto has_path = false;
	for (auto const* object : map()->selectedObjects())
		has_path = has_path || object->getType() == Object::Path;
	if (!has_path)
	{
		deactivate();
		return;
	}
	updateDirtyRect();
	updateStatusText();
}


}  // namespace OpenOrienteering
