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


#ifndef OPENORIENTEERING_BACKGROUND_EXTENTS_H
#define OPENORIENTEERING_BACKGROUND_EXTENTS_H

#include <vector>

#include <QString>

#include "core/extents.h"

namespace OpenOrienteering {

class Map;


/** A named extent of a background map, in the coordinates of the map which shows it. */
struct BackgroundExtent
{
	QString background;  ///< The file name of the background map.
	Extent extent;
};


/**
 * The named extents of the map templates of the map, read from their files.
 *
 * See the design notes, "Canvases: load extent from background map".
 */
std::vector<BackgroundExtent> backgroundMapExtents(const Map& map);

/** The text of an entry: "<name> (Background map: <file name>)". */
QString backgroundExtentLabel(const BackgroundExtent& entry);

/** Puts the extent into the extents of the map, replacing one of the same name, as one undo step. */
bool loadExtentFromBackgroundMap(Map& map, const BackgroundExtent& entry);


}  // namespace OpenOrienteering

#endif
