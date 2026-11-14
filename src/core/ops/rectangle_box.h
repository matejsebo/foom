/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_OPS_RECTANGLE_BOX_H
#define OPENORIENTEERING_OPS_RECTANGLE_BOX_H

#include <QString>

#include "core/map_coord.h"
#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Symbol;
class TextSymbol;

namespace ops {

/** The corners of a box; the box may be rotated. */
struct RectangleBoxGeometry
{
	MapCoordF top_left;
	MapCoordF top_right;
	MapCoordF bottom_left;
};


struct RectangleBoxParams : public OpParams
{
	/** The symbol of the frame; a line symbol. */
	const Symbol* frame = nullptr;
	/** The corner radius in mm; 0 for sharp corners. */
	double corner_radius = 0.0;
	/** The symbol of the grid lines; the frame symbol when nullptr. */
	const Symbol* grid = nullptr;
	/** The cell size in mm; 0 for no grid. */
	double cell_width = 0.0;
	double cell_height = 0.0;
	/** Number the cells. */
	bool numbering = false;
	/** Count from the bottom-left cell instead of the top-left one. */
	bool number_from_bottom = false;
	/** The symbol of the number texts. */
	const TextSymbol* number_text = nullptr;
	/** The count of cells at the end which show unnumbered_text instead of a number. */
	int unnumbered_cells = 0;
	QString unnumbered_text;
	/** The group number given to every output object, 0 for none. */
	int group = 0;
};


/**
 * Generates the objects of a rectangle box ("draw-rectangle-box"): the frame,
 * the grid lines and the number texts.
 *
 * See the design notes, "Rectangle objects".
 */
OpResult rectangleBox(const Map& map, const RectangleBoxGeometry& geometry, const RectangleBoxParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
