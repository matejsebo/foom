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


#ifndef OPENORIENTEERING_PATH_TEXT_LAYOUT_H
#define OPENORIENTEERING_PATH_TEXT_LAYOUT_H

#include <vector>

#include <QPainterPath>

#include "core/map_coord.h"

namespace OpenOrienteering {


/**
 * The layout of a text object along its anchor path.
 *
 * See the design notes, "Text on a path".
 */
struct PathTextLayout
{
	/** The glyph outlines in map coordinates (mm). */
	QPainterPath outline;
	/** The centre of every glyph's advance on the path, drawn glyphs only. */
	std::vector<MapCoordF> glyph_centres;
	/** The length of the path, mm. */
	double path_length = 0.0;
	/** The advance of the whole text, mm. */
	double text_advance = 0.0;
	/** True when the text does not fit the path. */
	bool overflow = false;
};


}  // namespace OpenOrienteering

#endif
