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


#ifndef OPENORIENTEERING_OPS_GROUP_H
#define OPENORIENTEERING_OPS_GROUP_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of setGroup().
 */
struct GroupParams : public OpParams
{
	/** The group number to give the objects, 0 for none. */
	int group = 0;
};


/**
 * Gives every input the group number ("group", "ungroup").
 *
 * Objects which have the number already are neither consumed nor output.
 * The outputs carry the inputs' symbol, tags and identity. An empty input
 * is an error.
 *
 * See the design notes, "Groups".
 */
OpResult setGroup(const Map& map, const std::vector<const Object*>& in, const GroupParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
