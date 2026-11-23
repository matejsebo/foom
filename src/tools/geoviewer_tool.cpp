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

#include "geoviewer_tool.h"

#include <Qt>
#include <QColor>
#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include "core/geoviewer.h"
#include "core/map.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

GeoviewerTool::GeoviewerTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
{
	useTouchCursor(false);
}

GeoviewerTool::~GeoviewerTool() = default;


void GeoviewerTool::updateStatusText()
{
	setStatusBarText(tr("<b>Click</b>: Open the geoviewer here, looking north. "
	                    "<b>Drag</b>: Open it at the start, looking towards the end. "));
}

void GeoviewerTool::objectSelectionChangedImpl()
{
	// NOP
}

void GeoviewerTool::clickRelease()
{
	open(click_pos_map, click_pos_map);
}

void GeoviewerTool::dragStart()
{
	start = click_pos_map;
	current = cur_pos_map;
	pointing = true;
	updateDirtyRect();
}

void GeoviewerTool::dragMove()
{
	current = cur_pos_map;
	updateDirtyRect();
}

void GeoviewerTool::dragFinish()
{
	pointing = false;
	updateDirtyRect();
	open(start, cur_pos_map);
}

void GeoviewerTool::dragCanceled()
{
	pointing = false;
	updateDirtyRect();
}

bool GeoviewerTool::keyPress(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape)
	{
		deactivate();
		return true;
	}
	return false;
}


void GeoviewerTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (!pointing)
		return;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(QColor(220, 0, 0), 2.0 * scaleFactor()));
	painter->drawLine(widget->mapToViewport(start), widget->mapToViewport(current));
	painter->restore();
}

int GeoviewerTool::updateDirtyRectImpl(QRectF& rect)
{
	if (!pointing)
		return -1;
	rectIncludeSafe(rect, QPointF(start));
	rectIncludeSafe(rect, QPointF(current));
	return int(4 * scaleFactor());
}


void GeoviewerTool::open(const MapCoordF& from, const MapCoordF& to)
{
	GeoviewerTarget target;
	if (!geoviewerTarget(map()->getGeoreferencing(), from, to, target))
	{
		setStatusBarText(tr("The map is not georeferenced: the geoviewer needs latitude and longitude."));
		return;
	}
	emit urlChosen(geoviewerUrl(geoviewerPattern(), target));
}


}  // namespace OpenOrienteering
