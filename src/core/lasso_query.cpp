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


#include "lasso_query.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "core/box_selection_rule.h"  // foom
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace {

using Polygon = std::vector<MapCoordF>;

/**
 * Returns true when the point lies inside the polygon, by the even-odd rule.
 */
bool contains(const Polygon& polygon, const QPointF& p)
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

double cross(const QPointF& a, const QPointF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

/**
 * Returns true when the segments p0-p1 and q0-q1 meet; parallel segments never do.
 */
bool segmentsMeet(const QPointF& p0, const QPointF& p1, const QPointF& q0, const QPointF& q1)
{
	auto const dp = p1 - p0;
	auto const dq = q1 - q0;
	auto const den = cross(dp, dq);
	if (std::abs(den) < 1e-12)
		return false;
	auto const w = q0 - p0;
	auto const u = cross(w, dq) / den;
	auto const v = cross(w, dp) / den;
	return u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0;
}

/**
 * Returns true when the segment a-b crosses an edge of the polygon.
 */
bool crossesEdge(const Polygon& polygon, const QPointF& a, const QPointF& b)
{
	auto const n = polygon.size();
	for (std::size_t i = 0, j = n - 1; i < n; j = i++)
	{
		if (segmentsMeet(a, b, QPointF(polygon[j]), QPointF(polygon[i])))
			return true;
	}
	return false;
}

bool selectsPath(const Polygon& polygon, const PathObject& path)
{
	for (auto const& part : path.parts())
	{
		auto const& coords = part.path_coords;
		for (std::size_t i = 0; i < coords.size(); ++i)
		{
			auto const p = QPointF(coords[i].pos);
			if (contains(polygon, p))
				return true;
			if (i > 0 && crossesEdge(polygon, QPointF(coords[i-1].pos), p))
				return true;
		}
	}
	if (path.getSymbol()->getContainedTypes() & Symbol::Area)
		return path.isPointInsideArea(polygon.front());
	return false;
}

bool selectsRect(const Polygon& polygon, const QRectF& rect)
{
	QPointF const corners[] = { rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft() };
	for (std::size_t i = 0; i < 4; ++i)
	{
		if (contains(polygon, corners[i]) || crossesEdge(polygon, corners[i], corners[(i + 1) % 4]))
			return true;
	}
	return rect.contains(QPointF(polygon.front()));
}

}  // namespace


void findObjectsInPolygon(
        const MapPart& part,
        const std::vector<MapCoordF>& polygon,
        bool include_hidden_objects,
        bool include_protected_objects,
        std::vector<Object*>& out,
        bool entirely )
{
	if (polygon.size() < 3)
		return;
	auto left = polygon.front().x(), top = polygon.front().y(), right = left, bottom = top;
	for (auto const& vertex : polygon)
	{
		left = std::min(left, vertex.x());
		right = std::max(right, vertex.x());
		top = std::min(top, vertex.y());
		bottom = std::max(bottom, vertex.y());
	}
	auto const bounds = QRectF(QPointF(left, top), QPointF(right, bottom));
	
	std::vector<Object*> candidates;
	part.query(bounds, candidates);
	for (auto* object : candidates)
	{
		if (!include_hidden_objects && object->getSymbol()->isHidden())
			continue;
		if (!include_protected_objects && object->getSymbol()->isProtected())
			continue;
		object->update();
		auto const& extent = object->getExtent();
		if (object->getType() != Object::Point && extent.isValid() && !bounds.intersects(extent))
			continue;  // a point symbol which draws nothing has no extent, and is tested by its coordinate
		auto selected = false;
		if (entirely)  // foom
		{
			if (objectEntirelyInPolygon(*object, polygon))
				out.push_back(object);
			continue;
		}
		switch (object->getType())
		{
		case Object::Point:
			selected = contains(polygon, QPointF(object->asPoint()->getCoord()));
			break;
		case Object::Path:
			selected = selectsPath(polygon, *object->asPath());
			break;
		case Object::Text:
			selected = selectsRect(polygon, object->getExtent());
			break;
		default:
			break;
		}
		if (selected)
			out.push_back(object);
	}
}


}  // namespace OpenOrienteering
