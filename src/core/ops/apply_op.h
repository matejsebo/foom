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


#ifndef OPENORIENTEERING_OPS_APPLY_OP_H
#define OPENORIENTEERING_OPS_APPLY_OP_H

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Applies the result of an operation to the map's current part.
 *
 * The consumed objects are released from the part, the outputs are added,
 * both recorded in one combined undo step (add step for the consumed
 * objects, delete step for the outputs), and the selection becomes the
 * outputs.
 *
 * When principal is given, every output receives its symbol, tags and
 * identity; the map's uuid registry gives the second and later outputs a
 * fresh uuid with derivedFrom() set. Without a principal, the outputs keep
 * what the operation gave them (an output made by duplicate() carries its
 * input's symbol, tags and identity). The outputs' modification time is set
 * to now, since the delete step which records them stamps nothing.
 *
 * Returns false and leaves the map unchanged when the result carries an
 * error, or when a consumed object is not in the current part. A result
 * with neither outputs nor consumed objects changes nothing and returns true.
 */
bool applyOp(Map& map, OpResult&& result, const Object* principal = nullptr);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
