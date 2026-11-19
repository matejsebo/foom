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


#include "topology.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

constexpr double position_tolerance = 1e-6;

bool isLinePath(const Object* object)
{
	return object && object->getType() == Object::Path && object->getSymbol()
	       && (object->getSymbol()->getContainedTypes() & Symbol::Line);
}

bool isAreaPath(const Object* object)
{
	return object && object->getType() == Object::Path && object->getSymbol()
	       && (object->getSymbol()->getContainedTypes() & Symbol::Area)
	       && !(object->getSymbol()->getContainedTypes() & Symbol::Line);
}

/** A copy of the path which keeps its identity, for an output which replaces it. */
std::unique_ptr<PathObject> workingCopy(const PathObject& path)
{
	auto copy = std::unique_ptr<PathObject>(static_cast<PathObject*>(path.duplicate()));
	copy->update();
	return copy;
}

/** A new path with the symbol, tags and pattern of the original and a fresh identity derived from it. */
std::unique_ptr<PathObject> derivedPath(const PathObject& path, MapCoordVector coords)
{
	auto output = std::unique_ptr<PathObject>(new PathObject(path.getSymbol(), std::move(coords)));
	output->setTags(path.tags());
	output->setPatternRotation(path.getPatternRotation());
	output->setPatternOrigin(path.getPatternOrigin());
	output->setDerivedFrom(path.uuid());
	return output;
}

std::unique_ptr<PathObject> replacement(const PathObject& path, MapCoordVector coords)
{
	auto output = derivedPath(path, std::move(coords));
	output->setIdentity(path.uuid(), path.createdTime(), path.modifiedTime());
	output->setDerivedFrom(path.derivedFrom());
	return output;
}

MapCoordVector partCoords(const PathObject& path, PathPartVector::size_type part_index)
{
	auto const& part = path.parts()[part_index];
	auto const& coords = path.getRawCoordinateVector();
	return MapCoordVector(coords.begin() + part.first_index, coords.begin() + part.last_index + 1);
}

/** The coordinates of the part between the two lengths, see PathObject::changePathBounds(). */
MapCoordVector pieceBetween(const PathObject& path, PathPartVector::size_type part_index, PathCoord::length_type start_len, PathCoord::length_type end_len)
{
	PathObject piece(path.getSymbol(), path.getRawCoordinateVector());
	piece.changePathBounds(part_index, start_len, end_len);
	auto coords = partCoords(piece, part_index);
	coords.back().setHolePoint(false);
	return coords;
}

}  // namespace


MapCoordVector withoutRepeatedVertices(const MapCoordVector& coords, bool& changed)
{
	MapCoordVector out;
	out.reserve(coords.size());
	changed = false;
	auto handles_pending = 0;
	auto previous_is_vertex = false;
	for (auto const& coord : coords)
	{
		if (handles_pending > 0)
		{
			out.push_back(coord);
			--handles_pending;
			previous_is_vertex = false;
			continue;
		}
		if (previous_is_vertex && !out.back().isCurveStart() && out.back().isPositionEqualTo(coord))
		{
			// Drop the repeated vertex, keeping the flags it carried.
			auto& kept = out.back();
			kept.setCurveStart(coord.isCurveStart());
			kept.setClosePoint(kept.isClosePoint() || coord.isClosePoint());
			kept.setHolePoint(kept.isHolePoint() || coord.isHolePoint());
			kept.setDashPoint(kept.isDashPoint() || coord.isDashPoint());
			changed = true;
			handles_pending = coord.isCurveStart() ? 2 : 0;
			previous_is_vertex = handles_pending == 0;
			continue;
		}
		out.push_back(coord);
		handles_pending = coord.isCurveStart() ? 2 : 0;
		previous_is_vertex = handles_pending == 0;
	}
	return out;
}

double signedAreaOnPaper(const MapCoordVector& coords, MapCoordVector::size_type first, MapCoordVector::size_type last)
{
	// Shoelace over the vertices (curve handles included as a coarse approximation
	// of the curve), with y up: the map's y axis points down the paper.
	auto area = 0.0;
	for (auto i = first; i < last; ++i)
	{
		auto const& a = coords[i];
		auto const& b = coords[i + 1];
		area += a.x() * b.y() - b.x() * a.y();
	}
	if (!coords[last].isPositionEqualTo(coords[first]))
		area += coords[last].x() * coords[first].y() - coords[first].x() * coords[last].y();
	return -area / 2;
}


