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

#include "view_highlights.h"

#include <algorithm>

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include "core/map.h"
#include "core/map_view.h"
#include "core/unsymbolized_objects.h"
#include "core/objects/object.h"
#include "core/virtual_path.h"
#include "course/course_data.h"
#include "course/impassable_features.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

ViewHighlights::ViewHighlights(MapEditorController* editor, Map* map)
: QObject(editor)
, editor(editor)
, map(map)
{
	// The map widget repaints the overlay with the objects which change; the route settings come with the course data.
	connect(map, &Map::courseDataChanged, this, [this]() {
		if (impassable_shown || unsymbolized_shown)
			repaint();
	});
}

ViewHighlights::~ViewHighlights() = default;


int ViewHighlights::impassableObjectCount() const
{
	return impassable_shown ? int(findImpassableObjects(*map, map->courseData().route).size()) : 0;
}

int ViewHighlights::unsymbolizedObjectCount() const
{
	return unsymbolized_shown ? int(findUnsymbolizedObjects(*map).size()) : 0;
}


void ViewHighlights::setImpassableShown(bool shown)
{
	if (impassable_shown == shown)
		return;
	impassable_shown = shown;
	if (shown && editor->getWindow())
		editor->getWindow()->showStatusBarMessage(tr("%n impassable object(s) highlighted.", nullptr, impassableObjectCount()), 5000);
	repaint();
}

void ViewHighlights::setUnsymbolizedShown(bool shown)
{
	if (unsymbolized_shown == shown)
		return;
	unsymbolized_shown = shown;
	if (shown && editor->getWindow())
	{
		auto const count = unsymbolizedObjectCount();
		editor->getWindow()->showStatusBarMessage(count ? tr("%n object(s) without symbol.", nullptr, count)
		                                                : tr("There are no objects without symbol."), 5000);
	}
	repaint();
}


void ViewHighlights::repaint() const
{
	if (auto* widget = editor->getMainWidget())
		widget->update();
}


void ViewHighlights::draw(QPainter* painter, MapWidget* widget) const
{
	if (impassable_shown)
		drawObjects(painter, widget, findImpassableObjects(*map, map->courseData().route), QColor(230, 0, 0, 140));
	if (unsymbolized_shown)
		drawObjects(painter, widget, findUnsymbolizedObjects(*map), QColor(230, 0, 230, 160));
}


void ViewHighlights::drawObjects(QPainter* painter, MapWidget* widget, const std::vector<const Object*>& objects, const QColor& color)
{
	if (objects.empty())
		return;
	auto const width = std::max(3.0, widget->getMapView()->lengthToPixel(1000.0));
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter->setBrush(Qt::NoBrush);
	for (auto const* object : objects)
	{
		switch (object->getType())
		{
		case Object::Point:
			painter->drawEllipse(widget->mapToViewport(QPointF(object->asPoint()->getCoordF())), 2 * width, 2 * width);
			break;
		case Object::Path:
			for (auto const& part : object->asPath()->parts())
			{
				QPainterPath path;
				auto first = true;
				for (auto const& coord : part.path_coords)
				{
					auto const p = widget->mapToViewport(QPointF(coord.pos));
					if (first)
						path.moveTo(p);
					else
						path.lineTo(p);
					first = false;
				}
				painter->drawPath(path);
			}
			break;
		default:
			{
				object->update();
				auto const& extent = object->getExtent();
				if (extent.isValid())
					painter->drawRect(QRectF(widget->mapToViewport(extent.topLeft()), widget->mapToViewport(extent.bottomRight())).normalized());
			}
			break;
		}
	}
	painter->restore();
}


}  // namespace OpenOrienteering
