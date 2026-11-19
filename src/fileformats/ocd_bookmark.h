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


#ifndef OPENORIENTEERING_OCD_BOOKMARK_H
#define OPENORIENTEERING_OCD_BOOKMARK_H

#include <QString>

#include "core/bookmarks.h"
#include "core/map_coord.h"
#include "fileformats/ocd_parameter_stream_reader.h"

namespace OpenOrienteering {

/**
 * Parses an OCD type 18 parameter string: the bookmark name, then x and y
 * in mm (y up), z zoom and d note. Returns false when there is no name.
 *
 * See the design notes, "Bookmarks and view history".
 */
inline bool parseOcdBookmark(const QString& param_string, Bookmark& out)
{
	OcdParameterStreamReader parameters(param_string);
	Bookmark bookmark;
	bookmark.name = parameters.value().toString();  // the first field, before any key
	auto x = 0.0, y = 0.0;
	while (parameters.readNext())
	{
		auto const value = parameters.value();
		switch (parameters.key())
		{
		case 'x':
			x = value.toDouble();
			break;
		case 'y':
			y = value.toDouble();
			break;
		case 'z':
			bookmark.zoom = value.toDouble();
			break;
		case 'd':
			bookmark.note = value.toString();
			break;
		default:
			break;
		}
	}
	if (bookmark.name.isEmpty())
		return false;
	bookmark.center = MapCoord(x, -y);
	out = bookmark;
	return true;
}


}  // namespace OpenOrienteering

#endif