OpResult joinLines(const Map& /*map*/, const std::vector<const Object*>& in, const JoinParams& params)
{
	OpResult result;
	if (!(params.tolerance >= 0.0))
	{
		result.error = QStringLiteral("The tolerance must not be negative.");
		return result;
	}
	std::vector<const PathObject*> lines;
	for (auto const* object : in)
	{
		if (isLinePath(object) && object->asPath()->parts().size() == 1 && !object->asPath()->parts().front().isClosed())
			lines.push_back(object->asPath());
	}
	if (lines.size() < 2)
	{
		result.error = QStringLiteral("Select at least two open line objects.");
		return result;
	}
	
	struct Candidate
	{
		double distance;
		std::size_t a;
		std::size_t b;
	};
	std::vector<Candidate> candidates;
	auto const ends = [](const PathObject* line) {
		auto const& coords = line->getRawCoordinateVector();
		return std::make_pair(MapCoordF(coords.front()), MapCoordF(coords.back()));
	};
	for (std::size_t i = 0; i < lines.size(); ++i)
	{
		auto const ends_i = ends(lines[i]);
		for (std::size_t j = i + 1; j < lines.size(); ++j)
		{
			if (lines[i]->getSymbol() != lines[j]->getSymbol())
				continue;
			auto const ends_j = ends(lines[j]);
			auto best = std::numeric_limits<double>::max();
			for (auto const& p : { ends_i.first, ends_i.second })
			{
				for (auto const& q : { ends_j.first, ends_j.second })
					best = std::min(best, p.distanceTo(q));
			}
			if (best <= params.tolerance)
				candidates.push_back({ best, i, j });
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) { return l.distance < r.distance; });
	
	// Chains: every line points at the working copy which holds it now.
	std::vector<std::size_t> chain(lines.size());
	for (std::size_t i = 0; i < lines.size(); ++i)
		chain[i] = i;
	std::vector<std::unique_ptr<PathObject>> copies(lines.size());
	std::vector<bool> absorbed(lines.size(), false);
	auto const find = [&chain](std::size_t i) {
		while (chain[i] != i)
			i = chain[i];
		return i;
	};
	auto const threshold_sq = params.tolerance * params.tolerance;
	for (auto const& candidate : candidates)
	{
		auto const a = find(candidate.a);
		auto const b = find(candidate.b);
		if (a == b)
			continue;
		if (!copies[a])
			copies[a] = workingCopy(*lines[a]);
		if (!copies[b])
			copies[b] = workingCopy(*lines[b]);
		copies[a]->update();
		copies[b]->update();
		if (!copies[a]->connectIfClose(copies[b].get(), threshold_sq))
			continue;
		copies[a]->update();
		chain[b] = a;
		absorbed[b] = true;
		copies[b].reset();
	}
	for (std::size_t i = 0; i < lines.size(); ++i)
	{
		if (absorbed[i])
			result.consumed.push_back(lines[i]);
	}
	for (std::size_t i = 0; i < lines.size(); ++i)
	{
		if (!absorbed[i] && copies[i])
		{
			// A chain head which absorbed something.
			auto absorbed_any = false;
			for (std::size_t j = 0; j < lines.size() && !absorbed_any; ++j)
				absorbed_any = absorbed[j] && find(j) == i;
			if (!absorbed_any)
				continue;
			result.consumed.push_back(lines[i]);
			result.out.push_back(std::move(copies[i]));
		}
	}
	return result;
}

OpResult closeAreas(const Map& /*map*/, const std::vector<const Object*>& in, const OpParams& /*params*/)
{
	OpResult result;
	for (auto const* object : in)
	{
		if (!isAreaPath(object))
			continue;
		auto const* path = object->asPath();
		auto open = false;
		for (auto const& part : path->parts())
			open = open || !part.isClosed();
		if (!open)
			continue;
		auto output = workingCopy(*path);
		for (auto& part : output->parts())
		{
			if (!part.isClosed())
				part.setClosed(true, true);
		}
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	return result;
}

OpResult areasAnticlockwise(const Map& /*map*/, const std::vector<const Object*>& in, const OpParams& /*params*/)
{
	OpResult result;
	for (auto const* object : in)
	{
		if (!isAreaPath(object))
			continue;
		auto const* path = object->asPath();
		auto const& coords = path->getRawCoordinateVector();
		std::vector<PathPartVector::size_type> to_reverse;
		for (PathPartVector::size_type p = 0; p < path->parts().size(); ++p)
		{
			auto const& part = path->parts()[p];
			if (part.last_index <= part.first_index + 1)
				continue;
			auto const area = signedAreaOnPaper(coords, part.first_index, part.last_index);
			if ((p == 0 && area < 0.0) || (p > 0 && area > 0.0))
				to_reverse.push_back(p);
		}
		if (to_reverse.empty())
			continue;
		auto output = workingCopy(*path);
		for (auto p : to_reverse)
			output->parts()[p].reverse();
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	return result;
}

OpResult removeDuplicateVertices(const Map& /*map*/, const std::vector<const Object*>& in, const OpParams& /*params*/)
{
	OpResult result;
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Path)
			continue;
		auto const* path = object->asPath();
		auto changed = false;
		auto coords = withoutRepeatedVertices(path->getRawCoordinateVector(), changed);
		if (!changed)
			continue;
		result.out.push_back(replacement(*path, std::move(coords)));
		result.consumed.push_back(object);
	}
	return result;
}


