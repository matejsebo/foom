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


#ifndef OPENORIENTEERING_PATH_TEXT_RENDERABLE_H
#define OPENORIENTEERING_PATH_TEXT_RENDERABLE_H

#include <QPainterPath>

#include "core/renderables/renderable.h"

class QPainter;

namespace OpenOrienteering {

class MapColor;
struct RenderConfig;


/**
 * Renderable for text on a path: glyph outlines already in map coordinates.
 *
 * With a positive framing width the outline is stroked with a pen of that
 * width (mm) instead of filled.
 *
 * See the design notes, "Text on a path".
 */
class PathTextRenderable : public Renderable
{
public:
	PathTextRenderable(const MapColor* color, const QPainterPath& outline_mm, double framing_width_mm);
	~PathTextRenderable() override;
	
	PainterConfig getPainterConfig(const QPainterPath* clip_path = nullptr) const override;
	void render(QPainter& painter, const RenderConfig& config) const override;
	
private:
	QPainterPath outline;
	double framing_width;
};


}  // namespace OpenOrienteering

#endif
