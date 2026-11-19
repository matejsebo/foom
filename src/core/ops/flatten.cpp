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


#include "flatten.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"
#include "core/objects/object.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * Appends the end points of straight segments approximating a cubic Bezier
 * curve to out, except the end point of the curve itself.
 * 
 * The curve is flat enough when both inner control points lie within the
 * tolerance of the chord: by the convex hull property, the curve then lies
 * within the tolerance of the chord, too. (PathCoordVector::curveToPathCoord
 * compares the lengths of control polygon and chord instead, which bounds
 * the deviation only together with its maximum segment length.)
 */
void flattenCurve(const MapCoordF& c0, const MapCoordF& c1, const MapCoordF& c2, const MapCoordF& c3, double tolerance, int depth, std::vector<MapCoord>& out)
{
	auto const distance_to_chord = [&c0, &c3](const MapCoordF& point) {
		auto const dx = c3.x() - c0.x();
		auto const dy = c3.y() - c0.y();
		auto const length_sq = dx*dx + dy*dy;
		if (length_sq <= 0.0)
			return point.distanceTo(c0);
		auto const t = qBound(0.0, ((point.x() - c0.x()) * dx + (point.y() - c0.y()) * dy) / length_sq, 1.0);
		return point.distanceTo(MapCoordF(c0.x() + t * dx, c0.y() + t * dy));
	};
	if (depth <= 0 || (distance_to_chord(c1) <= tolerance && distance_to_chord(c2) <= tolerance))
	{
		out.emplace_back(c3);
		return;
	}
	
	// Split in two
	MapCoordF c01((c0.x() + c1.x()) * 0.5, (c0.y() + c1.y()) * 0.5);
	MapCoordF c12((c1.x() + c2.x()) * 0.5, (c1.y() + c2.y()) * 0.5);
	MapCoordF c23((c2.x() + c3.x()) * 0.5, (c2.y() + c3.y()) * 0.5);
	MapCoordF c012((c01.x() + c12.x()) * 0.5, (c01.y() + c12.y()) * 0.5);
	MapCoordF c123((c12.x() + c23.x()) * 0.5, (c12.y() + c23.y()) * 0.5);
	MapCoordF c0123((c012.x() + c123.x()) * 0.5, (c012.y() + c123.y()) * 0.5);
	flattenCurve(c0, c01, c012, c0123, tolerance, depth - 1, out);
	flattenCurve(c0123, c123, c23, c3, tolerance, depth - 1, out);
}

}  // namespace


bool hasCurve(const MapCoordVector& coords)
{
	return std::any_of(begin(coords), end(coords), [](auto const& coord) { return coord.isCurveStart(); });
}

/**
 * Returns the coordinates with every curve replaced by straight segments.
 */
MapCoordVector flattenCoords(const MapCoordVector& coords, double tolerance)
{
	MapCoordVector out;
	out.reserve(coords.size() * 2);
	auto const size = coords.size();
	for (std::size_t i = 0; i < size; ++i)
	{
		auto vertex = coords[i];
		if (vertex.isCurveStart() && i + 3 < size)
		{
			vertex.setCurveStart(false);
			out.push_back(vertex);
			flattenCurve(MapCoordF(coords[i]), MapCoordF(coords[i+1]), MapCoordF(coords[i+2]), MapCoordF(coords[i+3]), tolerance, 16, out);
			out.pop_back();  // the end point follows as a vertex of its own, with its flags
			i += 2;
		}
		else
		{
			vertex.setCurveStart(false);
			out.push_back(vertex);
		}
	}
	return out;
}


OpResult flatten(const Map& /*map*/, const std::vector<const Object*>& in, const FlattenParams& params)
{
	OpResult result;
	if (!(params.tolerance > 0.0))
	{
		result.error = QStringLiteral("The tolerance must be positive.");
		return result;
	}
	
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path)
			continue;
		auto const* path = object->asPath();
		if (!hasCurve(path->getRawCoordinateVector()))
			continue;
	
		auto output = std::unique_ptr<PathObject>(new PathObject(path->getSymbol(), flattenCoords(path->getRawCoordinateVector(), params.tolerance)));
		output->setTags(path->tags());
		output->setPatternRotation(path->getPatternRotation());
		output->setPatternOrigin(path->getPatternOrigin());
		output->setIdentity(path->uuid(), path->createdTime(), path->modifiedTime());
		output->setDerivedFrom(path->derivedFrom());
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
