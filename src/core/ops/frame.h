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


#ifndef OPENORIENTEERING_OPS_FRAME_H
#define OPENORIENTEERING_OPS_FRAME_H

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Symbol;

namespace ops {

struct FrameParams : public OpParams
{
	/** The line or area symbol of the frame. */
	const Symbol* symbol = nullptr;
	/** How far the frame lies outside the map's extent, in mm on paper. */
	double margin_mm = 0.0;
};

/**
 * Makes a closed rectangle around the extent of the map ("draw-frame").
 *
 * See the design notes, "Frame around the map".
 */
OpResult frameAroundMap(const Map& map, const FrameParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
