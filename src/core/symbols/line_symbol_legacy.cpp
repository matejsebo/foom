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


#include "line_symbol.h"

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"
#include "core/path_coord.h"
#include "core/symbols/point_symbol.h"
#include "core/virtual_path.h"


// The legacy layout of the dashes of a line, which symbols read from OCD files use; see the design notes.

namespace OpenOrienteering {

bool LineSymbol::usesLegacyDashLayout() const
{
	// Single dashes without symbols on them; groups of dashes and dashes which carry
	// mid symbols are set by OOM's layout, whatever the option says.
	return legacy_dashes && dashed && dashes_in_group == 1 && dash_length > 0 && break_length > 0
	       && !(mid_symbol && !mid_symbol->isEmpty() && mid_symbols_per_spot > 0 && mid_symbol_placement != NoMidSymbols);
}

void LineSymbol::processDashedLineLegacy(
        const VirtualPath& path,
        const SplitPathCoord& start,
        const SplitPathCoord& end,
        MapCoordVector& out_flags,
        MapCoordVectorF& out_coords,
        ObjectRenderables& output ) const
{
	auto const& path_coords = path.path_coords;
	auto const& flags = path.coords.flags;
	
	auto const dash = 0.001 * dash_length;
	auto const gap = 0.001 * break_length;
	// The share of a dash which stands at an end of the line and at a corner vertex; none: a gap stands there.
	auto const end_share = qBound(0.0, double(end_dash_length) / dash_length, 1.0);
	
	// The line falls into sections at its dash and corner vertices.
	struct Boundary
	{
		double clen;
		double share;  // of a dash, at this side of the section
	};
	std::vector<Boundary> boundaries;
	boundaries.push_back({ double(start.clen), end_share });
	for (auto index = path_coords.findNextDashPoint(start.path_coord_index);
	     index < path_coords.size() && path_coords[index].clen < end.clen;
	     index = path_coords.findNextDashPoint(index))
	{
		if (path_coords[index].clen <= start.clen)
			continue;
		// A corner vertex is an end of the pattern; on a dash vertex lies the middle of a dash. Where the
		// symbol has a dash symbol, which is the corner symbol of an OCD file, a dash point is a corner vertex.
		auto const corner = flags[path_coords[index].index].isCornerPoint() || (dash_symbol && !dash_symbol->isEmpty());
		boundaries.push_back({ double(path_coords[index].clen), corner ? end_share : 0.5 });
		if (index + 1 >= path_coords.size())
			break;
	}
	boundaries.push_back({ double(end.clen), end_share });
	
	std::vector<std::pair<double, double>> dashes;
	auto const add = [&dashes](double from, double to) {
		if (to - from < 1e-6)
			return;
		if (!dashes.empty() && from - dashes.back().second < 1e-4)
			dashes.back().second = to;  // two dashes which meet at a vertex are one
		else
			dashes.emplace_back(from, to);
	};
	for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
	{
		auto const from = boundaries[i].clen;
		auto const length = boundaries[i+1].clen - from;
		if (length <= 0.0)
			continue;
		auto const first = boundaries[i].share;
		auto const last = boundaries[i+1].share;
		auto const shares = first + last;
	
		// Between the two ends lie whole dashes, with a gap before each and one after the last: the number
		// which comes nearest to the dash's length; the gaps keep theirs and the dashes take the rest.
		auto count = qMax(0, int(std::floor((length - gap - shares * dash) / (dash + gap) + 0.5)));
		if (shares <= 0.0)
			count = qMax(1, count);
		auto const adjusted = (length - (count + 1) * gap) / (count + shares);
		if (adjusted <= 0.0)
		{
			// Too short for two gaps. With gaps at the ends, the first gap is drawn and a dash to the end.
			if (shares <= 0.0)
				add(from + gap, from + length);
			else
				add(from, from + length);
			continue;
		}
	
		auto position = from;
		if (first > 0.0)
		{
			add(position, position + first * adjusted);
			position += first * adjusted;
		}
		for (int k = 0; k < count; ++k)
		{
			position += gap;
			add(position, position + adjusted);
			position += adjusted;
		}
		if (last > 0.0)
			add(from + length - last * adjusted, from + length);
	}
	
	// The processed path is one run of coordinates in which a gap flag switches between dash and gap.
	auto cursor = start;
	auto first = true;
	for (auto const& piece : dashes)
	{
		auto const dash_start = SplitPathCoord::at(PathCoord::length_type(qBound(double(start.clen), piece.first, double(end.clen))), cursor);
		auto const dash_end = SplitPathCoord::at(PathCoord::length_type(qBound(double(start.clen), piece.second, double(end.clen))), dash_start);
		if (!(dash_end.clen > dash_start.clen))
			continue;
		if (dash_start.clen > cursor.clen)
		{
			auto const mark = out_flags.size();
			path.copy(cursor, dash_start, out_flags, out_coords);
			if (first && mark < out_flags.size())
				out_flags[mark].setGapPoint(true);  // the line begins with a gap
			out_flags.back().setGapPoint(true);
		}
		processContinuousLine(path, dash_start, dash_end, false, out_flags, out_coords, output);
		cursor = dash_end;
		first = false;
	}
}


}  // namespace OpenOrienteering