namespace {

/** The crossings of the path with the other inputs, as lengths along its first part. */
std::vector<PathObject::Intersection> crossingsOf(const PathObject& path, const std::vector<const PathObject*>& others)
{
	PathObject::Intersections all;
	for (auto const* other : others)
	{
		if (other != &path)
			path.calcAllIntersectionsWith(other, all);
	}
	all.normalize();
	return std::vector<PathObject::Intersection>(all.begin(), all.end());
}

}  // namespace


OpResult fixOvershoots(const Map& /*map*/, const std::vector<const Object*>& in, const OvershootParams& params)
{
	OpResult result;
	if (!(params.tolerance >= 0.0))
	{
		result.error = QStringLiteral("The tolerance must not be negative.");
		return result;
	}
	std::vector<const PathObject*> paths;
	for (auto const* object : in)
	{
		if (object && object->getType() == Object::Path)
		{
			object->update();
			paths.push_back(object->asPath());
		}
	}
	if (paths.size() < 2)
	{
		result.error = QStringLiteral("Select at least two path objects.");
		return result;
	}
	
	for (auto const* path : paths)
	{
		if (!isLinePath(path) || path->parts().size() != 1 || path->parts().front().isClosed())
			continue;
		auto const& part = path->parts().front();
		auto const total = part.length();
		if (!(total > 0.0))
			continue;
		
		// Overshoots: the last crossing within the tolerance of the start,
		// and the first within the tolerance of the end.
		auto trim_start = PathCoord::length_type(0);
		auto trim_end = total;
		if (params.remove_overshoots)
		{
			for (auto const& crossing : crossingsOf(*path, paths))
			{
				if (crossing.part_index != 0)
					continue;
				if (crossing.length > position_tolerance && crossing.length <= params.tolerance)
					trim_start = std::max(trim_start, crossing.length);
				if (crossing.length < total - position_tolerance && total - crossing.length <= params.tolerance)
					trim_end = std::min(trim_end, crossing.length);
			}
		}
		auto changed = trim_start > 0.0 || trim_end < total;
		if (changed && trim_start >= trim_end)
			continue;  // the whole line would go
		auto coords = changed ? pieceBetween(*path, 0, trim_start, trim_end) : partCoords(*path, 0);
		
		// Undershoots: extend an untrimmed end along its tangent.
		if (params.remove_undershoots && params.tolerance > 0.0)
		{
			auto const& path_coords = part.path_coords;
			std::vector<const PathObject*> others;
			for (auto const* other : paths)
			{
				if (other != path)
					others.push_back(other);
			}
			auto const extend = [&](bool at_start) -> bool {
				auto const& end = at_start ? path_coords.front() : path_coords.back();
				auto const& before = at_start ? path_coords[1] : path_coords[path_coords.size() - 2];
				auto direction = end.pos - before.pos;
				auto const length = std::hypot(direction.x(), direction.y());
				if (!(length > 0.0))
					return false;
				direction = MapCoordF(direction.x() / length * params.tolerance, direction.y() / length * params.tolerance);
				PathObject probe(path->getSymbol(), { MapCoord(end.pos), MapCoord(end.pos + direction) });
				probe.update();
				auto best = std::numeric_limits<double>::max();
				MapCoordF hit;
				for (auto const& crossing : crossingsOf(probe, others))
				{
					if (crossing.length > position_tolerance && crossing.coord.distanceTo(end.pos) > position_tolerance && crossing.length < best)
					{
						best = crossing.length;
						hit = crossing.coord;
					}
				}
				if (best > params.tolerance)
					return false;
				auto& moved = at_start ? coords.front() : coords.back();
				moved.setX(hit.x());
				moved.setY(hit.y());
				return true;
			};
			if (trim_start == 0.0 && path_coords.size() >= 2 && extend(true))
				changed = true;
			if (trim_end == total && path_coords.size() >= 2 && extend(false))
				changed = true;
		}
		if (!changed)
			continue;
		result.out.push_back(replacement(*path, std::move(coords)));
		result.consumed.push_back(path);
	}
	return result;
}

