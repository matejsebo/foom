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


#include "path_text_renderable.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "core/map_color.h"
#include "core/renderables/renderable.h"
#include "settings.h"


namespace OpenOrienteering {

PathTextRenderable::PathTextRenderable(const MapColor* color, const QPainterPath& outline_mm, double framing_width_mm)
: Renderable { color }
, outline { outline_mm }
, framing_width { framing_width_mm }
{
	extent = outline.controlPointRect();
	if (framing_width > 0.0)
		extent.adjust(-framing_width / 2, -framing_width / 2, framing_width / 2, framing_width / 2);
}

PathTextRenderable::~PathTextRenderable() = default;

PainterConfig PathTextRenderable::getPainterConfig(const QPainterPath* clip_path) const
{
	if (framing_width > 0.0)
		return { color_priority, PainterConfig::PenOnly, framing_width, clip_path };
	return { color_priority, PainterConfig::BrushOnly, 0.0, clip_path };
}

void PathTextRenderable::render(QPainter& painter, const RenderConfig& config) const
{
	painter.save();
	auto const disable_antialiasing = config.options.testFlag(RenderConfig::Screen) && !(Settings::getInstance().getSettingCached(Settings::MapDisplay_TextAntialiasing).toBool());
	if (disable_antialiasing)
	{
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, false);
	}
	if (framing_width > 0.0)
	{
		auto pen = painter.pen();
		pen.setJoinStyle(Qt::MiterJoin);
		pen.setMiterLimit(0.5);
		painter.setPen(pen);
	}
	painter.drawPath(outline);
	painter.restore();
}


}  // namespace OpenOrienteering
