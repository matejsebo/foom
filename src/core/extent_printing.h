/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef FOOM_EXTENT_PRINTING_H
#define FOOM_EXTENT_PRINTING_H

#include <QtGlobal>

#ifdef QT_PRINTSUPPORT_LIB

namespace OpenOrienteering {

class Map;
class MapPrinter;
struct Extent;


/**
 * Sets the printer up for the extent: the print scale, the page (the extent's
 * paper, or a page of the extent's size), the print area (the extent less its
 * margin) and the extents which back it as further sides.
 *
 * Returns the number of sides, 1 for an extent without a back.
 *
 * See the design notes, "Extents in print and export".
 */
int applyExtentToPrinter(MapPrinter& printer, const Map& map, const Extent& extent);


}  // namespace OpenOrienteering

#endif

#endif
