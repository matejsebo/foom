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


#ifndef OPENORIENTEERING_OPS_RESHAPE_H
#define OPENORIENTEERING_OPS_RESHAPE_H

#include <vector>

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of reshape().
 */
struct ReshapeParams : public OpParams
{
	/** The stroke, at least two coordinates; curve flags are kept. */
	MapCoordVector stroke;
	
	/** How far (mm) the stroke's ends may lie from the target path. */
	double snap_distance = 1.0;
	
	/** On a closed part, replace the longer arc instead of the shorter one. */
	bool other_arc = false;
};


/**
 * Replaces the stretch of a path between the stroke's ends by the stroke
 * ("reshape").
 *
 * The input is exactly one path object. The stroke's ends are snapped to
 * the path; they must lie within the snap distance, on the same part and
 * at different positions. On an open part the stretch between the ends is
 * replaced, on a closed one the shorter arc, or the longer one with
 * params.other_arc. The output keeps the input's symbol, tags, pattern and
 * identity; the input is consumed.
 *
 * See the design notes, "Reshape".
 */
OpResult reshape(const Map& map, const std::vector<const Object*>& in, const ReshapeParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
