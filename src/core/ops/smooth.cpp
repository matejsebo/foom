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


#include "smooth.h"

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
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

constexpr double flatten_tolerance = 0.05;
/** Upstream's BEZIER_HANDLE_DISTANCE (src/util/util.h): kappa over the square root of two. */
constexpr double handle_distance = 0.390524291729;

struct Vertex
{
	QPointF pos;
	bool dash = false;
	bool kept = false;
	bool sharp = false;
};

double cross(const QPointF& a, const QPointF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

double dot(const QPointF& a, const QPointF& b)
{
	return a.x() * b.x() + a.y() * b.y();
}

double length(const QPointF& v)
{
	return std::hypot(v.x(), v.y());
}

double pointLineDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
	auto const ab = b - a;
	auto const len = length(ab);
	if (!(len > 0.0))
		return length(p - a);
	return std::abs(cross(ab, p - a)) / len;
}

/** Douglas-Peucker over the vertices first..last (inclusive); marks the survivors. */
void douglasPeucker(const std::vector<QPointF>& points, std::size_t first, std::size_t last, double tolerance, std::vector<bool>& survive)
{
	if (last <= first + 1)
		return;
	auto worst = first;
	auto worst_distance = -1.0;
	for (auto i = first + 1; i < last; ++i)
	{
		auto const d = pointLineDistance(points[i], points[first], points[last]);
		if (d > worst_distance)
		{
			worst_distance = d;
			worst = i;
		}
	}
	if (worst_distance > tolerance)
	{
		survive[worst] = true;
		douglasPeucker(points, first, worst, tolerance, survive);
		douglasPeucker(points, worst, last, tolerance, survive);
	}
}

/** The turn (signed, degrees) at vertex i of the polyline, 0 at ends of an open one. */
double turnAt(const std::vector<Vertex>& vertices, std::size_t i, bool closed)
{
	auto const n = vertices.size();
	if (!closed && (i == 0 || i + 1 >= n))
		return 0.0;
	auto const& previous = vertices[(i + n - 1) % n].pos;
	auto const& next = vertices[(i + 1) % n].pos;
	auto const incoming = vertices[i].pos - previous;
	auto const outgoing = next - vertices[i].pos;
	if (!(length(incoming) > 0.0) || !(length(outgoing) > 0.0))
		return 0.0;
	return qRadiansToDegrees(std::atan2(cross(incoming, outgoing), dot(incoming, outgoing)));
}

/**
 * Smooths one part. Returns the new coordinates (without a close point;
 * the caller closes the part), or an empty vector when nothing changes
 * is not decidable here, so the result is always returned.
 */
