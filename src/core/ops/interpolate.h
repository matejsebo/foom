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

#ifndef FOOM_OPS_INTERPOLATE_H
#define FOOM_OPS_INTERPOLATE_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;

namespace ops {

/**
 * Parameters of interpolate().
 */
struct InterpolateParams : public OpParams
{
	/** The number of objects between the two inputs. */
	int count = 1;
	
	/** The symbol of the new objects; nullptr for the first input's. */
	const Symbol* symbol = nullptr;
	
	/** Fit curves through the simplified result (step 4), with toCurve()'s defaults. */
	bool curve_fit = false;
};


/**
 * Makes count paths between two paths, both open or both closed, by blending
 * points equally spaced by arc length; consumes nothing ("interpolate").
 *
 * See the design notes, "Interpolate".
 */
OpResult interpolate(const Map& map, const std::vector<const Object*>& in, const InterpolateParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
