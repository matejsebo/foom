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


#include "offset.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QPointF>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/path_coord.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * A vertex of the offset polyline: its position, and the input vertex whose
 * flags it carries.
 */
struct OffsetVertex
{
	QPointF pos;
	MapCoord source;
	bool has_flags;
};

using OffsetPolyline = std::vector<OffsetVertex>;

double cross(const QPointF& a, const QPointF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

double length(const QPointF& p)
{
	return std::hypot(p.x(), p.y());
}

/**
 * Returns the unit normal to the left of travel from a to b on paper.
 */
QPointF leftNormal(const QPointF& a, const QPointF& b)
{
	auto const d = b - a;
	auto const len = length(d);
	return QPointF(d.y() / len, -d.x() / len);
}

/**
 * Intersects the lines a0-a1 and b0-b1. u and v are the parameters on the
 * two lines, 0 at a0 respectively b0 and 1 at a1 respectively b1. Returns
 * false for parallel lines.
 */
bool intersectLines(const QPointF& a0, const QPointF& a1, const QPointF& b0, const QPointF& b1, double& u, double& v)
{
	auto const da = a1 - a0;
	auto const db = b1 - b0;
	auto const den = cross(da, db);
	if (std::abs(den) < 1e-12)
		return false;
	auto const w = b0 - a0;
	u = cross(w, db) / den;
	v = cross(w, da) / den;
	return true;
}

/**
 * Appends the vertices of the round join from `from` to `to` around the
 * centre, excluding the ends, at the given chord error.
 */
void appendArc(const QPointF& centre, const QPointF& from, const QPointF& to, double tolerance, OffsetPolyline& out)
{
	auto const radius = length(from - centre);
	if (radius <= tolerance)
		return;
	auto const a0 = std::atan2(from.y() - centre.y(), from.x() - centre.x());
	auto const a1 = std::atan2(to.y() - centre.y(), to.x() - centre.x());
	auto sweep = a1 - a0;
	while (sweep > M_PI)
		sweep -= 2 * M_PI;
	while (sweep <= -M_PI)
		sweep += 2 * M_PI;
	auto const step = 2 * std::acos(std::max(-1.0, 1.0 - tolerance / radius));
	auto const count = std::max(1, int(std::ceil(std::abs(sweep) / step)));
	for (int k = 1; k < count; ++k)
	{
		auto const angle = a0 + sweep * k / count;
		out.push_back({ centre + QPointF(radius * std::cos(angle), radius * std::sin(angle)), MapCoord{}, false });
	}
}

/**
 * Joins the offset segments prev (a_p to b_p) and next (a_n to b_n), which
 * meet at the input vertex corner, and appends the join vertices.
 */
void appendJoin(const QPointF& a_p, const QPointF& b_p, const QPointF& a_n, const QPointF& b_n, const MapCoord& corner, double tolerance, OffsetPolyline& out)
{
	auto const first = out.size();
	double u, v;
	if (!intersectLines(a_p, b_p, a_n, b_n, u, v))
	{
		// Parallel: continue straight.
		out.push_back({ b_p, corner, true });
		if (length(a_n - b_p) > tolerance)
			out.push_back({ a_n, MapCoord{}, false });
	}
	else if (u <= 1.0 + 1e-9)
	{
		// Inner side: trim both segments to their intersection.
		out.push_back({ a_p + (b_p - a_p) * u, corner, true });
	}
	else
	{
		// Outer side: a round join.
		out.push_back({ b_p, MapCoord{}, false });
		appendArc(QPointF(corner), b_p, a_n, tolerance, out);
		out.push_back({ a_n, MapCoord{}, false });
		auto& middle = out[first + (out.size() - first) / 2];
		middle.source = corner;
		middle.has_flags = true;
	}
}

/**
 * Returns true when the segments p0-p1 and q0-q1 cross each other properly;
 * x is the crossing.
 */
bool segmentsCross(const QPointF& p0, const QPointF& p1, const QPointF& q0, const QPointF& q1, QPointF& x)
{
	double u, v;
	if (!intersectLines(p0, p1, q0, q1, u, v))
		return false;
	auto const eps = 1e-9;
	if (u <= eps || u >= 1.0 - eps || v <= eps || v >= 1.0 - eps)
		return false;
	x = p0 + (p1 - p0) * u;
	return true;
}

/**
 * Cuts out every loop between two crossing segments of the polyline.
 */
void removeLoops(OffsetPolyline& polyline)
{
	for (std::size_t i = 0; i + 2 < polyline.size(); )
	{
		auto cut = false;
		for (std::size_t j = i + 2; j + 1 < polyline.size(); ++j)
		{
			QPointF x;
			if (segmentsCross(polyline[i].pos, polyline[i+1].pos, polyline[j].pos, polyline[j+1].pos, x))
			{
				polyline.erase(begin(polyline) + i + 1, begin(polyline) + j + 1);
				polyline.insert(begin(polyline) + i + 1, { x, MapCoord{}, false });
				cut = true;
				break;
			}
		}
		if (!cut)
			++i;
	}
}

/**
 * Returns the flattened vertices of the part without repeated positions,
 * and without the closing point of a closed part.
 */
MapCoordVector cleanVertices(const MapCoordVector& part_coords, bool closed, double tolerance)
{
	auto flat = flattenCoords(part_coords, tolerance);
	if (closed && flat.size() > 1)
		flat.pop_back();
	MapCoordVector out;
	out.reserve(flat.size());
	for (auto const& coord : flat)
	{
		if (!out.empty() && out.back().isPositionEqualTo(coord))
			continue;
		out.push_back(coord);
	}
	if (closed && out.size() > 1 && out.front().isPositionEqualTo(out.back()))
		out.pop_back();
	return out;
}

/**
 * Returns twice the signed area of the polygon, positive for clockwise on paper.
 */
double signedArea2(const MapCoordVector& vertices)
{
	auto sum = 0.0;
	auto const n = vertices.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		auto const& a = vertices[i];
		auto const& b = vertices[(i + 1) % n];
		sum += a.x() * b.y() - b.x() * a.y();
	}
	return sum;
}

