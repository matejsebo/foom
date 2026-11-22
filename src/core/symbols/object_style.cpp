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


#include "object_style.h"

#include <QtGlobal>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/private_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "undo/object_undo.h"


namespace OpenOrienteering {

namespace {

constexpr int min_line_width = 10;   // 1/1000 mm
constexpr int min_font_size = 100;   // 1/1000 mm

int micrometres(double mm)
{
	return qRound(1000 * mm);
}

void readStyle(const Symbol& symbol, ObjectStyle& style)
{
	switch (symbol.getType())
	{
	case Symbol::Line:
		if (!style.has_line)
		{
			style.has_line = true;
			style.line_color = symbol.asLine()->getColor();
			style.line_width = 0.001 * symbol.asLine()->getLineWidth();
		}
		break;
	case Symbol::Area:
		if (!style.has_fill)
		{
			style.has_fill = true;
			style.fill_color = symbol.asArea()->getColor();
		}
		break;
	case Symbol::Text:
		if (!style.has_text)
		{
			style.has_text = true;
			style.text_color = symbol.asText()->getColor();
			style.font_family = symbol.asText()->getFontFamily();
			style.font_size = symbol.asText()->getFontSize();
		}
		break;
	case Symbol::Combined:
		{
			auto const* combined = symbol.asCombined();
			for (int i = 0; i < combined->getNumParts(); ++i)
			{
				if (auto const* part = combined->getPart(i))
					readStyle(*part, style);
			}
		}
		break;
	default:
		break;
	}
}

/** Sets the values on the symbol; parts of a combined symbol must be private. */
void writeStyle(Symbol& symbol, const ObjectStyle& style)
{
	switch (symbol.getType())
	{
	case Symbol::Line:
		if (style.has_line)
		{
			symbol.asLine()->setColor(style.line_color);
			symbol.asLine()->setLineWidth(0.001 * qMax(min_line_width, micrometres(style.line_width)));
		}
		break;
	case Symbol::Area:
		if (style.has_fill)
			symbol.asArea()->setColor(style.fill_color);
		break;
	case Symbol::Text:
		if (style.has_text)
		{
			symbol.asText()->setColor(style.text_color);
			symbol.asText()->setFontFamily(style.font_family);
			symbol.asText()->setFontSize(qMax(min_font_size, micrometres(style.font_size)));
		}
		break;
	case Symbol::Combined:
		{
			auto* combined = symbol.asCombined();
			for (int i = 0; i < combined->getNumParts(); ++i)
			{
				// The parts of a private copy are owned by it (makePrivateCopy()).
				if (auto* part = const_cast<Symbol*>(combined->getPart(i)))
					writeStyle(*part, style);
			}
		}
		break;
	default:
		break;
	}
}

}  // namespace


bool ObjectStyle::operator==(const ObjectStyle& other) const
{
	return has_line == other.has_line
	       && (!has_line || (line_color == other.line_color && micrometres(line_width) == micrometres(other.line_width)))
	       && has_fill == other.has_fill
	       && (!has_fill || fill_color == other.fill_color)
	       && has_text == other.has_text
	       && (!has_text || (text_color == other.text_color && font_family == other.font_family
	                         && micrometres(font_size) == micrometres(other.font_size)));
}


ObjectStyle objectStyleOf(const Symbol& symbol)
{
	ObjectStyle style;
	readStyle(symbol, style);
	return style;
}

std::unique_ptr<Symbol> styledCopy(const Symbol& symbol, const ObjectStyle& style)
{
	auto copy = makePrivateCopy(symbol);
	writeStyle(*copy, style);
	return copy;
}

bool setObjectStyle(Map& map, const ObjectStyle& style)
{
	auto const* old_symbol = sharedPrivateSymbol(map);
	if (!old_symbol)
		return false;
	auto copy = styledCopy(*old_symbol, style);
	if (objectStyleOf(*copy) == objectStyleOf(*old_symbol))
		return false;

	auto* symbol = copy.release();
	map.addSymbol(symbol, map.getNumSymbols());
	auto* part = map.getCurrentPart();
	auto* undo = new SwitchSymbolUndoStep(&map);
	for (auto* object : map.selectedObjects())
	{
		undo->addObject(part->findObjectIndex(object), object->getSymbol());
		object->setSymbol(symbol, true);
		object->update();
	}
	map.setObjectsDirty();
	map.push(undo);
	map.emitSelectionEdited();
	map.emitSelectionChanged();  // the symbols of the selected objects changed
	return true;
}


}  // namespace OpenOrienteering
