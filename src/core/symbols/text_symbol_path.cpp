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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

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

namespace {

/**
 * Distances along a flattened path.
 *
 * They are true lengths or, in the legacy spacing which symbols read from OCD files use, of
 * every straight piece the longer of its two sides plus half the shorter (see
 * the design notes).
 */
class PathMeasure
{
public:
	PathMeasure(const PathCoordVector& coords, bool legacy)
	: coords(coords)
	{
		distances.reserve(coords.size());
		lengths.reserve(coords.size());
		distances.push_back(0.0);
		lengths.push_back(0.0);
		for (std::size_t i = 1; i < coords.size(); ++i)
		{
			auto const dx = std::abs(coords[i].pos.x() - coords[i-1].pos.x());
			auto const dy = std::abs(coords[i].pos.y() - coords[i-1].pos.y());
			auto const length = std::hypot(dx, dy);
			lengths.push_back(lengths.back() + length);
			distances.push_back(distances.back() + (legacy ? qMax(dx, dy) + qMin(dx, dy) / 2 : length));
		}
	}
	
	double total() const { return distances.back(); }
	
	/** The point at a distance; before the start and after the end, on the straight continuation of the path. */
	MapCoordF at(double distance) const
	{
		auto const i = piece(distance);
		auto const span = distances[i+1] - distances[i];
		auto const t = span > 0.0 ? (distance - distances[i]) / span : 0.0;
		return coords[i].pos + (coords[i+1].pos - coords[i].pos) * t;
	}
	
	/** The true length of the path up to a distance, which is clamped to the path. */
	double lengthAt(double distance) const
	{
		distance = qBound(0.0, distance, total());
		auto const i = piece(distance);
		auto const span = distances[i+1] - distances[i];
		auto const t = span > 0.0 ? (distance - distances[i]) / span : 0.0;
		return lengths[i] + (lengths[i+1] - lengths[i]) * t;
	}
	
private:
	/** The piece which holds a distance; the first or the last piece which has a length for a distance off the path. */
	std::size_t piece(double distance) const
	{
		auto const last = distances.size() - 2;
		auto i = std::size_t(0);
		if (distance > 0.0)
		{
			auto const upper = std::upper_bound(begin(distances), end(distances), distance);
			i = qMin(std::size_t(std::distance(begin(distances), upper)) - 1, last);
		}
		while (i > 0 && !(distances[i+1] > distances[i]))
			--i;
		while (i < last && !(distances[i+1] > distances[i]))
			++i;
		return i;
	}
	
	const PathCoordVector& coords;
	std::vector<double> distances;
	std::vector<double> lengths;
};

}  // namespace


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
	if (part.path_coords.size() < 2)
		return layout;
	// With the legacy spacing every length of this layout, the path's included, is a length of that measure.
	PathMeasure const measure(part.path_coords, legacy_path_spacing);
	auto const path_length = legacy_path_spacing ? measure.total() : double(part.length());
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
	// Where a letter begins: with kerning, the text up to and with the letter less the letter's own advance.
	std::vector<double> origin(std::size_t(n) + 1);
	for (int i = 0; i < n; ++i)
		origin[std::size_t(i)] = advance[std::size_t(i) + 1] - font_metrics.horizontalAdvance(text.mid(i, 1)) / scaling;
	origin[std::size_t(n)] = advance[std::size_t(n)];
	auto const total = advance.back();
	layout.text_advance = total;
	
	auto const h_mode = path_alignment & 0x03;
	auto const v_mode = path_alignment & 0x0c;
	auto start = 0.0;
	auto gap = 0.0;
	// A text longer than its path: in legacy mode the letters at its head which fit are aligned.
	auto fitting = total;
	if (legacy_path_spacing && total > path_length)
	{
		fitting = 0.0;
		for (int i = 1; i <= n && advance[std::size_t(i)] <= path_length; ++i)
			fitting = advance[std::size_t(i)];
	}
	switch (h_mode)
	{
	case 1:
		start = (path_length - fitting) / 2;
		break;
	case 2:
		start = path_length - fitting;
		break;
	case 3:
		if (n > 1)
			gap = (path_length - total) / (n - 1);
		break;
	default:
		break;
	}
	// Middle and top are taken by the height of the capitals.
	auto const dy = v_mode == 4 ? font_metrics.capHeight() / 2 : (v_mode == 8 ? font_metrics.capHeight() : 0.0);
	layout.overflow = total > path_length + 1e-9;
	layout.box_bottom = -dy / scaling;
	layout.box_top = (font_metrics.ascent() - dy) / scaling;
	layout.caret_offsets.reserve(std::size_t(n) + 1);
	for (int i = 0; i <= n; ++i)
	{
		// Halfway across the gap between two glyphs.
		auto const gaps = i == 0 ? 0.0 : (i == n ? double(n - 1) : i - 0.5);
		auto const offset = qBound(0.0, start + advance[std::size_t(i)] + gaps * gap, path_length);
		layout.caret_offsets.push_back(legacy_path_spacing ? measure.lengthAt(offset) : offset);
	}
	
	if (legacy_path_spacing)
	{
		// A letter stands on the chord from the place where its advance begins to the place where it ends.
		for (int i = 0; i < n; ++i)
		{
			auto const from = start + origin[std::size_t(i)] + i * gap;
			auto const to = start + advance[std::size_t(i) + 1] + i * gap;
			if (from < -1e-9 || from > path_length - 1e-6)
			{
				layout.overflow = true;
				continue;
			}
			auto const p = measure.at(from);
			auto const q = measure.at(to);
			layout.glyph_centres.push_back((p + q) / 2);
			if (text.at(i).isSpace() || p == q)
				continue;
	
			QPainterPath glyph;
			glyph.addText(0.0, 0.0, font, QString(text.at(i)));
			QTransform transform;
			transform.translate(p.x(), p.y());
			transform.rotate(qRadiansToDegrees(std::atan2(q.y() - p.y(), q.x() - p.x())));
			transform.scale(1.0 / scaling, 1.0 / scaling);
			transform.translate(0.0, dy);
			layout.outline.addPath(transform.map(glyph));
		}
		layout.outline.setFillRule(Qt::WindingFill);
		return layout;
	}
	
	auto const begin = SplitPathCoord::begin(part.path_coords);
	for (int i = 0; i < n; ++i)
	{
		auto const glyph_advance = advance[std::size_t(i) + 1] - origin[std::size_t(i)];
		auto const centre = start + (origin[std::size_t(i)] + advance[std::size_t(i) + 1]) / 2 + i * gap;
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