OpResult intersections(const Map& /*map*/, const std::vector<const Object*>& in, const IntersectionParams& params)
{
	OpResult result;
	if (params.action == IntersectionParams::AddPoint && (!params.point_symbol || params.point_symbol->getType() != Symbol::Point))
	{
		result.error = QStringLiteral("Select a point symbol for the new objects.");
		return result;
	}
	std::vector<const PathObject*> paths;
	for (auto const* object : in)
	{
		if (object && object->getType() == Object::Path)
		{
			object->update();
			paths.push_back(object->asPath());
		}
	}
	if (paths.size() < 2)
	{
		result.error = QStringLiteral("Select at least two path objects.");
		return result;
	}
	
	auto const is_touch = [](const PathObject& path, const PathObject& other, const PathObject::Intersection& crossing) {
		auto const& part = path.parts()[crossing.part_index];
		auto const& other_part = other.parts()[crossing.other_part_index];
		auto const at_end = [](const PathPart& p, PathCoord::length_type length) {
			return !p.isClosed() && (length <= position_tolerance || length >= p.length() - position_tolerance);
		};
		return at_end(part, crossing.length) || at_end(other_part, crossing.other_length);
	};
	
	std::vector<MapCoordF> points;
	for (std::size_t i = 0; i < paths.size(); ++i)
	{
		auto const* path = paths[i];
		// The crossings on this path, per part, as lengths.
		std::vector<std::vector<PathCoord::length_type>> lengths(path->parts().size());
		for (std::size_t j = 0; j < paths.size(); ++j)
		{
			if (j == i)
				continue;
			PathObject::Intersections found;
			path->calcAllIntersectionsWith(paths[j], found);
			found.normalize();
			for (auto const& crossing : found)
			{
				if (!params.include_touching && is_touch(*path, *paths[j], crossing))
					continue;
				lengths[crossing.part_index].push_back(crossing.length);
				if (params.action == IntersectionParams::AddPoint && j > i)
					points.push_back(crossing.coord);
			}
		}
		if (params.action == IntersectionParams::AddPoint)
			continue;
		
		auto any = false;
		for (auto& list : lengths)
		{
			std::sort(list.begin(), list.end());
			list.erase(std::unique(list.begin(), list.end(), [](PathCoord::length_type a, PathCoord::length_type b) { return std::abs(a - b) <= position_tolerance; }), list.end());
			any = any || !list.empty();
		}
		if (!any)
			continue;
		
		if (params.action == IntersectionParams::InsertVertex)
		{
			auto output = workingCopy(*path);
			auto changed = false;
			for (PathPartVector::size_type p = 0; p < lengths.size(); ++p)
			{
				// From the end, so that earlier lengths stay valid.
				for (auto it = lengths[p].rbegin(); it != lengths[p].rend(); ++it)
				{
					output->update();
					auto const& part = output->parts()[p];
					auto const split = SplitPathCoord::at(*it, SplitPathCoord::begin(part.path_coords));
					if (split.param <= 1e-4 || split.param >= 1.0 - 1e-4)
						continue;  // a vertex is there already
					auto const index = output->subdivide(split.index, split.param);
					output->getCoordinateRef(index).setDashPoint(params.dash);
					changed = true;
				}
			}
			if (!changed)
				continue;
			output->update();
			result.out.push_back(std::move(output));
			result.consumed.push_back(path);
		}
		else  // Cut
		{
			if (path->parts().size() != 1 || lengths.front().empty())
				continue;
			auto const& part = path->parts().front();
			auto const& list = lengths.front();
			auto const total = part.length();
			std::vector<PathCoord::length_type> cuts;
			for (auto length : list)
			{
				if (length > position_tolerance && length < total - position_tolerance)
					cuts.push_back(length);
			}
			if (cuts.empty())
				continue;
			std::vector<MapCoordVector> pieces;
			if (part.isClosed())
			{
				for (std::size_t k = 0; k < cuts.size(); ++k)
					pieces.push_back(pieceBetween(*path, 0, cuts[k], cuts[(k + 1) % cuts.size()]));
			}
			else
			{
				auto previous = PathCoord::length_type(0);
				for (auto cut : cuts)
				{
					pieces.push_back(pieceBetween(*path, 0, previous, cut));
					previous = cut;
				}
				pieces.push_back(pieceBetween(*path, 0, previous, total));
			}
			auto first = true;
			for (auto& piece : pieces)
			{
				result.out.push_back(first ? replacement(*path, std::move(piece)) : derivedPath(*path, std::move(piece)));
				first = false;
			}
			result.consumed.push_back(path);
		}
	}
	
	if (params.action == IntersectionParams::AddPoint)
	{
		std::vector<MapCoordF> distinct;
		for (auto const& point : points)
		{
			auto const seen = std::any_of(distinct.begin(), distinct.end(), [&point](const MapCoordF& d) { return d.distanceTo(point) <= position_tolerance; });
			if (seen)
				continue;
			distinct.push_back(point);
			auto object = std::unique_ptr<PointObject>(new PointObject(params.point_symbol));
			object->setPosition(MapCoord(point));
			result.out.push_back(std::move(object));
		}
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
