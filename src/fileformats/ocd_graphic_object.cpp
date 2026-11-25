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

#include "ocd_graphic_object.h"

#include <QtMath>
#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>
#include <QStringRef>
#include <QVector>

#include "core/map.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"

namespace OpenOrienteering {

OcdLayoutStyle parseOcdLayoutStyle(const QString& object_string)
{
	// the design notes, "Graphic and layout objects in OCD files".
	OcdLayoutStyle style;
	auto const fields = object_string.splitRef(QLatin1Char('\t'));
	if (fields.isEmpty())
		return style;
	style.font = fields.front().toString();
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (field.size() < 2)
			continue;
		auto ok = false;
		auto const value = field.mid(1).toDouble(&ok);
		if (!ok)
			continue;
		switch (field.at(0).unicode())
		{
		case 'o':
			if (value >= 0.0 && value <= 100.0)
				style.opacity = value / 100.0;
			break;
		case 's':
			if (value > 0.0)
				style.size_pt = value;
			break;
		case 'b':
			style.bold = !qIsNull(value);
			break;
		case 'i':
			style.italic = !qIsNull(value);
			break;
		case 'a':
			if (value >= 0.0 && value <= 3.0)
				style.alignment = qRound(value);
			break;
		default:
			;  // not seen
		}
	}
	return style;
}


MapColorCmyk ocdLayoutCmyk(quint32 color_field)
{
	return MapColorCmyk((color_field & 0xff) / 255.0f,
	                    ((color_field >> 8) & 0xff) / 255.0f,
	                    ((color_field >> 16) & 0xff) / 255.0f,
	                    ((color_field >> 24) & 0xff) / 255.0f);
}


QString ocdLayoutColorName(const MapColorCmyk& cmyk, double opacity)
{
	auto name = QStringLiteral("Layout C%1 M%2 Y%3 K%4")
	            .arg(qRound(100 * cmyk.c)).arg(qRound(100 * cmyk.m)).arg(qRound(100 * cmyk.y)).arg(qRound(100 * cmyk.k));
	if (opacity < 0.995)
		name += QStringLiteral(", %1 %").arg(qRound(100 * opacity));
	return name;
}


const MapColor* ocdLayoutColor(Map& map, const MapColorCmyk& cmyk, double opacity)
{
	auto const name = ocdLayoutColorName(cmyk, opacity);
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == name)
			return map.getColor(i);
	}
	auto* color = new MapColor(name, 0);
	color->setCmyk(cmyk);
	color->setOpacity(float(opacity));
	map.addColor(color, 0);
	return color;
}


std::unique_ptr<Symbol> makeOcdGraphicSymbol(int object_type, const MapColor* color, int line_width, const OcdLayoutStyle* style, const QString& name)
{
	std::unique_ptr<Symbol> symbol;
	switch (object_type)
	{
	case 2:
		{
			auto line = std::make_unique<LineSymbol>();
			line->setColor(color);
			line->setLineWidth(line_width / 100.0);
			symbol = std::move(line);
		}
		break;
	case 3:
		{
			auto area = std::make_unique<AreaSymbol>();
			area->setColor(color);
			symbol = std::move(area);
		}
		break;
	case 4:
		if (style)
		{
			auto text = std::make_unique<TextSymbol>();
			text->setColor(color);
			text->setFontFamily(style->font.isEmpty() ? QStringLiteral("Arial") : style->font);
			// Points to thousandths of a millimetre, as for OCD text symbols.
			text->setFontSize(qRound(1000 * (style->size_pt > 0.0 ? style->size_pt : 12.0) / 72.0 * 25.4));
			text->setBold(style->bold);
			text->setItalic(style->italic);
			symbol = std::move(text);
		}
		break;
	default:
		;  // points and formatted texts were not seen
	}
	if (symbol)
	{
		symbol->setName(name);
		symbol->setPrivate(true);
	}
	return symbol;
}


bool isOcdGraphicSymbol(const Symbol* symbol, OcdGraphicStyle* style)
{
	if (!symbol || !symbol->isPrivate())
		return false;
	OcdGraphicStyle found;
	switch (symbol->getType())
	{
	case Symbol::Line:
		{
			auto const* line = symbol->asLine();
			if (line->isDashed() || line->hasBorder() || line->getCapStyle() == LineSymbol::PointedCap
			    || line->getStartSymbol() || line->getMidSymbol() || line->getMidSymbol2()
			    || line->getEndSymbol() || line->getDashSymbol() || line->getTaperMode() != 0)
				return false;
			found.color = line->getColor();
			found.line_width = qRound(line->getLineWidth() / 10.0);
			if (found.line_width <= 0 || found.line_width > 65535)
				return false;
		}
		break;
	case Symbol::Area:
		{
			auto const* area = symbol->asArea();
			if (area->getNumFillPatterns() > 0)
				return false;
			found.color = area->getColor();
		}
		break;
	default:
		return false;
	}
	if (!found.color)
		return false;
	if (style)
		*style = found;
	return true;
}


quint32 ocdLayoutColorField(const MapColorCmyk& cmyk)
{
	auto const byte = [](float value) { return quint32(qBound(0, qRound(255 * value), 255)); };
	return byte(cmyk.c) | (byte(cmyk.m) << 8) | (byte(cmyk.y) << 16) | (byte(cmyk.k) << 24);
}


QString ocdLayoutObjectString(const OcdLayoutStyle& style, bool text)
{
	auto string = text ? style.font : QString();
	string += QStringLiteral("\to%1").arg(qBound(0, qRound(100 * style.opacity), 100));
	if (text)
	{
		string += QStringLiteral("\ts%1\tb%2\ti%3\ta%4")
		          .arg(style.size_pt, 0, 'f', 1).arg(style.bold ? 1 : 0).arg(style.italic ? 1 : 0).arg(style.alignment);
	}
	return string;
}


QString ocdLayoutListEntry(int object_type, const QString& text, int place)
{
	auto name = object_type == 2 ? QStringLiteral("Line object")
	          : object_type == 3 ? QStringLiteral("Area object")
	          : QStringLiteral("Text object");
	if (object_type == 4)
	{
		// The first line of the text, without what would end the field.
		auto line = text.section(QLatin1Char('\n'), 0, 0).simplified();
		line.truncate(40);
		if (!line.isEmpty())
			name += QStringLiteral(" (%1)").arg(line);
	}
	return name + QStringLiteral("\tr0\ts1\tn%1").arg(place);
}


}  // namespace OpenOrienteering
