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


#ifndef OPENORIENTEERING_OPS_LINE_TEXT_H
#define OPENORIENTEERING_OPS_LINE_TEXT_H

#include <vector>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of lengthenLineText().
 */
struct LengthenLineTextParams : public OpParams
{
	/** Lengthen by a percentage of the path length (true) or by a distance (false). */
	bool by_percent = false;

	/** The distance, mm. */
	double distance = 5.0;

	/** The percentage of the length of the anchor path. */
	double percent = 20.0;
};


/**
 * Lengthens the anchor paths of the line text objects among the inputs
 * ("lengthen-line-text").
 *
 * The extension goes to the end for start alignment, to the start for end
 * alignment, and half to each end otherwise. A straight end segment is
 * moved along itself; a curved one gets a new straight segment along its
 * end tangent. Only the first part changes; closed parts are left alone
 * with a warning. Other inputs are ignored. No line text among the inputs,
 * or an extension not above zero, is an error.
 *
 * See the design notes, "Lengthen line text".
 */
OpResult lengthenLineText(const Map& map, const std::vector<const Object*>& in, const LengthenLineTextParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
