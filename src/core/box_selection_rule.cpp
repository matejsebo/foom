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

#include "box_selection_rule.h"

#include <cstddef>
#include <functional>

#include <QPointF>
#include <QRectF>
#include <QVariant>

#include "settings.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace {

/** Calls the test for every point of the object's geometry, and returns false at the first which fails it. */
bool allPoints(const Object& object, const std::function<bool (const QPointF&)>& test)
{
	switch (object.getType())
	{
	case Object::Point:
		return test(QPointF(object.asPoint()->getCoordF()));
	case Object::Path:
		for (auto const& part : object.asPath()->parts())
		{
			for (auto const& coord : part.path_coords)
			{
				if (!test(QPointF(coord.pos)))
					return false;
			}
		}
		return !object.asPath()->parts().empty();
	case Object::Text:
		{
			auto const& extent = object.getExtent();
			if (!extent.isValid())
				return false;
			for (auto const& corner : { extent.topLeft(), extent.topRight(), extent.bottomRight(), extent.bottomLeft() })
			{
				if (!test(corner))
					return false;
			}
			return true;
		}
	default:
		return false;
	}
}

bool inPolygon(const std::vector<MapCoordF>& polygon, const QPointF& p)
{
	auto inside = false;
	auto const n = polygon.size();
	for (std::size_t i = 0, j = n - 1; i < n; j = i++)
	{
		auto const& a = polygon[i];
		auto const& b = polygon[j];
		if ((a.y() > p.y()) != (b.y() > p.y()))
		{
			auto const x = a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
			if (p.x() < x)
				inside = !inside;
		}
	}
	return inside;
}

}  // namespace


bool objectEntirelyInBox(const Object& object, const QRectF& box)
{
	auto const rect = box.normalized();
	return allPoints(object, [&rect](const QPointF& p) {
		return p.x() >= rect.left() && p.x() <= rect.right() && p.y() >= rect.top() && p.y() <= rect.bottom();
	});
}

bool objectEntirelyInPolygon(const Object& object, const std::vector<MapCoordF>& polygon)
{
	if (polygon.size() < 3)
		return false;
	return allPoints(object, [&polygon](const QPointF& p) { return inPolygon(polygon, p); });
}

bool rectangleSelectsEntirelyInside()
{
	return Settings::getInstance().getSetting(Settings::MapEditor_RectangleSelectionInside).toBool();
}


}  // namespace OpenOrienteering
