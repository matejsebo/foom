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


#include "lasso_tool.h"

#include <cstddef>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QBrush>
#include <QCursor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include "core/lasso_query.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "gui/map/map_widget.h"
#include "gui/modifier_key.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

LassoTool::LassoTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase { QCursor(QPixmap(QString::fromLatin1(":/images/cursor-hollow.png")), 1, 1), Other, editor, tool_action }
{
	// nothing else
}

LassoTool::~LassoTool() = default;

void LassoTool::initImpl()
{
	updateStatusText();
}

void LassoTool::updateStatusText()
{
	setStatusBarText(tr("<b>Drag</b>: Select the objects inside the outline. ") +
	                 tr("<b>%1+Drag</b>: Toggle their selection. ").arg(ModifierKey::shift()));
}

bool LassoTool::selectPolygon(const std::vector<MapCoordF>& polygon, bool toggle)
{
	// the design notes, "Lasso selection".
	std::vector<Object*> objects;
	findObjectsInPolygon(*map()->getCurrentPart(), polygon, false, false, objects);
	
	auto selection_changed = false;
	if (!toggle)
	{
		selection_changed = map()->getNumSelectedObjects() > 0;
		map()->clearObjectSelection(false);
	}
	auto const size = objects.size();
	for (std::size_t i = 0; i < size; ++i)
	{
		if (toggle)
			map()->toggleObjectSelection(objects[i], i == size - 1);
		else
			map()->addObjectToSelection(objects[i], i == size - 1);
		selection_changed = true;
	}
	if (size == 0 && selection_changed)
		map()->emitSelectionChanged();
	return selection_changed;
}

void LassoTool::dragStart()
{
	outline.clear();
	outline.push_back(click_pos_map);
}

void LassoTool::dragMove()
{
	if (outline.empty() || outline.back() != cur_pos_map)
		outline.push_back(cur_pos_map);
	updateDirtyRect();
}

void LassoTool::dragFinish()
{
	if (outline.size() >= 3)
		selectPolygon(outline, active_modifiers & Qt::ShiftModifier);
	outline.clear();
	updateDirtyRect();
}

void LassoTool::dragCanceled()
{
	outline.clear();
	updateDirtyRect();
}

void LassoTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	map()->drawSelection(painter, true, widget, nullptr);
	if (outline.size() < 2)
		return;
	QPolygonF polygon;
	polygon.reserve(int(outline.size()));
	for (auto const& vertex : outline)
		polygon.append(widget->mapToViewport(vertex));
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QPen(QBrush(active_color), scaleFactor()));
	painter->drawPolygon(polygon);
}

int LassoTool::updateDirtyRectImpl(QRectF& rect)
{
	for (auto const& vertex : outline)
		rectIncludeSafe(rect, vertex);
	return 2;
}

void LassoTool::objectSelectionChangedImpl()
{
	updateDirtyRect();
}


}  // namespace OpenOrienteering
