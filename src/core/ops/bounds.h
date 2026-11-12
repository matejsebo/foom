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


#ifndef OPENORIENTEERING_OPS_BOUNDS_H
#define OPENORIENTEERING_OPS_BOUNDS_H

#include <vector>

#include <QRectF>

#include "core/map_coord.h"

namespace OpenOrienteering {

class Object;

namespace ops {

/**
 * Returns the extent of the objects: the bounding box of their coordinates, in mm.
 *
 * For a boxed text the box counts, for other text only the anchor (the
 * second coordinate of a boxed text holds the box size). Null objects are
 * skipped. No coordinates give a null rectangle at the origin.
 *
 * See the design notes, "Align and distribute".
 */
QRectF boundingRect(const std::vector<const Object*>& in);

/**
 * Returns the centre of the objects' extent, see boundingRect().
 */
MapCoordF boundingCentre(const std::vector<const Object*>& in);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