std::vector<MapCoord> smoothPart(const MapCoordVector& flat, std::size_t first, std::size_t last, bool closed, const SmoothParams& params)
{
	std::vector<Vertex> vertices;
	auto const end = closed ? last : last + 1;  // the close point is left out
	for (auto i = first; i < end; ++i)
	{
		Vertex v;
		v.pos = QPointF(flat[i].x(), flat[i].y());
		v.dash = flat[i].isDashPoint();
		if (!vertices.empty() && length(v.pos - vertices.back().pos) < 1e-9)
			continue;
		vertices.push_back(v);
	}
	auto const n = vertices.size();
	if (n < 2)
		return {};
	
	// Kept vertices.
	auto last_turn_sign = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		auto& v = vertices[i];
		if (!closed && (i == 0 || i + 1 == n))
		{
			v.kept = true;
			v.sharp = true;
		}
		if (v.dash)
			v.kept = true;
		auto const turn = turnAt(vertices, i, closed);
		if (params.keep_corners && std::abs(turn) > params.corner_angle_deg)
		{
			v.kept = true;
			v.sharp = true;
		}
		auto const sign = turn > 1e-9 ? 1 : (turn < -1e-9 ? -1 : 0);
		if (params.keep_inflections && sign != 0 && last_turn_sign != 0 && sign != last_turn_sign)
			v.kept = true;
		if (sign != 0)
			last_turn_sign = sign;
	}
	auto any_kept = false;
	for (auto const& v : vertices)
		any_kept = any_kept || v.kept;
	if (!any_kept)
		vertices.front().kept = true;
	
	// Douglas-Peucker on each run between kept vertices. A closed part is
	// walked round from its first kept vertex; indices beyond n wrap.
	std::vector<QPointF> points;
	points.reserve(n);
	for (auto const& v : vertices)
		points.push_back(v.pos);
	std::size_t first_kept = 0;
	while (first_kept < n && !vertices[first_kept].kept)
		++first_kept;
	std::vector<std::size_t> kept_indices;
	if (closed)
	{
		for (std::size_t step = 0; step < n; ++step)
		{
			if (vertices[(first_kept + step) % n].kept)
				kept_indices.push_back(first_kept + step);
		}
		kept_indices.push_back(first_kept + n);
	}
	else
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			if (vertices[i].kept)
				kept_indices.push_back(i);
		}
	}
	std::vector<bool> survive(n, false);
	for (std::size_t r = 0; r + 1 < kept_indices.size(); ++r)
	{
		auto const ka = kept_indices[r];
		auto const kb = kept_indices[r + 1];
		std::vector<QPointF> run;
		for (auto k = ka; k <= kb; ++k)
			run.push_back(points[k % n]);
		std::vector<bool> run_survive(run.size(), false);
		douglasPeucker(run, 0, run.size() - 1, params.tolerance, run_survive);
		for (std::size_t k = 0; k < run.size(); ++k)
		{
			if (run_survive[k])
				survive[(ka + k) % n] = true;
		}
		survive[ka % n] = true;
		survive[kb % n] = true;
	}
	
	std::vector<Vertex> reduced;
	for (std::size_t step = 0; step < n; ++step)
	{
		auto const i = closed ? (first_kept + step) % n : step;
		if (survive[i])
			reduced.push_back(vertices[i]);
	}
	auto const m = reduced.size();
	if (m < 2)
		return {};
	
	// Tangents and handles.
	auto const tangent = [&reduced, m, closed](std::size_t i, bool outgoing) {
		auto const& v = reduced[i];
		QPointF direction;
		auto const has_previous = closed || i > 0;
		auto const has_next = closed || i + 1 < m;
		if (v.sharp || !has_previous || !has_next)
		{
			if (outgoing && has_next)
				direction = reduced[(i + 1) % m].pos - v.pos;
			else if (!outgoing && has_previous)
				direction = v.pos - reduced[(i + m - 1) % m].pos;
			else
				direction = has_next ? reduced[(i + 1) % m].pos - v.pos : v.pos - reduced[(i + m - 1) % m].pos;
		}
		else
		{
			direction = reduced[(i + 1) % m].pos - reduced[(i + m - 1) % m].pos;
		}
		auto const len = length(direction);
		return len > 0.0 ? direction / len : QPointF(1.0, 0.0);
	};
	std::vector<MapCoord> out;
	auto const segments = closed ? m : m - 1;
	for (std::size_t i = 0; i < segments; ++i)
	{
		auto const& a = reduced[i];
		auto const& b = reduced[(i + 1) % m];
		MapCoord start(a.pos.x(), a.pos.y());
		start.setDashPoint(a.dash);
		auto const straight = a.sharp && b.sharp;
		if (!straight)
		{
			auto const len = length(b.pos - a.pos);
			auto const h1 = a.pos + tangent(i, true) * (handle_distance * len);
			auto const h2 = b.pos - tangent((i + 1) % m, false) * (handle_distance * len);
			start.setCurveStart(true);
			out.push_back(start);
			out.emplace_back(h1.x(), h1.y());
			out.emplace_back(h2.x(), h2.y());
		}
		else
		{
			out.push_back(start);
		}
	}
	if (!closed)
	{
		MapCoord end(reduced.back().pos.x(), reduced.back().pos.y());
		end.setDashPoint(reduced.back().dash);
		out.push_back(end);
	}
	return out;
}

bool sameGeometry(const MapCoordVector& a, const MapCoordVector& b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (!a[i].isPositionEqualTo(b[i]) || a[i].isCurveStart() != b[i].isCurveStart() || a[i].isDashPoint() != b[i].isDashPoint())
			return false;
	}
	return true;
}

}  // namespace


OpResult smoothPaths(const Map& /*map*/, const std::vector<const Object*>& in, const SmoothParams& params)
{
	OpResult result;
	if (!(params.tolerance > 0.0))
	{
		result.error = QStringLiteral("The tolerance must be positive.");
		return result;
	}
	auto found = false;
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path || !object->getSymbol())
			continue;
		if (!(object->getSymbol()->getContainedTypes() & (Symbol::Line | Symbol::Area)))
			continue;
		found = true;
		auto const* path = object->asPath();
		auto const flat = flattenCoords(path->getRawCoordinateVector(), flatten_tolerance);
		auto output = std::unique_ptr<PathObject>(new PathObject(path->getSymbol()));
		auto first_part = true;
		std::size_t start = 0;
		for (std::size_t i = 0; i < flat.size(); ++i)
		{
			if (!flat[i].isHolePoint() && i + 1 < flat.size())
				continue;
			auto const closed = flat[i].isClosePoint();
			auto const coords = smoothPart(flat, start, i, closed, params);
			start = i + 1;
			if (coords.empty())
				continue;
			auto first_coord = true;
			for (auto const& coord : coords)
			{
				output->addCoordinate(coord, first_coord && !first_part);
				first_coord = false;
			}
			if (closed)
				output->parts().back().setClosed(true);
			first_part = false;
		}
		if (output->getCoordinateCount() < 2 || sameGeometry(output->getRawCoordinateVector(), path->getRawCoordinateVector()))
			continue;
		output->setTags(path->tags());
		output->setPatternRotation(path->getPatternRotation());
		output->setPatternOrigin(path->getPatternOrigin());
		output->setIdentity(path->uuid(), path->createdTime(), path->modifiedTime());
		output->setDerivedFrom(path->derivedFrom());
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	if (!found)
		result.error = QStringLiteral("Select line or area objects.");
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
