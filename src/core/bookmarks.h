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


#ifndef OPENORIENTEERING_BOOKMARKS_H
#define OPENORIENTEERING_BOOKMARKS_H

#include <QtGlobal>
#include <QString>

#include "core/map_coord.h"

namespace OpenOrienteering {

/**
 * A named view: centre and zoom.
 *
 * See the design notes, "Bookmarks and view history".
 */
struct Bookmark
{
	QString name;
	MapCoord center;
	double zoom = 1.0;
	QString note;
};

inline bool operator==(const Bookmark& lhs, const Bookmark& rhs)
{
	return lhs.name == rhs.name
	       && lhs.center == rhs.center
	       && qFuzzyCompare(lhs.zoom, rhs.zoom)
	       && lhs.note == rhs.note;
}

inline bool operator!=(const Bookmark& lhs, const Bookmark& rhs)
{
	return !(lhs == rhs);
}


}  // namespace OpenOrienteering

#endif