/**
 * Offsets one part and returns its new coordinates, closed again if it was.
 */
MapCoordVector offsetPart(const MapCoordVector& part_coords, bool closed, double distance, double tolerance)
{
	auto const vertices = cleanVertices(part_coords, closed, tolerance);
	auto const n = vertices.size();
	if (n < 2)
		return part_coords;
	
	auto const segment_count = closed ? n : n - 1;
	std::vector<QPointF> a(segment_count), b(segment_count);
	for (std::size_t i = 0; i < segment_count; ++i)
	{
		auto const p = QPointF(vertices[i]);
		auto const q = QPointF(vertices[(i + 1) % n]);
		auto const shift = leftNormal(p, q) * distance;
		a[i] = p + shift;
		b[i] = q + shift;
	}
	
	OffsetPolyline polyline;
	polyline.reserve(n * 2);
	if (closed)
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			auto const prev = (i + segment_count - 1) % segment_count;
			appendJoin(a[prev], b[prev], a[i], b[i], vertices[i], tolerance, polyline);
		}
		polyline.push_back(polyline.front());
		removeLoops(polyline);
		polyline.pop_back();
	}
	else
	{
		polyline.push_back({ a[0], vertices[0], true });
		for (std::size_t i = 1; i + 1 < n; ++i)
			appendJoin(a[i-1], b[i-1], a[i], b[i], vertices[i], tolerance, polyline);
		polyline.push_back({ b[segment_count - 1], vertices[n - 1], true });
		removeLoops(polyline);
	}
	
	MapCoordVector out;
	out.reserve(polyline.size() + 1);
	for (auto const& vertex : polyline)
	{
		auto coord = MapCoord(MapCoordF(vertex.pos));
		if (vertex.has_flags)
		{
			coord.setFlags(vertex.source.flags());
			coord.setCurveStart(false);
			coord.setClosePoint(false);
			coord.setHolePoint(false);
		}
		if (!out.empty() && out.back().isPositionEqualTo(coord))
		{
			if (vertex.has_flags)
				out.back() = coord;
			continue;
		}
		out.push_back(coord);
	}
	if (closed)
	{
		if (out.size() > 1 && out.front().isPositionEqualTo(out.back()))
			out.pop_back();
		auto close = MapCoord(out.front().x(), out.front().y());
		close.setClosePoint(true);
		out.push_back(close);
	}
	out.back().setHolePoint(part_coords.back().isHolePoint());
	return out;
}

}  // namespace


