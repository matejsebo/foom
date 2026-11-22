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


#include "text_symbol.h"

#include <cmath>
#include <cstddef>

#include <QtGlobal>
#include <QtMath>
#include <QChar>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QString>
#include <QTransform>

#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/renderables/path_text_renderable.h"
#include "core/renderables/renderable.h"
#include "core/symbols/path_text_layout.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

PathTextLayout TextSymbol::layoutPathText(const TextObject* text_object) const
{
	// the design notes, "Text on a path".
	PathTextLayout layout;
	auto const& coords = text_object->getRawCoordinateVector();
	if (coords.size() < 2)
		return layout;
	
	PathObject anchor_path(nullptr, coords);
	anchor_path.updatePathCoords();
	if (anchor_path.parts().empty())
		return layout;
	auto const& part = anchor_path.parts().front();
	auto const path_length = double(part.length());
	layout.path_length = path_length;
	if (!(path_length > 0.0))
		return layout;
	
	auto text = text_object->getText();
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	auto const n = text.size();  // foom: an empty text still gets its cursor position
	
	auto const& font = getQFont();
	auto const& font_metrics = getFontMetrics();
	auto const scaling = calculateInternalScaling();  // text units per mm
	std::vector<double> advance(std::size_t(n) + 1);
	for (int i = 0; i <= n; ++i)
		advance[std::size_t(i)] = font_metrics.horizontalAdvance(text.left(i)) / scaling;
	auto const total = advance.back();
	layout.text_advance = total;
	
	auto const h_mode = path_alignment & 0x03;
	auto const v_mode = path_alignment & 0x0c;
	auto start = 0.0;
	auto gap = 0.0;
	switch (h_mode)
	{
	case 1:
		start = (path_length - total) / 2;
		break;
	case 2:
		start = path_length - total;
		break;
	case 3:
		if (n > 1)
			gap = (path_length - total) / (n - 1);
		break;
	default:
		break;
	}
	auto const dy = v_mode == 4 ? font_metrics.ascent() / 2 : (v_mode == 8 ? font_metrics.ascent() : 0.0);
	layout.overflow = total > path_length + 1e-9;
	layout.box_bottom = -dy / scaling;
	layout.box_top = (font_metrics.ascent() - dy) / scaling;
	layout.caret_offsets.reserve(std::size_t(n) + 1);
	for (int i = 0; i <= n; ++i)
	{
		// Halfway across the gap between two glyphs.
		auto const gaps = i == 0 ? 0.0 : (i == n ? double(n - 1) : i - 0.5);
		layout.caret_offsets.push_back(qBound(0.0, start + advance[std::size_t(i)] + gaps * gap, path_length));
	}
	
	auto const begin = SplitPathCoord::begin(part.path_coords);
	for (int i = 0; i < n; ++i)
	{
		auto const glyph_advance = advance[std::size_t(i) + 1] - advance[std::size_t(i)];
		auto const centre = start + (advance[std::size_t(i)] + advance[std::size_t(i) + 1]) / 2 + i * gap;
		if (centre < -1e-9 || centre > path_length + 1e-9)
		{
			layout.overflow = true;
			continue;
		}
		auto const split = SplitPathCoord::at(PathCoord::length_type(qBound(0.0, centre, path_length)), begin);
		auto const tangent = split.tangentVector();
		layout.glyph_centres.push_back(split.pos);
		if (text.at(i).isSpace())
			continue;
		
		QPainterPath glyph;
		glyph.addText(0.0, 0.0, font, QString(text.at(i)));
		QTransform transform;
		transform.translate(split.pos.x(), split.pos.y());
		transform.rotate(qRadiansToDegrees(std::atan2(tangent.y(), tangent.x())));
		transform.scale(1.0 / scaling, 1.0 / scaling);
		transform.translate(-glyph_advance * scaling / 2, dy);
		layout.outline.addPath(transform.map(glyph));
	}
	layout.outline.setFillRule(Qt::WindingFill);
	return layout;
}

void TextSymbol::createPathTextRenderables(const TextObject* text_object, ObjectRenderables& output) const
{
	auto const layout = layoutPathText(text_object);
	if (layout.outline.isEmpty())
		return;
	if (color)
		output.insertRenderable(new PathTextRenderable(color, layout.outline, 0.0));
	if (framing && framing_color)
	{
		if (framing_mode == LineFraming && framing_line_half_width > 0)
		{
			output.insertRenderable(new PathTextRenderable(framing_color, layout.outline, 2 * 0.001 * framing_line_half_width));
		}
		else if (framing_mode == ShadowFraming)
		{
			QTransform shift;
			shift.translate(0.001 * framing_shadow_x_offset, 0.001 * framing_shadow_y_offset);
			output.insertRenderable(new PathTextRenderable(framing_color, shift.map(layout.outline), 0.0));
		}
	}
}


}  // namespace OpenOrienteering
