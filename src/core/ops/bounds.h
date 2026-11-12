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

#include "core/map_coord.h"

namespace OpenOrienteering {

class Object;

namespace ops {

/**
 * Returns the centre of the bounding box of the objects' coordinates.
 *
 * For text objects, only the anchor counts (the second coordinate of a
 * boxed text holds the box size). Null objects are skipped. The centre of
 * no coordinates is the origin.
 */
MapCoordF boundingCentre(const std::vector<const Object*>& in);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
