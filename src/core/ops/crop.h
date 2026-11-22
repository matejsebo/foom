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

#ifndef FOOM_OPS_CROP_H
#define FOOM_OPS_CROP_H

#include <vector>

#include <QRectF>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class PathObject;

namespace ops {

/**
 * Parameters of crop().
 */
struct CropParams : public OpParams
{
	/** The cropping shapes; open parts are closed by joining their ends. */
	std::vector<const PathObject*> shapes;
	
	/** The cropping rectangle in map coordinates (mm), used when shapes is empty. */
	QRectF rect;
	
	/** Remove what lies inside the region instead of what lies outside. */
	bool hole = false;
	
	/** Remove the shapes. */
	bool delete_shapes = false;
};


/**
 * Crops the inputs to the union of the shapes, or to the rectangle ("crop").
 *
 * Areas are clipped, lines cut where they cross the region's edge, points
 * and texts kept or removed by their first coordinate. The pieces replace
 * an input; the shapes themselves are never cropped.
 *
 * See the design notes, "Crop".
 */
OpResult crop(const Map& map, const std::vector<const Object*>& in, const CropParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
