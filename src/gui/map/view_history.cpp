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


#include "view_history.h"

#include <vector>


namespace OpenOrienteering {

constexpr std::size_t ViewHistory::capacity;

bool ViewHistory::record(const Entry& entry)
{
	if (!entries.empty())
	{
		if (entries[position] == entry)
			return false;
		entries.erase(begin(entries) + position + 1, end(entries));
	}
	entries.push_back(entry);
	if (entries.size() > capacity)
		entries.erase(begin(entries));
	position = entries.size() - 1;
	return true;
}

const ViewHistory::Entry* ViewHistory::back()
{
	if (!canGoBack())
		return nullptr;
	--position;
	return &entries[position];
}

const ViewHistory::Entry* ViewHistory::forward()
{
	if (!canGoForward())
		return nullptr;
	++position;
	return &entries[position];
}


}  // namespace OpenOrienteering
