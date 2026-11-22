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


#ifndef OPENORIENTEERING_OPS_VERTEX_TYPE_H
#define OPENORIENTEERING_OPS_VERTEX_TYPE_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * The vertex types which setVertexType() sets.
 */
enum class VertexType
{
	Normal,     ///< none of the flags below
	Dash,       ///< the dash flag (OOM's dash point)
	Corner,     ///< the dash and the corner flag (a corner vertex)
	VirtualGap  ///< the virtual gap flag
};


/**
 * Parameters of setVertexType().
 */
struct VertexTypeParams : public OpParams
{
	/** The type every vertex gets. */
	VertexType type = VertexType::Normal;
	
	/** Leave the first and the last coordinate of every part alone. */
	bool keep_ends = true;
};


/**
 * Gives every vertex of the path objects among the inputs one type
 * ("set-vertex-type"): it sets that type's flags and clears the others'.
 *
 * The control points of curves are left alone. Objects which are not
 * paths, and paths which would not change, are neither consumed nor output.
 * The outputs carry the inputs' symbol, tags, pattern and identity. An
 * input without a path object is an error.
 *
 * See the design notes, "Vertex types" and "Corner and
 * virtual gap vertices".
 */
OpResult setVertexType(const Map& map, const std::vector<const Object*>& in, const VertexTypeParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
