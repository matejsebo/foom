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


#ifndef OPENORIENTEERING_OPS_MIRROR_H
#define OPENORIENTEERING_OPS_MIRROR_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of mirror().
 */
struct MirrorParams : public OpParams
{
	/**
	 * The angle of the axis in degrees, counter-clockwise on paper.
	 *
	 * 0 is a horizontal axis, which swaps top and bottom; 90 is a vertical
	 * axis, which swaps left and right.
	 */
	double axis_angle = 90.0;
	
	/** Keep the inputs and add the outputs, instead of replacing the inputs. */
	bool duplicate = false;
};


/**
 * Reflects the objects about an axis through the centre of their bounding box.
 *
 * Every coordinate is reflected, flags kept. Each path is then reversed, so
 * the left and right sides of asymmetric line symbols stay where the eye
 * expects them. The rotation of point and text objects and the pattern
 * rotation of paths become 2a - r. The outputs carry the inputs' symbol,
 * tags and identity; without params.duplicate the inputs are consumed.
 * An empty input is an error.
 *
 * See the design notes, "Mirror".
 */
OpResult mirror(const Map& map, const std::vector<const Object*>& in, const MirrorParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
