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

#ifndef FOOM_OPS_CONVERT_H
#define FOOM_OPS_CONVERT_H

#include <vector>

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class PointSymbol;

namespace ops {

/**
 * Parameters of convertToPoints().
 */
struct ConvertToPointsParams : public OpParams
{
	/** The symbol of the new point objects. */
	const PointSymbol* symbol = nullptr;
	
	/** Added to a text object's anchor (paper mm, y down). */
	MapCoordF text_offset;
};


/**
 * Replaces text objects by points at their anchor plus the offset, and line and
 * area objects by points at their centroid, keeping identity, tags and group;
 * point objects are left alone ("convert-to-point").
 *
 * See the design notes, "Convert objects".
 */
OpResult convertToPoints(const Map& map, const std::vector<const Object*>& in, const ConvertToPointsParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
