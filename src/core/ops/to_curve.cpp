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


#include "to_curve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <QtMath>
#include <QPointF>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

double length(const QPointF& v)
{
	return std::hypot(v.x(), v.y());
}

QPointF unit(const QPointF& v)
{
	auto const len = length(v);
	return len > 0.0 ? v / len : QPointF();
}

QPointF pos(const MapCoord& coord)
{
	return { coord.x(), coord.y() };
}

struct Segment
{
	enum Kind { Straight, Curve };
	Kind kind = Straight;
	bool convert = false;     ///< a straight segment which becomes a curve
	QPointF handle1;          ///< for a curve: its control points
	QPointF handle2;
};

struct Part
{
	std::vector<MapCoord> vertices;  ///< without the close point
	std::vector<Segment> segments;   ///< segment k runs from vertex k to vertex k + 1 (wrapping when closed)
	bool closed = false;

	std::size_t count() const { return vertices.size(); }
	QPointF at(std::size_t k) const { return pos(vertices[k % count()]); }

	bool hasBefore(std::size_t v) const { return closed || v > 0; }
	bool hasAfter(std::size_t v) const { return closed || v + 1 < count(); }
	std::size_t before(std::size_t v) const { return (v + segments.size() - 1) % segments.size(); }

	/** The direction in which the segment leaves its start vertex. */
	QPointF outgoing(std::size_t k) const
	{
		auto const& s = segments[k];
		auto const start = at(k);
		auto const end = at(k + 1);
		if (s.kind == Segment::Curve)
		{
			for (auto const& next : { s.handle1, s.handle2, end })
			{
				if (length(next - start) > 1e-9)
					return unit(next - start);
			}
		}
		return unit(end - start);
	}

	/** The direction in which the segment arrives at its end vertex. */
	QPointF incoming(std::size_t k) const
	{
		auto const& s = segments[k];
		auto const start = at(k);
		auto const end = at(k + 1);
		if (s.kind == Segment::Curve)
		{
			for (auto const& previous : { s.handle2, s.handle1, start })
			{
				if (length(end - previous) > 1e-9)
					return unit(end - previous);
			}
		}
		return unit(end - start);
	}

	/** The turn of the direction at a vertex, in degrees; 0 at an end of an open part. */
	double turn(std::size_t v) const
	{
		if (!hasBefore(v) || !hasAfter(v))
			return 0.0;
		auto const a = incoming(before(v));
		auto const b = outgoing(v % segments.size());
		auto const cosine = std::max(-1.0, std::min(1.0, QPointF::dotProduct(a, b)));
		return qRadiansToDegrees(std::acos(cosine));
	}
};

/** Splits the coordinates of one part into vertices and segments; consecutive vertices at one place are joined. */
Part readPart(const MapCoordVector& coords, std::size_t first, std::size_t last, bool closed)
{
	Part part;
	part.closed = closed;
	auto i = first;
	while (i <= last)
	{
		auto vertex = coords[i];
		auto const is_curve = vertex.isCurveStart() && i + 3 <= last;
		if (!part.vertices.empty() && !part.segments.empty() && part.segments.back().kind == Segment::Straight
		    && length(pos(vertex) - pos(part.vertices.back())) < 1e-9)
		{
			// The same place again: one vertex, with the dash flag of either.
			part.segments.pop_back();
			if (part.vertices.back().isDashPoint())
				vertex.setDashPoint(true);
			part.vertices.pop_back();
		}
		vertex.setCurveStart(false);
		vertex.setClosePoint(false);
		vertex.setHolePoint(false);
		part.vertices.push_back(vertex);
		if (i == last)
			break;
		Segment segment;
		if (is_curve)
		{
			segment.kind = Segment::Curve;
			segment.handle1 = pos(coords[i + 1]);
			segment.handle2 = pos(coords[i + 2]);
			i += 3;
		}
		else
		{
			i += 1;
		}
		part.segments.push_back(segment);
	}
	if (closed && part.vertices.size() > 1)
	{
		// The close point is the first vertex again.
		auto const dash = part.vertices.back().isDashPoint();
		part.vertices.pop_back();
		if (dash)
			part.vertices.front().setDashPoint(true);
		if (!part.segments.empty() && part.segments.back().kind == Segment::Straight
		    && length(part.at(part.vertices.size() - 1) - part.at(0)) < 1e-9 && part.vertices.size() > 1)
		{
			// A last segment of no length before the close point.
			part.segments.pop_back();
			part.vertices.pop_back();
		}
	}
	if (!closed && part.segments.size() + 1 != part.vertices.size())
		part.segments.resize(part.vertices.size() > 0 ? part.vertices.size() - 1 : 0);
	return part;
}

/** Marks the straight segments which become curves. */
bool chooseSegments(Part& part, const ToCurveParams& params)
{
	std::vector<double> lengths;
	for (std::size_t k = 0; k < part.segments.size(); ++k)
	{
		if (part.segments[k].kind == Segment::Straight)
			lengths.push_back(length(part.at(k + 1) - part.at(k)));
	}
	if (lengths.empty())
		return false;
	auto limit = -1.0;
	if (params.keep_straight_parts)
	{
		auto sorted = lengths;
		std::sort(sorted.begin(), sorted.end());
		auto const n = sorted.size();
		auto const median = n % 2 ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
		limit = 4.0 * median;
	}
	auto any = false;
	for (std::size_t k = 0; k < part.segments.size(); ++k)
	{
		auto& segment = part.segments[k];
		if (segment.kind != Segment::Straight)
			continue;
		auto const len = length(part.at(k + 1) - part.at(k));
		segment.convert = len > 1e-9 && !(limit >= 0.0 && len > limit);
		any = any || segment.convert;
	}
	return any;
}

