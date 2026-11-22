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


#ifndef FOOM_OPS_GRADIENT_H
#define FOOM_OPS_GRADIENT_H

#include <vector>

#include <QColor>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of gradientFill().
 */
struct GradientParams : public OpParams
{
	QColor from_color = Qt::white;
	QColor to_color = Qt::black;
	/** The direction in degrees: 0 from left to right, counterclockwise. */
	double angle = 0;
	/** Take the direction and the ends from the first open line of the input. */
	bool use_line = false;
	/** Keep the areas behind the images instead of consuming them. */
	bool keep_areas = false;
	/** The resolution of the images, dots per inch. */
	double dpi = 300;
	/** The largest number of pixels on the longer side. */
	int max_pixels = 4000;
};

/**
 * Makes a linear gradient image object for every input path whose symbol
 * has an area, masked by the area (gradient-create).
 *
 * See the design notes, "Image object", second change.
 */
OpResult gradientFill(const Map& map, const std::vector<const Object*>& in, const GradientParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
