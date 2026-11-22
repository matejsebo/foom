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


#include "merge_overlay.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include "core/map.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/virtual_path.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

QColor MergeOverlay::color()
{
	return QColor(255, 0, 255);
}

void MergeOverlay::draw(QPainter* painter, const Object& object, const QRectF& bounding_box, qreal scale)
{
	auto const* map = object.getMap();
	if (!map)
		return;
	object.update();
	
	painter->save();
	RenderConfig config = { *map, bounding_box, scale, RenderConfig::Screen | RenderConfig::HelperSymbols | RenderConfig::ForceMinSize, 1.0 };
	auto const& renderables = object.renderables();
	for (int i = -1; i < map->getNumColors(); ++i)
		renderables.draw(i, color(), painter, config);
	
	painter->setRenderHint(QPainter::Antialiasing, false);
	QPen pen(color(), 2.0);
	pen.setCosmetic(true);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	if (object.getType() == Object::Path)
	{
		for (auto const& part : object.asPath()->parts())
		{
			QPolygonF line;
			for (auto const& coord : part.path_coords)
				line.append(coord.pos);
			painter->drawPolyline(line);
		}
	}
	else
	{
		auto const extent = object.getExtent();
		if (extent.isValid())
			painter->drawRect(extent);
		else
			painter->drawEllipse(QPointF(object.getRawCoordinateVector().front()), 3.0 / scale, 3.0 / scale);
	}
	painter->restore();
}


MergeOverlayActivity::MergeOverlayActivity(Map& map)
: map(map)
{
	// nothing else
}

MergeOverlayActivity::~MergeOverlayActivity() = default;

void MergeOverlayActivity::setObject(const Object* new_object)
{
	object = new_object;
	if (object)
	{
		object->update();
		map.setActivityBoundingBox(object->getExtent(), 6, true);
	}
	else
	{
		map.clearActivityBoundingBox();
	}
}

void MergeOverlayActivity::draw(QPainter* painter, MapWidget* widget)
{
	if (!object)
		return;
	auto* view = widget->getMapView();
	painter->save();
	painter->translate(widget->width() / 2.0 + view->panOffset().x(), widget->height() / 2.0 + view->panOffset().y());
	painter->setWorldTransform(view->worldTransform(), true);
	MergeOverlay::draw(painter, *object, view->calculateViewedRect(widget->viewportToView(widget->rect())), view->calculateFinalZoomFactor());
	painter->restore();
}


}  // namespace OpenOrienteering
