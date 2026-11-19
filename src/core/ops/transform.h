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


#ifndef OPENORIENTEERING_OPS_TRANSFORM_H
#define OPENORIENTEERING_OPS_TRANSFORM_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of moveBy().
 */
struct MoveParams : public OpParams
{
	/** The offset in mm of map coordinates, where y grows downwards. */
	double dx = 0.0;
	double dy = 0.0;
	
	/** Keep the inputs and add the outputs, instead of replacing the inputs. */
	bool duplicate = false;
};


/**
 * Parameters of rotateBy().
 */
struct RotateParams : public OpParams
{
	/** The angle in degrees, counter-clockwise on paper. */
	double angle = 0.0;
	
	/** Rotate each object about its own centre instead of the common one. */
	bool each_object = false;
	
	/** Keep the inputs and add the outputs, instead of replacing the inputs. */
	bool duplicate = false;
};


/**
 * Parameters of stretch().
 */
struct StretchParams : public OpParams
{
	/** The factors; neither may be zero. */
	double factor_x = 1.0;
	double factor_y = 1.0;
	
	/** Keep the inputs and add the outputs, instead of replacing the inputs. */
	bool duplicate = false;
};


/**
 * Moves the objects by the offset ("move-by").
 *
 * The outputs carry the inputs' symbol, tags and identity; without
 * params.duplicate the inputs are consumed. An empty input is an error.
 *
 * See the design notes, "Numeric transforms".
 */
OpResult moveBy(const Map& map, const std::vector<const Object*>& in, const MoveParams& params);

/**
 * Rotates the objects about the centre of the bounding box of their
 * coordinates, or each about its own centre ("rotate-by").
 *
 * The rotation of rotatable point and text objects and of rotatable fill
 * patterns turns with the object (Object::rotateAround). The outputs carry
 * the inputs' symbol, tags and identity; without params.duplicate the
 * inputs are consumed. An empty input is an error.
 */
OpResult rotateBy(const Map& map, const std::vector<const Object*>& in, const RotateParams& params);

/**
 * Stretches the objects by the factors about the centre of the bounding box
 * of their coordinates ("stretch").
 *
 * Text boxes are scaled; rotations stay. The outputs carry the inputs'
 * symbol, tags and identity; without params.duplicate the inputs are
 * consumed. An empty input and a zero factor are errors.
 */
OpResult stretch(const Map& map, const std::vector<const Object*>& in, const StretchParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
