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


#ifndef OPENORIENTEERING_OPS_SIZED_SHAPE_H
#define OPENORIENTEERING_OPS_SIZED_SHAPE_H

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Symbol;

namespace ops {

struct SizedShapeParams : public OpParams
{
	enum Kind
	{
		Circle,
		Ellipse,
		Line,
		Rectangle
	};

	Kind kind = Circle;
	/** The radius of a circle, the first axis of an ellipse, the length of a line, the width of a rectangle. */
	double width = 5.0;
	/** The second axis of an ellipse, the height of a rectangle. */
	double height = 5.0;
	/** Degrees, counter-clockwise from east as seen on the map. */
	double angle_deg = 0.0;
	/** Sizes in m on the ground; otherwise in mm on paper. */
	bool metres = false;
	/** The line or area symbol of the shape. */
	const Symbol* symbol = nullptr;
};

/**
 * Makes one shape of the given size at the place: centred there, or for a
 * line starting there ("draw-sized-shape").
 *
 * See the design notes, "Shapes of a given size".
 */
OpResult sizedShape(const Map& map, const SizedShapeParams& params, const MapCoordF& at);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
