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


#include "reshape.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * Returns the coordinates of the given part of the path.
 */
MapCoordVector partCoords(const PathObject& path, PathPartVector::size_type part_index)
{
	auto const& part = path.parts()[part_index];
	auto const& coords = path.getRawCoordinateVector();
	return MapCoordVector(begin(coords) + part.first_index, begin(coords) + part.last_index + 1);
}

/**
 * Returns the coordinates of the part between the two lengths, see
 * PathObject::changePathBounds().
 */
MapCoordVector pieceBetween(const PathObject& path, PathPartVector::size_type part_index, PathCoord::length_type start_len, PathCoord::length_type end_len)
{
	PathObject piece(path.getSymbol(), path.getRawCoordinateVector());
	piece.changePathBounds(part_index, start_len, end_len);
	auto coords = partCoords(piece, part_index);
	coords.back().setHolePoint(false);
	return coords;
}

/**
 * Returns the stroke with its direction reversed; curve flags follow.
 */
MapCoordVector reversed(const PathObject& path, MapCoordVector stroke)
{
	PathObject helper(path.getSymbol(), std::move(stroke));
	helper.reverse();
	return helper.getRawCoordinateVector();
}

/**
 * Appends the stroke to the coordinates which end at its snapped start.
 *
 * The stroke's first coordinate is dropped in favour of the last coordinate
 * present, which takes over its curve flag; the stroke's last coordinate
 * is dropped too, since the caller continues at its snapped position.
 */
void appendStrokeInterior(MapCoordVector& coords, const MapCoordVector& stroke)
{
	coords.back().setCurveStart(stroke.front().isCurveStart());
	coords.insert(end(coords), begin(stroke) + 1, end(stroke) - 1);
}

}  // namespace


OpResult reshape(const Map& /*map*/, const std::vector<const Object*>& in, const ReshapeParams& params)
{
	OpResult result;
	if (in.size() != 1 || !in.front() || in.front()->getType() != Object::Path)
	{
		result.error = QStringLiteral("Select one line or area object.");
		return result;
	}
	if (params.stroke.size() < 2)
	{
		result.error = QStringLiteral("The stroke needs at least two points.");
		return result;
	}
	auto const* target = in.front()->asPath();
	target->update();
	
	// Snap the ends.
	auto const snap_sq = params.snap_distance * params.snap_distance;
	auto const start_hit = target->findClosestPointTo(MapCoordF(params.stroke.front()));
	auto const end_hit = target->findClosestPointTo(MapCoordF(params.stroke.back()));
	if (start_hit.distance_squared > snap_sq || end_hit.distance_squared > snap_sq)
	{
		result.error = QStringLiteral("The stroke must start and end on the object.");
		return result;
	}
	auto const part_index = target->findPartIndexForIndex(start_hit.path_coord.index);
	if (part_index != target->findPartIndexForIndex(end_hit.path_coord.index))
	{
		result.error = QStringLiteral("The stroke must start and end on the same line, border or hole.");
		return result;
	}
	auto start_len = start_hit.path_coord.clen;
	auto end_len = end_hit.path_coord.clen;
	if (qFuzzyCompare(1.0 + start_len, 1.0 + end_len))
	{
		result.error = QStringLiteral("The stroke must end at a different point of the object.");
		return result;
	}
	
	auto const& part = target->parts()[part_index];
	auto const part_begin = part.path_coords.front().clen;
	auto const part_end = part.path_coords.back().clen;
	auto stroke = params.stroke;
	MapCoordVector new_part;
	if (part.isClosed())
	{
		// Replace the shorter arc from P forward to Q; keep the arc from Q forward to P.
		auto const length = part_end - part_begin;
		auto forward = end_len - start_len;
		if (forward < 0)
			forward += length;
		auto const replace_forward = (forward <= length - forward) != params.other_arc;
		if (!replace_forward)
		{
			std::swap(start_len, end_len);
			stroke = reversed(*target, std::move(stroke));
		}
		new_part = pieceBetween(*target, part_index, end_len, start_len);
		appendStrokeInterior(new_part, stroke);
		auto close = MapCoord(new_part.front().x(), new_part.front().y());
		close.setClosePoint(true);
		new_part.push_back(close);
	}
	else
	{
		if (start_len > end_len)
		{
			std::swap(start_len, end_len);
			stroke = reversed(*target, std::move(stroke));
		}
		if (start_len > part_begin)
		{
			new_part = pieceBetween(*target, part_index, part_begin, start_len);
		}
		else
		{
			new_part.push_back(MapCoord(start_hit.path_coord.pos));
			new_part.back().setFlags(stroke.front().flags());
		}
		appendStrokeInterior(new_part, stroke);
		if (end_len < part_end)
		{
			auto const tail = pieceBetween(*target, part_index, end_len, part_end);
			new_part.insert(new_part.end(), begin(tail), end(tail));
		}
		else
		{
			new_part.push_back(MapCoord(end_hit.path_coord.pos));
		}
	}
	new_part.back().setHolePoint(target->getCoordinate(part.last_index).isHolePoint());
	
	// Assemble the whole object.
	auto const& old_coords = target->getRawCoordinateVector();
	MapCoordVector coords(begin(old_coords), begin(old_coords) + part.first_index);
	coords.insert(end(coords), begin(new_part), end(new_part));
	coords.insert(end(coords), begin(old_coords) + part.last_index + 1, end(old_coords));
	auto output = std::unique_ptr<PathObject>(new PathObject(target->getSymbol(), std::move(coords)));
	output->setTags(target->tags());
	output->setPatternRotation(target->getPatternRotation());
	output->setPatternOrigin(target->getPatternOrigin());
	output->setIdentity(target->uuid(), target->createdTime(), target->modifiedTime());
	output->setDerivedFrom(target->derivedFrom());
	result.out.push_back(std::move(output));
	result.consumed.push_back(in.front());
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
