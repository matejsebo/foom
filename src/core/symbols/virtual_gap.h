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

#ifndef OPENORIENTEERING_VIRTUAL_GAP_H
#define OPENORIENTEERING_VIRTUAL_GAP_H

#include "core/map_coord.h"

namespace OpenOrienteering {

class PathPartVector;


/**
 * Splits the given path parts where virtual gaps hide segments.
 *
 * When a coordinate of the parts which starts a segment carries the
 * virtual gap flag, the flags and coordinates of the pieces which line
 * symbols draw are written to out_flags and out_coords, and true is
 * returned. A piece is an open part, ended by a hole point; a closed part
 * with gaps becomes the pieces between them, the one across the close
 * point joined into one. Pieces of a single coordinate are left out. Parts
 * without gaps are copied as they are. Without any gap, false is returned
 * and the output is left alone.
 *
 * See the design notes, "Corner and virtual gap vertices".
 */
bool splitAtVirtualGaps(const PathPartVector& parts, MapCoordVector& out_flags, MapCoordVectorF& out_coords);


}  // namespace OpenOrienteering

#endif
