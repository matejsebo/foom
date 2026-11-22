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

#include "virtual_gap.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "core/objects/object.h"
#include "core/virtual_coord_vector.h"


namespace OpenOrienteering {

namespace {

using Range = std::pair<std::size_t, std::size_t>;  // first and last index, inclusive

/** The index of the vertex after the segment which starts at i. */
std::size_t nextVertex(const PathPart& part, std::size_t i)
{
	return i + (part.coords.flags[i].isCurveStart() ? 3 : 1);
}

bool hasGap(const PathPart& part)
{
	for (auto i = part.first_index; i < part.last_index; i = nextVertex(part, i))
	{
		if (part.coords.flags[i].isVirtualGap())
			return true;
	}
	return false;
}

/** Appends the ranges as one open piece. The first coordinate of every range after the first is skipped, being the last of the one before. */
void appendPiece(const PathPart& part, const std::vector<Range>& ranges, MapCoordVector& out_flags, MapCoordVectorF& out_coords)
{
	for (std::size_t r = 0; r < ranges.size(); ++r)
	{
		auto i = ranges[r].first;
		if (r > 0)
		{
			// The joining coordinate takes the flags which start the next segment.
			out_flags.back().setCurveStart(part.coords.flags[i].isCurveStart());
			++i;
		}
		for (; i <= ranges[r].second; ++i)
		{
			auto flags = part.coords.flags[i];
			flags.setClosePoint(false);
			flags.setHolePoint(false);
			flags.setVirtualGap(false);
			out_flags.push_back(flags);
			out_coords.push_back(part.coords[i]);
		}
	}
	out_flags.back().setCurveStart(false);
	out_flags.back().setHolePoint(true);
}

}  // namespace


bool splitAtVirtualGaps(const PathPartVector& parts, MapCoordVector& out_flags, MapCoordVectorF& out_coords)
{
	auto any = false;
	for (auto const& part : parts)
		any |= hasGap(part);
	if (!any)
		return false;

	out_flags.clear();
	out_coords.clear();
	for (auto const& part : parts)
	{
		if (!hasGap(part))
		{
			for (auto i = part.first_index; i <= part.last_index; ++i)
			{
				auto flags = part.coords.flags[i];
				flags.setHolePoint(i == part.last_index);
				out_flags.push_back(flags);
				out_coords.push_back(part.coords[i]);
			}
			continue;
		}

		std::vector<Range> pieces;
		auto start = part.first_index;
		for (auto i = part.first_index; i < part.last_index; )
		{
			auto const next = nextVertex(part, i);
			if (part.coords.flags[i].isVirtualGap())
			{
				pieces.emplace_back(start, i);
				start = next;
			}
			i = next;
		}
		pieces.emplace_back(start, part.last_index);

		if (part.isClosed() && pieces.front().first == part.first_index && pieces.back().second == part.last_index)
		{
			// The piece across the close point
			auto const across = std::vector<Range>{ pieces.back(), pieces.front() };
			pieces.pop_back();
			pieces.erase(pieces.begin());
			if (across[0].second > across[0].first || across[1].second > across[1].first)
				appendPiece(part, across, out_flags, out_coords);
		}
		for (auto const& piece : pieces)
		{
			if (piece.second > piece.first)
				appendPiece(part, { piece }, out_flags, out_coords);
		}
	}
	if (!out_flags.empty())
		out_flags.back().setHolePoint(false);
	return true;
}


}  // namespace OpenOrienteering
