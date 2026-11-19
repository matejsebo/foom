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


#ifndef OPENORIENTEERING_OPS_ALIGN_H
#define OPENORIENTEERING_OPS_ALIGN_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of align().
 */
struct AlignParams : public OpParams
{
	/**
	 * The edge or centre line of the extent to match.
	 *
	 * y grows downwards, so Top is the smallest y. The first three are
	 * horizontal positions, the last three vertical ones.
	 */
	enum Edge
	{
		Left,
		HCentre,
		Right,
		Top,
		VCentre,
		Bottom
	};
	
	Edge edge = Left;
};


/**
 * Parameters of distribute().
 */
struct DistributeParams : public OpParams
{
	enum Direction
	{
		Horizontal,
		Vertical
	};
	
	Direction direction = Horizontal;
};


/**
 * Moves every object but the first so that the named edge or centre of its
 * extent (ops::boundingRect) matches the first object's ("align-...").
 *
 * The first object and objects already in place are neither output nor
 * consumed. The outputs carry the inputs' symbol, tags and identity. Fewer
 * than two objects is an error.
 *
 * See the design notes, "Align and distribute".
 */
OpResult align(const Map& map, const std::vector<const Object*>& in, const AlignParams& params);

/**
 * Spaces the centres of the objects' extents equally along one axis
 * ("distribute-h", "distribute-v").
 *
 * The objects are sorted by centre (a stable sort, so ties keep the input
 * order); the first and the last stay, the others move along the axis. Objects
 * already in place are neither output nor consumed. The outputs carry the
 * inputs' symbol, tags and identity. Fewer than three objects is an error.
 */
OpResult distribute(const Map& map, const std::vector<const Object*>& in, const DistributeParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
