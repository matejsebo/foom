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


#include "text_symbol.h"

#include <cstddef>

#include <QtGlobal>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/renderables/renderable_implementation.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"
#include "util/util.h"


// Text symbol properties which symbols read from OCD files need, see the design notes, "Text symbol properties".

namespace OpenOrienteering {

namespace {

/** Returns the bounding box of all lines in text coordinates; false when there is no line. */
bool textBoundingBox(const TextObject* text_object, QRectF& bbox)
{
	auto const num_lines = text_object->getNumLines();
	if (num_lines == 0)
		return false;
	
	auto const* line = text_object->getLineInfo(0);
	bbox = QRectF(line->line_x, line->line_y - line->ascent, line->width, line->ascent + line->descent);
	for (int i = 1; i < num_lines; ++i)
	{
		line = text_object->getLineInfo(i);
		rectInclude(bbox, QRectF(line->line_x, line->line_y - line->ascent, line->width, line->ascent + line->descent));
	}
	return true;
}

}  // namespace


int TextSymbol::qfontWeight(int weight)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return qBound(1, weight, 1000);
#else
	// Qt 5 uses a legacy scale; interpolate between its named weights.
	static const struct { int opentype; int legacy; } table[] = {
	    { 100, QFont::Thin }, { 200, QFont::ExtraLight }, { 300, QFont::Light }, { 400, QFont::Normal },
	    { 500, QFont::Medium }, { 600, QFont::DemiBold }, { 700, QFont::Bold }, { 800, QFont::ExtraBold }, { 900, QFont::Black } };
	if (weight <= table[0].opentype)
		return table[0].legacy;
	for (std::size_t i = 1; i < sizeof(table) / sizeof(table[0]); ++i)
	{
		if (weight <= table[i].opentype)
		{
			auto const& a = table[i - 1];
			auto const& b = table[i];
			return a.legacy + (weight - a.opentype) * (b.legacy - a.legacy) / (b.opentype - a.opentype);
		}
	}
	return QFont::Black;
#endif
}


void TextSymbol::createBoxFramingRenderables(const TextObject* text_object, ObjectRenderables& output) const
{
	QRectF bbox;
	if (!textBoundingBox(text_object, bbox))
		return;
	
	auto const scaling = calculateInternalScaling();
	bbox.adjust(-0.001 * framing_box_left * scaling, -0.001 * framing_box_top * scaling,
	            0.001 * framing_box_right * scaling, 0.001 * framing_box_bottom * scaling);
	
	AreaSymbol area_symbol;
	area_symbol.setColor(framing_color);
	
	MapCoordVector  box_flags(4);
	MapCoordVectorF box_coords(4);
	VirtualPath box_path = { box_flags, box_coords };
	box_flags.back().setHolePoint(true);
	
	auto const transform = text_object->calcTextToMapTransform();
	box_coords[0] = MapCoordF(transform.map(bbox.topLeft()));
	box_coords[1] = MapCoordF(transform.map(bbox.topRight()));
	box_coords[2] = MapCoordF(transform.map(bbox.bottomRight()));
	box_coords[3] = MapCoordF(transform.map(bbox.bottomLeft()));
	box_path.path_coords.update(0);
	output.insertRenderable(new AreaRenderable(&area_symbol, box_path));
}


void TextSymbol::createFrameSymbolRenderables(const TextObject* text_object, ObjectRenderables& output) const
{
	QRectF bbox;
	if (!textBoundingBox(text_object, bbox))
		return;
	
	// The point symbol follows the text's rotation, as a road shield must.
	auto const centre = MapCoordF(text_object->calcTextToMapTransform().map(bbox.center()));
	frame_symbol->createRenderablesScaled(centre, -text_object->getRotation(), output);
}


bool TextSymbol::symbolChangedEvent(const Symbol* old_symbol, const Symbol* new_symbol)
{
	if (!frame_symbol || frame_symbol != old_symbol)
		return false;
	
	frame_symbol = (new_symbol && new_symbol->getType() == Symbol::Point) ? new_symbol->asPoint() : nullptr;
	resetIcon();
	return true;
}


bool TextSymbol::containsSymbol(const Symbol* symbol) const
{
	return frame_symbol && frame_symbol == symbol;
}


bool TextSymbol::loadingFinishedEvent(Map* map)
{
	if (temp_frame_symbol_index < 0)
		return true;
	
	auto const index = temp_frame_symbol_index;
	temp_frame_symbol_index = -1;
	if (index >= map->getNumSymbols())
		return false;
	
	auto const* symbol = map->getSymbol(index);
	frame_symbol = (symbol->getType() == Symbol::Point) ? symbol->asPoint() : nullptr;
	return true;
}


}  // namespace OpenOrienteering