/** Reflects a unit direction in the line of the unit chord. */
QPointF reflect(const QPointF& direction, const QPointF& chord)
{
	return 2.0 * QPointF::dotProduct(chord, direction) * chord - direction;
}

/** The two handles of the converted segment k, as offsets from its ends (the second points backwards from the end). */
std::pair<QPointF, QPointF> handlesOf(const Part& part, std::size_t k, const ToCurveParams& params)
{
	auto const n = part.count();
	auto const a = k;
	auto const b = (k + 1) % n;
	auto const p1 = part.at(a);
	auto const p2 = part.at(b);
	auto const chord = p2 - p1;
	auto const chord_length = length(chord);
	auto const factor = 1.0 + params.tangent_factor;
	auto const third = chord_length / 3.0 * factor;
	auto const corner = [&part, &params](std::size_t v) {
		return params.keep_corners && part.turn(v) > params.corner_angle_deg;
	};
	auto const root = [](const QPointF& from, const QPointF& to) { return std::sqrt(length(to - from)); };

	// The side at the start; `known` is false where the end rule decides.
	auto const startSide = [&](bool& known) -> QPointF {
		known = true;
		if (corner(a))
			return {};
		if (!part.hasBefore(a))
		{
			known = false;
			return {};
		}
		auto const previous = part.before(a);
		if (part.segments[previous].convert)
		{
			auto const p0 = part.at(previous);
			auto const d01 = root(p0, p1);
			auto const d12 = root(p1, p2);
			return (chord + d12 * ((p1 - p0) / d01 - (p2 - p0) / (d01 + d12))) / 3.0 * factor;
		}
		return part.incoming(previous) * third;
	};
	auto const endSide = [&](bool& known) -> QPointF {
		known = true;
		if (corner(b))
			return {};
		if (!part.hasAfter(b))
		{
			known = false;
			return {};
		}
		auto const next = b % part.segments.size();
		if (part.segments[next].convert)
		{
			auto const p3 = part.at(next + 1);
			auto const d12 = root(p1, p2);
			auto const d23 = root(p2, p3);
			return (chord + d12 * ((p3 - p2) / d23 - (p3 - p1) / (d12 + d23))) / 3.0 * factor;
		}
		return part.outgoing(next) * third;
	};

	auto start_known = false;
	auto end_known = false;
	auto h1 = startSide(start_known);
	auto h2 = endSide(end_known);
	auto const along = unit(chord) * third;
	auto const freeEnd = [&](const QPointF& other) {
		// The tangent at the other end, reflected in the chord.
		if (params.keep_end_angles || length(other) < 1e-12)
			return along;
		return reflect(unit(other), unit(chord)) * third;
	};
	if (!start_known && !end_known)
	{
		h1 = along;
		h2 = along;
	}
	else if (!start_known)
	{
		h1 = freeEnd(h2);
	}
	else if (!end_known)
	{
		h2 = freeEnd(h1);
	}
	return { h1, h2 };
}

/** Appends the converted part to the output. */
void writePart(const Part& part, const ToCurveParams& params, PathObject& output, bool new_part)
{
	auto first = true;
	auto const add = [&output, &first, new_part](const MapCoord& coord) {
		output.addCoordinate(coord, first && new_part);
		first = false;
	};
	for (std::size_t k = 0; k < part.segments.size(); ++k)
	{
		auto const& segment = part.segments[k];
		auto start = part.vertices[k];
		if (segment.kind == Segment::Curve || segment.convert)
		{
			start.setCurveStart(true);
			add(start);
			QPointF c1 = segment.handle1;
			QPointF c2 = segment.handle2;
			if (segment.convert)
			{
				auto const handles = handlesOf(part, k, params);
				c1 = part.at(k) + handles.first;
				c2 = part.at(k + 1) - handles.second;
			}
			add(MapCoord(c1.x(), c1.y()));
			add(MapCoord(c2.x(), c2.y()));
		}
		else
		{
			add(start);
		}
	}
	if (!part.closed)
		add(part.vertices.back());
	else
		output.parts().back().setClosed(true);
}

}  // namespace


OpResult toCurve(const Map& /*map*/, const std::vector<const Object*>& in, const ToCurveParams& params)
{
	OpResult result;
	auto found = false;
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path || !object->getSymbol())
			continue;
		if (!(object->getSymbol()->getContainedTypes() & (Symbol::Line | Symbol::Area)))
			continue;
		found = true;
		auto const* path = object->asPath();
		auto const& coords = path->getRawCoordinateVector();

		std::vector<Part> parts;
		auto changed = false;
		for (auto const& path_part : path->parts())
		{
			auto part = readPart(coords, path_part.first_index, path_part.last_index, path_part.isClosed());
			if (part.vertices.size() >= 2 && !part.segments.empty())
				changed = chooseSegments(part, params) || changed;
			parts.push_back(std::move(part));
		}
		if (!changed)
			continue;

		auto output = std::unique_ptr<PathObject>(new PathObject(path->getSymbol()));
		auto first_part = true;
		for (auto const& part : parts)
		{
			if (part.vertices.size() < 2 || part.segments.empty())
				continue;
			writePart(part, params, *output, !first_part);
			first_part = false;
		}
		if (output->getCoordinateCount() < 2)
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