OpResult offset(const Map& /*map*/, const std::vector<const Object*>& in, const OffsetParams& params)
{
	OpResult result;
	if (qFuzzyIsNull(params.distance))
	{
		result.error = QStringLiteral("The distance must not be zero.");
		return result;
	}
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path)
			continue;
		auto const* path = object->asPath();
		auto const& old_coords = path->getRawCoordinateVector();
		auto const& parts = path->parts();
		if (parts.empty())
			continue;
	
		auto const outer_closed = parts.front().isClosed();
		auto outer_orientation = 0.0;
		MapCoordVector coords;
		coords.reserve(old_coords.size() * 2);
		for (std::size_t part_index = 0; part_index < parts.size(); ++part_index)
		{
			auto const& part = parts[part_index];
			auto const part_coords = MapCoordVector(begin(old_coords) + part.first_index, begin(old_coords) + part.last_index + 1);
			auto distance = params.distance;
			if (outer_closed && part.isClosed())
			{
				auto const orientation = signedArea2(cleanVertices(part_coords, true, params.tolerance));
				if (part_index == 0)
					outer_orientation = orientation;
				else if (orientation * outer_orientation > 0)
					distance = -distance;  // a hole drawn the same way round grows when the boundary shrinks
			}
			auto const new_part = offsetPart(part_coords, part.isClosed(), distance, params.tolerance);
			coords.insert(end(coords), begin(new_part), end(new_part));
		}
	
		auto output = std::unique_ptr<PathObject>(new PathObject(path->getSymbol(), std::move(coords)));
		if (params.refit_curves && hasCurve(old_coords))
		{
			output->convertToCurves();
			output->simplify(nullptr, params.curve_threshold);
		}
		output->setTags(path->tags());
		output->setPatternRotation(path->getPatternRotation());
		output->setPatternOrigin(path->getPatternOrigin());
		output->setIdentity(path->uuid(), path->createdTime(), path->modifiedTime());
		output->setDerivedFrom(path->derivedFrom());
		result.out.push_back(std::move(output));
		if (!params.duplicate)
			result.consumed.push_back(object);
	}
	if (result.out.empty())
		result.error = QStringLiteral("Select a line or area object.");
	return result;
}

double signedDistance(const PathObject& path, const MapCoordF& position)
{
	path.update();
	auto const closest = path.findClosestPointTo(position);
	auto const part_index = path.findPartIndexForIndex(closest.path_coord.index);
	auto const& path_coords = path.parts()[part_index].path_coords;
	if (path_coords.size() < 2)
		return 0.0;
	auto next = std::upper_bound(begin(path_coords), end(path_coords), closest.path_coord.clen, [](PathCoord::length_type clen, const PathCoord& pc) { return clen < pc.clen; });
	if (next == end(path_coords))
		--next;
	if (next == begin(path_coords))
		++next;
	auto const normal = leftNormal(QPointF((next - 1)->pos), QPointF(next->pos));
	auto const offset = QPointF(position) - QPointF(closest.path_coord.pos);
	return offset.x() * normal.x() + offset.y() * normal.y();
}


}  // namespace ops

}  // namespace OpenOrienteering
