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

#include "selection_queries.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QtMath>
#include <QString>

#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

/// 0.001 mm in map units.
constexpr qint64 duplicate_tolerance = 1;

bool visible(const Object* object)
{
	auto const* symbol = object->getSymbol();
	return !symbol || !symbol->isHidden();
}

template <class Test>
std::vector<Object*> collect(const MapPart& part, Test test)
{
	std::vector<Object*> found;
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		// The map owns its objects; a query hands them out as the lasso query does.
		auto* object = const_cast<Object*>(part.getObject(i));
		if (visible(object) && test(object))
			found.push_back(object);
	}
	return found;
}

bool near(const MapCoord& a, const MapCoord& b)
{
	return std::abs(a.nativeX() - b.nativeX()) <= duplicate_tolerance && std::abs(a.nativeY() - b.nativeY()) <= duplicate_tolerance;
}

bool sameCoordinates(const MapCoordVector& a, const MapCoordVector& b, bool reversed)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t k = 0; k < a.size(); ++k)
	{
		if (!near(a[k], reversed ? b[b.size() - 1 - k] : b[k]))
			return false;
	}
	return true;
}

bool equalObjects(const Object* a, const Object* b)
{
	if (a->getType() != b->getType() || a->getSymbol() != b->getSymbol())
		return false;
	auto const& ca = a->getRawCoordinateVector();
	auto const& cb = b->getRawCoordinateVector();
	switch (a->getType())
	{
	case Object::Point:
		return !ca.empty() && !cb.empty() && near(ca.front(), cb.front());
	case Object::Path:
		return sameCoordinates(ca, cb, false) || sameCoordinates(ca, cb, true);
	case Object::Text:
		return static_cast<const TextObject*>(a)->getText() == static_cast<const TextObject*>(b)->getText()
		       && sameCoordinates(ca, cb, false);
	}
	return false;
}

