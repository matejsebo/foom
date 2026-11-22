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

#ifndef OPENORIENTEERING_CURVE_ENTRY_STYLE_H
#define OPENORIENTEERING_CURVE_ENTRY_STYLE_H

namespace OpenOrienteering {

/**
 * How dragging at a vertex in the path tool gives the curve's handles.
 * 
 * See the design notes, "Curve entry style".
 */
enum class CurveEntryStyle
{
	Legacy = 0,      ///< the drag gives the direction; the handles are a fixed share of the chord
	Illustrator = 1  ///< the drag gives the handle itself
};

/** The style of the setting MapEditor/curve_entry_style. */
CurveEntryStyle curveEntryStyle();

/**
 * The length of a handle: in legacy mode BEZIER_HANDLE_DISTANCE times the
 * chord between the vertex and its neighbour on the curve, in Illustrator
 * mode the length of the drag when it is positive, else the legacy length.
 */
double curveHandleLength(CurveEntryStyle style, double chord, double drag_length);


}  // namespace OpenOrienteering

#endif
