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


#include "sketch_tools.h"

#include <Qt>
#include <QBrush>
#include <QColor>
#include <QCursor>
#include <QInputDialog>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include "core/map.h"
#include "core/map_view.h"
#include "gui/map/map_widget.h"
#include "sketch/sketch.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

int pen_color = 0;
int pen_width = 1;

QCursor sketchCursor()
{
	return QCursor(QPixmap(QString::fromLatin1(":/images/cursor-hollow.png")), 1, 1);
}

}  // namespace


// ### SketchPenTool ###

SketchPenTool::SketchPenTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase { sketchCursor(), Other, editor, tool_action }
{
	// nothing else
}

SketchPenTool::~SketchPenTool() = default;

// static
void SketchPenTool::setPen(int color, int width)
{
	pen_color = qBound(0, color, sketch_color_count - 1);
	pen_width = qBound(0, width, sketch_width_count - 1);
}

// static
int SketchPenTool::penColor()
{
	return pen_color;
}

// static
int SketchPenTool::penWidth()
{
	return pen_width;
}

void SketchPenTool::initImpl()
{
	updateStatusText();
}

void SketchPenTool::updateStatusText()
{
	setStatusBarText(tr("<b>Drag</b>: Draw a stroke on the sketch layer. "));
}

void SketchPenTool::dragStart()
{
	points.clear();
	points.push_back(click_pos_map);
}

void SketchPenTool::dragMove()
{
	if (points.empty() || points.back() != cur_pos_map)
		points.push_back(cur_pos_map);
	updateDirtyRect();
}

void SketchPenTool::dragFinish()
{
	if (points.empty() || points.back() != cur_pos_map)
		points.push_back(cur_pos_map);
	if (points.size() >= 2)
		addSketchStroke(*map(), points, pen_color, pen_width);
	points.clear();
	updateDirtyRect();
}

void SketchPenTool::dragCanceled()
{
	points.clear();
	updateDirtyRect();
}

void SketchPenTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (points.size() < 2)
		return;
	QPolygonF line;
	line.reserve(int(points.size()));
	for (auto const& point : points)
		line.append(widget->mapToViewport(point));
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QPen(QBrush(sketchColorRgb(pen_color)), 2 * scaleFactor()));
	painter->drawPolyline(line);
}

int SketchPenTool::updateDirtyRectImpl(QRectF& rect)
{
	for (auto const& point : points)
		rectIncludeSafe(rect, point);
	return rect.isValid() ? 3 : -1;
}

void SketchPenTool::objectSelectionChangedImpl()
{
	// nothing
}


// ### SketchEraserTool ###

SketchEraserTool::SketchEraserTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase { sketchCursor(), Other, editor, tool_action }
{
	// nothing else
}

SketchEraserTool::~SketchEraserTool() = default;

void SketchEraserTool::initImpl()
{
	updateStatusText();
}

void SketchEraserTool::updateStatusText()
{
	setStatusBarText(tr("<b>Click</b> or <b>Drag</b>: Erase strokes and notes of the sketch layer. "));
}

qreal SketchEraserTool::radius() const
{
	return cur_map_widget ? 0.001 * cur_map_widget->getMapView()->pixelToLength(radius_pixels) : 1.0;
}

void SketchEraserTool::clickRelease()
{
	eraseSketch(*map(), click_pos_map, radius());
}

void SketchEraserTool::dragStart()
{
	trail.clear();
	trail.push_back(click_pos_map);
}

void SketchEraserTool::dragMove()
{
	if (trail.empty() || trail.back() != cur_pos_map)
		trail.push_back(cur_pos_map);
	updateDirtyRect();
}

void SketchEraserTool::dragFinish()
{
	if (trail.empty() || trail.back() != cur_pos_map)
		trail.push_back(cur_pos_map);
	eraseSketchAlong(*map(), trail, radius());
	trail.clear();
	updateDirtyRect();
}

void SketchEraserTool::dragCanceled()
{
	trail.clear();
	updateDirtyRect();
}

void SketchEraserTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (trail.empty())
		return;
	// The trail which will be erased on release.
	QPolygonF line;
	line.reserve(int(trail.size()));
	for (auto const& point : trail)
		line.append(widget->mapToViewport(point));
	QPen pen(QBrush(QColor(255, 255, 255, 160)), 2 * radius_pixels);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	painter->setBrush(Qt::NoBrush);
	painter->setPen(pen);
	painter->drawPolyline(line);
	painter->setPen(QPen(QBrush(active_color), scaleFactor()));
	painter->drawEllipse(line.last(), qreal(radius_pixels), qreal(radius_pixels));
}

int SketchEraserTool::updateDirtyRectImpl(QRectF& rect)
{
	for (auto const& point : trail)
		rectIncludeSafe(rect, point);
	return rect.isValid() ? radius_pixels + 2 : -1;
}

void SketchEraserTool::objectSelectionChangedImpl()
{
	// nothing
}


// ### SketchNoteTool ###

SketchNoteTool::SketchNoteTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase { sketchCursor(), Other, editor, tool_action }
{
	// nothing else
}

SketchNoteTool::~SketchNoteTool() = default;

void SketchNoteTool::initImpl()
{
	updateStatusText();
}

void SketchNoteTool::updateStatusText()
{
	setStatusBarText(tr("<b>Click</b>: Put a note on the sketch layer. "));
}

int SketchNoteTool::addNote(const MapCoordF& position, const QString& text)
{
	auto const trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return 0;
	return addSketchNote(*map(), position, trimmed, SketchPenTool::penColor());
}

void SketchNoteTool::clickRelease()
{
	auto const position = click_pos_map;
	auto ok = false;
	auto const text = QInputDialog::getText(window(), tr("Sketch note"), tr("Note:"), QLineEdit::Normal, {}, &ok);
	if (ok)
		addNote(position, text);
}

void SketchNoteTool::objectSelectionChangedImpl()
{
	// nothing
}


}  // namespace OpenOrienteering