double cross(const MapCoordF& o, const MapCoordF& a, const MapCoordF& b)
{
	return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool onSegment(const MapCoordF& p, const MapCoordF& a, const MapCoordF& b)
{
	return std::min(a.x(), b.x()) - 1e-9 <= p.x() && p.x() <= std::max(a.x(), b.x()) + 1e-9
	       && std::min(a.y(), b.y()) - 1e-9 <= p.y() && p.y() <= std::max(a.y(), b.y()) + 1e-9;
}

/// Whether the segments meet, touching included.
bool segmentsMeet(const MapCoordF& a, const MapCoordF& b, const MapCoordF& c, const MapCoordF& d)
{
	auto const d1 = cross(c, d, a);
	auto const d2 = cross(c, d, b);
	auto const d3 = cross(a, b, c);
	auto const d4 = cross(a, b, d);
	auto const eps = 1e-12;
	if (((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps)))
		return true;
	return (std::abs(d1) <= eps && onSegment(a, c, d)) || (std::abs(d2) <= eps && onSegment(b, c, d))
	       || (std::abs(d3) <= eps && onSegment(c, a, b)) || (std::abs(d4) <= eps && onSegment(d, a, b));
}

bool selfIntersects(const PathObject* path)
{
	path->update();
	struct Segment
	{
		MapCoordF a, b;
		std::size_t part, index, count;
		bool closed;
	};
	std::vector<Segment> segments;
	std::size_t part_index = 0;
	for (auto const& part : path->parts())
	{
		// The flattened outline without repeated points.
		std::vector<MapCoordF> points;
		for (auto const& coord : part.path_coords)
		{
			if (points.empty() || points.back() != coord.pos)
				points.push_back(coord.pos);
		}
		auto const closed = part.isClosed() && points.size() > 2 && points.front() == points.back();
		if (points.size() < 2)
		{
			++part_index;
			continue;
		}
		auto const count = points.size() - 1;
		for (std::size_t k = 0; k < count; ++k)
			segments.push_back({ points[k], points[k + 1], part_index, k, count, closed });
		++part_index;
	}
	for (std::size_t i = 0; i < segments.size(); ++i)
	{
		for (std::size_t j = i + 1; j < segments.size(); ++j)
		{
			auto const& s = segments[i];
			auto const& t = segments[j];
			if (s.part == t.part)
			{
				if (t.index == s.index + 1)
					continue;
				if (s.closed && s.index == 0 && t.index + 1 == s.count)
					continue;
			}
			if (segmentsMeet(s.a, s.b, t.a, t.b))
				return true;
		}
	}
	return false;
}

bool invalid(const Object* object)
{
	if (object->getType() == Object::Text)
		return static_cast<const TextObject*>(object)->getText().isEmpty();
	if (object->getType() != Object::Path)
		return false;
	auto const* path = object->asPath();
	auto const& coords = path->getRawCoordinateVector();
	if (coords.empty())
		return true;
	auto const all_equal = std::all_of(coords.begin(), coords.end(), [&coords](const MapCoord& c) { return c.isPositionEqualTo(coords.front()); });
	if (all_equal)
		return true;
	for (std::size_t k = 0; k < coords.size(); ++k)
	{
		if (coords[k].isCurveStart() && k + 3 >= coords.size())
			return true;
	}
	auto const area = object->getSymbol() && object->getSymbol()->getContainedTypes() & Symbol::Area;
	for (auto const& part : path->parts())
	{
		std::vector<MapCoord> distinct;
		for (auto k = part.first_index; k <= part.last_index && k < coords.size(); ++k)
		{
			if (std::none_of(distinct.begin(), distinct.end(), [&](const MapCoord& c) { return c.isPositionEqualTo(coords[k]); }))
				distinct.push_back(coords[k]);
		}
		if (distinct.size() < (area ? 3u : 2u))
			return true;
	}
	return false;
}

bool compare(double a, PropertyQuery::Compare op, double b, double tolerance)
{
	switch (op)
	{
	case PropertyQuery::Equal:        return std::abs(a - b) <= tolerance;
	case PropertyQuery::NotEqual:     return std::abs(a - b) > tolerance;
	case PropertyQuery::Less:         return a < b;
	case PropertyQuery::Greater:      return a > b;
	case PropertyQuery::LessEqual:    return a <= b;
	case PropertyQuery::GreaterEqual: return a >= b;
	}
	return false;
}

/// The property of the object; false when it has none.
bool property(const Object* object, PropertyQuery::Property which, double& value)
{
	switch (which)
	{
	case PropertyQuery::Vertices:
		value = double(object->getRawCoordinateVector().size());
		return true;
	case PropertyQuery::Length:
		if (object->getType() != Object::Path)
			return false;
		object->update();
		value = object->asPath()->getPaperLength();
		return true;
	case PropertyQuery::Area:
	{
		if (object->getType() != Object::Path || !object->getSymbol() || !(object->getSymbol()->getContainedTypes() & Symbol::Area))
			return false;
		object->update();
		auto const& parts = object->asPath()->parts();
		value = 0.0;
		for (std::size_t k = 0; k < parts.size(); ++k)
			value += (k == 0 ? 1.0 : -1.0) * std::abs(parts[k].calculateArea());
		return true;
	}
	case PropertyQuery::Rotation:
		if (object->getType() != Object::Point && object->getType() != Object::Text)
			return false;
		value = qRadiansToDegrees(object->getRotation());
		return true;
	case PropertyQuery::TextLength:
		if (object->getType() != Object::Text)
			return false;
		value = double(static_cast<const TextObject*>(object)->getText().length());
		return true;
	}
	return false;
}

}  // namespace



std::vector<Object*> findDuplicates(const MapPart& part)
{
	// the design notes, "Selection queries".
	std::vector<Object*> found;
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		auto* object = const_cast<Object*>(part.getObject(i));
		if (!visible(object))
			continue;
		for (int j = 0; j < i; ++j)
		{
			auto const* earlier = part.getObject(j);
			if (visible(earlier) && equalObjects(earlier, object))
			{
				found.push_back(object);
				break;
			}
		}
	}
	return found;
}


std::vector<Object*> findSelfIntersecting(const MapPart& part)
{
	return collect(part, [](const Object* object) {
		return object->getType() == Object::Path && selfIntersects(object->asPath());
	});
}


std::vector<Object*> findInvalid(const MapPart& part)
{
	return collect(part, [](const Object* object) { return invalid(object); });
}


std::vector<Object*> findByDate(const MapPart& part, DateKind kind, qint64 from, qint64 to)
{
	return collect(part, [kind, from, to](const Object* object) {
		auto const time = kind == DateKind::Created ? object->createdTime() : object->modifiedTime();
		return from <= time && time <= to;
	});
}


std::vector<Object*> findByProperty(const MapPart& part, const PropertyQuery& query)
{
	auto const real = query.property != PropertyQuery::Vertices && query.property != PropertyQuery::TextLength;
	return collect(part, [&query, real](const Object* object) {
		if (!query.symbols.empty() && std::find(query.symbols.begin(), query.symbols.end(), object->getSymbol()) == query.symbols.end())
			return false;
		double value = 0.0;
		return property(object, query.property, value) && compare(value, query.compare, query.value, real ? 0.005 : 0.0);
	});
}


}  // namespace OpenOrienteering
