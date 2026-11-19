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


#ifndef OPENORIENTEERING_VIEW_HISTORY_H
#define OPENORIENTEERING_VIEW_HISTORY_H

#include <cstddef>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"

namespace OpenOrienteering {

/**
 * The history of views the user has looked at: centre and zoom.
 *
 * See the design notes, "Bookmarks and view history".
 */
class ViewHistory
{
public:
	struct Entry
	{
		MapCoord center;
		double zoom = 1.0;
	};
	
	/** 32 views are kept. */
	static constexpr std::size_t capacity = 32;
	
	/**
	 * Records a view after the current entry, dropping the entries after it.
	 *
	 * Returns false, changing nothing, when the view equals the current entry.
	 * The oldest entry goes when there are more than capacity.
	 */
	bool record(const Entry& entry);
	
	bool canGoBack() const { return position > 0; }
	bool canGoForward() const { return !entries.empty() && position + 1 < entries.size(); }
	
	/** Moves to the previous entry and returns it, or nullptr. */
	const Entry* back();
	
	/** Moves to the next entry and returns it, or nullptr. */
	const Entry* forward();
	
	std::size_t size() const { return entries.size(); }

private:
	std::vector<Entry> entries;
	std::size_t position = 0;
};

inline bool operator==(const ViewHistory::Entry& lhs, const ViewHistory::Entry& rhs)
{
	return lhs.center == rhs.center && qFuzzyCompare(lhs.zoom, rhs.zoom);
}


}  // namespace OpenOrienteering

#endif
