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


#include "generator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

#include <QtGlobal>
#include <QtMath>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_part.h"
#include "core/object_groups.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "layout/layout_part.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

const QLatin1String scale_bar_id("gen-scale-bar");
const QLatin1String north_lines_id("gen-north-lines");
const QLatin1String legend_id("gen-legend");

const QLatin1String generator_tag("generator");
const QLatin1String params_tag("params");

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Generator", text);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}


/** What a generator made: objects, and the private symbols they use. */
struct Build
{
	std::vector<Symbol*> symbols;
	std::vector<Object*> objects;
	
	Build() = default;
	Build(const Build&) = delete;
	Build& operator=(const Build&) = delete;
	
	~Build()
	{
		// Only what was not handed over to the map.
		for (auto* object : objects)
			delete object;
		for (auto* symbol : symbols)
			delete symbol;
	}
	
	void handedOver()
	{
		objects.clear();
		symbols.clear();
	}
};

struct Context
{
	const Map& map;
	int exclude_group;  ///< The group which is being rebuilt, or 0.
};


const MapColor* darkestColor(const Map& map)
{
	const MapColor* darkest = nullptr;
	auto lightness = 256;
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		const QColor& rgb = *color;
		auto const value = rgb.lightness();
		if (value < lightness)
		{
			lightness = value;
			darkest = color;
		}
	}
	return darkest;
}

const Symbol* symbolByNumber(const Map& map, const QString& number, Symbol::TypeCombination types)
{
	if (number.isEmpty())
		return nullptr;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if ((symbol->getType() & types) && symbol->getNumberAsString() == number)
			return symbol;
	}
	return nullptr;
}

const Symbol* lineSymbol(const Context& c, const QJsonObject& params, Build& build)
{
	if (auto const* symbol = symbolByNumber(c.map, params.value(QLatin1String("line_symbol")).toString(), Symbol::Line | Symbol::Combined))
		return symbol;
	auto* symbol = new LineSymbol();
	symbol->setName(tr("Layout line"));
	symbol->setLineWidth(0.15);
	symbol->setColor(darkestColor(c.map));
	symbol->setPrivate(true);
	build.symbols.push_back(symbol);
	return symbol;
}

const Symbol* areaSymbol(const Context& c, const QJsonObject& params, Build& build)
{
	if (auto const* symbol = symbolByNumber(c.map, params.value(QLatin1String("area_symbol")).toString(), Symbol::Area | Symbol::Combined))
		return symbol;
	auto* symbol = new AreaSymbol();
	symbol->setName(tr("Layout area"));
	symbol->setColor(darkestColor(c.map));
	symbol->setPrivate(true);
	build.symbols.push_back(symbol);
	return symbol;
}

const TextSymbol* textSymbol(const Context& c, const QJsonObject& params, Build& build)
{
	if (auto const* symbol = symbolByNumber(c.map, params.value(QLatin1String("text_symbol")).toString(), Symbol::Text))
		return symbol->asText();
	auto* symbol = new TextSymbol();
	symbol->setName(tr("Layout text"));
	symbol->setFontSize(2500);
	symbol->setColor(darkestColor(c.map));
	symbol->setPrivate(true);
	build.symbols.push_back(symbol);
	return symbol;
}

PathObject* makeLine(const Symbol* symbol, const QPointF& from, const QPointF& to)
{
	return new PathObject(symbol, MapCoordVector{ MapCoord(from), MapCoord(to) });
}

PathObject* makeRectangle(const Symbol* symbol, const QRectF& box)
{
	MapCoordVector coords { MapCoord(box.topLeft()), MapCoord(box.topRight()), MapCoord(box.bottomRight()), MapCoord(box.bottomLeft()), MapCoord(box.topLeft()) };
	coords.back().setClosePoint(true);
	return new PathObject(symbol, coords);
}

TextObject* makeText(const Symbol* symbol, const QPointF& anchor, const QString& string, TextObject::HorizontalAlignment h_align, TextObject::VerticalAlignment v_align)
{
	auto* text = new TextObject(symbol);
	text->setAnchorPosition(MapCoordF(anchor));
	text->setText(string);
	text->setHorizontalAlignment(h_align);
	text->setVerticalAlignment(v_align);
	return text;
}

/** 10000 as "10 000". */
QString grouped(unsigned int value)
{
	auto string = QString::number(value);
	for (int i = string.length() - 3; i > 0; i -= 3)
		string.insert(i, QLatin1Char(' '));
	return string;
}


// ### Scale bar ###

bool buildScaleBar(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error)
{
	auto const scale = c.map.getScaleDenominator();
	auto length_m = params.value(QLatin1String("length_m")).toDouble();
	if (length_m <= 0)
		length_m = scaleBarAutoLength(scale);
	auto const divisions = qBound(1, params.value(QLatin1String("divisions")).toInt(5), 50);
	auto const length = length_m * 1000.0 / scale;  // mm on paper
	if (length < 1 || length > 2000)
	{
		setError(error, tr("A scale bar of %1 m would be %2 mm long.").arg(length_m).arg(length, 0, 'f', 1));
		return false;
	}
	
	auto const* line = lineSymbol(c, params, build);
	auto const* text = textSymbol(c, params, build);
	constexpr auto height = 1.2;
	auto const x0 = anchor.x();
	auto const y0 = anchor.y();
	auto const step = length / divisions;
	
	if (params.value(QLatin1String("style")).toString() == QLatin1String("ticks"))
	{
		build.objects.push_back(makeLine(line, { x0, y0 + height }, { x0 + length, y0 + height }));
		for (int i = 0; i <= divisions; ++i)
			build.objects.push_back(makeLine(line, { x0 + i * step, y0 }, { x0 + i * step, y0 + height }));
	}
	else
	{
		build.objects.push_back(makeRectangle(line, { x0, y0, length, height }));
		auto const* area = areaSymbol(c, params, build);
		for (int i = 0; i < divisions; i += 2)
			build.objects.push_back(makeRectangle(area, { x0 + i * step, y0, step, height }));
	}
	
	auto const kilometres = length_m >= 2000;
	for (int i = 0; i <= divisions; ++i)
	{
		auto const value = length_m * i / divisions / (kilometres ? 1000 : 1);
		auto label = QString::number(value, 'g', 6);
		if (i == divisions)
			label += kilometres ? tr(" km") : tr(" m");
		build.objects.push_back(makeText(text, { x0 + i * step, y0 - 0.6 }, label, TextObject::AlignHCenter, TextObject::AlignBottom));
	}
	
	auto below = y0 + height + 0.8;
	if (params.value(QLatin1String("show_ratio")).toBool(true))
	{
		build.objects.push_back(makeText(text, { x0 + length / 2, below }, QLatin1String("1:") + grouped(scale), TextObject::AlignHCenter, TextObject::AlignTop));
		below += 1.5 * text->getFontSize();
	}
	auto const contour_interval = params.value(QLatin1String("contour_interval_m")).toDouble();
	if (contour_interval > 0)
		build.objects.push_back(makeText(text, { x0 + length / 2, below }, tr("Contour interval %1 m").arg(contour_interval), TextObject::AlignHCenter, TextObject::AlignTop));
	return true;
}


// ### North lines ###

QPolygonF outlineFromParams(const QJsonObject& params)
{
	QPolygonF polygon;
	for (auto const& point : params.value(QLatin1String("outline")).toArray())
	{
		auto const pair = point.toArray();
		if (pair.size() == 2)
			polygon.append({ pair.at(0).toDouble(), pair.at(1).toDouble() });
	}
	return polygon;
}

/** The bounding box of the objects in the parts with map content, without the group being rebuilt. */
QRectF mapContentExtent(const Context& c)
{
	QRectF extent;
	for (int p = 0; p < c.map.getNumParts(); ++p)
	{
		auto const* part = c.map.getPart(std::size_t(p));
		if (!isMapContent(*part))
			continue;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (c.exclude_group != 0 && object->group() == c.exclude_group)
				continue;
			object->update();
			rectIncludeSafe(extent, object->getExtent());
		}
	}
	return extent;
}

bool buildNorthLines(const Context& c, const QJsonObject& params, const MapCoordF& /*anchor*/, Build& build, QString* error)
{
	auto spacing_m = params.value(QLatin1String("spacing_m")).toDouble();
	if (spacing_m <= 0)
		spacing_m = northLinesDefaultSpacing(c.map);
	auto const spacing = spacing_m * 1000.0 / c.map.getScaleDenominator();  // mm on paper
	if (spacing < 0.5)
	{
		setError(error, tr("North lines %1 m apart would be %2 mm apart on the map.").arg(spacing_m).arg(spacing, 0, 'f', 2));
		return false;
	}
	
	auto outline = outlineFromParams(params);
	if (outline.size() < 3)
	{
		auto const extent = mapContentExtent(c);
		if (!extent.isValid() || extent.isEmpty())
		{
			setError(error, tr("The map has no objects to take the extent from."));
			return false;
		}
		outline = QPolygonF(extent);
	}
	if (outline.isClosed())
		outline.removeLast();
	
	// Up is -y. The lines point at the angle, clockwise from up; across is to their right.
	auto const angle = qDegreesToRadians(params.value(QLatin1String("angle")).toDouble());
	const QPointF along(std::sin(angle), -std::cos(angle));
	const QPointF across(std::cos(angle), std::sin(angle));
	auto lowest = std::numeric_limits<double>::max();
	auto highest = std::numeric_limits<double>::lowest();
	for (auto const& vertex : qAsConst(outline))
	{
		auto const offset = QPointF::dotProduct(across, vertex);
		lowest = std::min(lowest, offset);
		highest = std::max(highest, offset);
	}
	auto const first = std::ceil(lowest / spacing);
	auto const last = std::floor(highest / spacing);
	if (last - first > 5000)
	{
		setError(error, tr("That would be more than 5000 north lines."));
		return false;
	}
	
	const Symbol* symbol = symbolByNumber(c.map, params.value(QLatin1String("line_symbol")).toString(), Symbol::Line | Symbol::Combined);
	if (!symbol)
	{
		for (int i = 0; i < c.map.getNumPublicSymbols() && !symbol; ++i)
		{
			auto const* candidate = c.map.getSymbol(i);
			if (candidate->getType() == Symbol::Line && candidate->getNumberComponent(0) == 601)
				symbol = candidate;
		}
	}
	if (!symbol)
		symbol = lineSymbol(c, {}, build);
	
	for (auto k = first; k <= last; ++k)
	{
		auto const offset = k * spacing;
		// Where the line crosses the outline, as distances along the line.
		std::vector<double> crossings;
		for (int i = 0; i < outline.size(); ++i)
		{
			auto const& a = outline.at(i);
			auto const& b = outline.at((i + 1) % outline.size());
			auto const side_a = QPointF::dotProduct(across, a) - offset;
			auto const side_b = QPointF::dotProduct(across, b) - offset;
			if ((side_a < 0) == (side_b < 0))
				continue;
			auto const point = a + (b - a) * (side_a / (side_a - side_b));
			crossings.push_back(QPointF::dotProduct(along, point));
		}
		std::sort(begin(crossings), end(crossings));
		for (std::size_t i = 0; i + 1 < crossings.size(); i += 2)
		{
			if (crossings[i + 1] - crossings[i] < 0.01)
				continue;
			// The upper end first.
			build.objects.push_back(makeLine(symbol, across * offset + along * crossings[i + 1], across * offset + along * crossings[i]));
		}
	}
	if (build.objects.empty())
	{
		setError(error, tr("No north line falls inside the outline."));
		return false;
	}
	return true;
}


// ### Legend ###

bool buildLegend(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error)
{
	auto const flag = [&params](const char* key, bool fallback) {
		return params.value(QLatin1String(key)).toBool(fallback);
	};
	auto const number = [&params](const char* key, double fallback) {
		return params.value(QLatin1String(key)).toDouble(fallback);
	};
	auto const width = qBound(1.0, number("icon_width", 10), 200.0);
	auto const height = qBound(1.0, number("icon_height", 5), 200.0);
	auto const line_spacing = qBound(0.0, number("line_spacing", 1), 100.0);
	auto const label_width = qBound(5.0, number("label_width", 45), 500.0);
	auto const max_column_height = number("max_column_height", 0);
	auto const show_number = flag("show_number", false);
	auto const* text_symbol = textSymbol(c, params, build);
	auto const text_line = text_symbol->getLineSpacing() * text_symbol->getFontMetrics().lineSpacing() / text_symbol->calculateInternalScaling();
	
	std::vector<bool> in_use;
	if (flag("only_used", false))
		determineMapContentSymbolsInUse(c.map, in_use);  // not the layout, so not a legend's own samples
	
	auto x = anchor.x();
	auto y = anchor.y();
	auto rows_in_column = 0;
	for (int i = 0; i < c.map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = c.map.getSymbol(i);
		if (symbol->isHelperSymbol() || symbol->isPrivate())
			continue;
		if (symbol->isHidden() && !flag("include_hidden", true))
			continue;
		if (!in_use.empty() && !in_use[std::size_t(i)])
			continue;
		
		auto type = symbol->getType();
		if (type == Symbol::Combined)
			type = (symbol->getContainedTypes() & Symbol::Area) ? Symbol::Area : Symbol::Line;
		auto const wanted = (type == Symbol::Point && flag("points", true))
		                    || (type == Symbol::Line && flag("lines", true))
		                    || (type == Symbol::Area && flag("areas", true))
		                    || (type == Symbol::Text && flag("texts", true));
		if (!wanted)
			continue;
		
		// The sample of a line is as long as it needs to show its pattern.
		auto sample_width = width;
		if (symbol->getType() == Symbol::Line)
		{
			auto const* line = symbol->asLine();
			if (line->isDashed())
			{
				auto const dashes = qMax(1, line->getDashesInGroup());
				auto const period = 0.001 * (dashes * line->getDashLength() + (dashes - 1) * line->getInGroupBreakLength() + line->getBreakLength());
				sample_width = std::max(sample_width, period);
			}
			else if (line->getMidSymbol() && !line->getMidSymbol()->isEmpty())
			{
				sample_width = std::max(sample_width, 2 * 0.001 * line->getSegmentLength());
			}
		}
		
		auto label_text = symbol->getPlainTextName();
		if (show_number)
			label_text = symbol->getNumberAsString() + QLatin1Char(' ') + label_text;
		auto* label = new TextObject(text_symbol);
		label->setText(label_text);
		label->setHorizontalAlignment(TextObject::AlignLeft);
		label->setVerticalAlignment(TextObject::AlignVCenter);
		label->setBox(0, 0, label_width, height);
		label->prepareLineInfos();
		auto const row_height = std::max(height, label->getNumLines() * text_line);
		
		if (max_column_height > 0 && rows_in_column > 0 && y + row_height - anchor.y() > max_column_height)
		{
			x += width + label_width + 5;
			y = anchor.y();
			rows_in_column = 0;
		}
		
		const QRectF box(x, y + (row_height - height) / 2, width, height);
		switch (type)
		{
		case Symbol::Point:
			{
				auto* point = new PointObject(symbol);
				point->setPosition(MapCoordF(box.center()));
				build.objects.push_back(point);
			}
			break;
		case Symbol::Line:
			build.objects.push_back(makeLine(symbol, { box.left(), box.center().y() }, { box.left() + sample_width, box.center().y() }));
			break;
		case Symbol::Area:
			build.objects.push_back(makeRectangle(symbol, box));
			break;
		case Symbol::Text:
			build.objects.push_back(makeText(symbol, box.bottomLeft(), QStringLiteral("Abc"), TextObject::AlignLeft, TextObject::AlignBaseline));
			break;
		default:
			break;
		}
		
		auto const label_left = x + std::max(width, sample_width) + 2;
		label->setBox(qRound(1000 * (label_left + label_width / 2)), qRound(1000 * (y + row_height / 2)), label_width, row_height);
		build.objects.push_back(label);
		
		y += row_height + line_spacing;
		++rows_in_column;
	}
	
	if (build.objects.empty())
	{
		setError(error, tr("No symbol passes the filters."));
		return false;
	}
	return true;
}


using BuildFunction = bool (*)(const Context&, const QJsonObject&, const MapCoordF&, Build&, QString*);

BuildFunction buildFunction(const QString& id)
{
	if (id == scale_bar_id)
		return &buildScaleBar;
	if (id == north_lines_id)
		return &buildNorthLines;
	if (id == legend_id)
		return &buildLegend;
	return nullptr;
}

/**
 * Adds what was built to the part, which must be the current part, as the
 * given group, and pushes the undo step for it.
 */
void addBuilt(Map& map, const QString& id, const QJsonObject& params, const MapCoordF& anchor, Build& build, int part_index, int group, CombinedUndoStep* undo)
{
	for (auto* symbol : build.symbols)
		map.addSymbol(symbol, map.getNumSymbols());
	
	auto stored = params;
	auto const first = MapCoordF(build.objects.front()->getRawCoordinateVector().front());
	stored.insert(QLatin1String("ax"), qRound(1000 * (anchor.x() - first.x())) / 1000.0);
	stored.insert(QLatin1String("ay"), qRound(1000 * (anchor.y() - first.y())) / 1000.0);
	auto const json = QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact));
	
	auto* step = new DeleteObjectsUndoStep(&map);
	for (auto* object : build.objects)
	{
		object->setTag(generator_tag, id);
		object->setTag(params_tag, json);
		object->setGroup(group);
		step->addObject(map.addObject(object, part_index));
	}
	build.handedOver();
	undo->push(step);
	map.setObjectGroupName(group, generatorTitle(id));
	map.setObjectsDirty();
}

QPolygonF selectedOutline(const Map& map)
{
	for (auto const* object : map.selectedObjects())
	{
		if (object->getType() != Object::Path)
			continue;
		auto const* path = object->asPath();
		path->update();
		if (path->parts().empty() || !path->parts().front().isClosed())
			continue;
		QPolygonF polygon;
		for (auto const& coord : path->parts().front().path_coords)
			polygon.append(coord.pos);
		if (polygon.isClosed())
			polygon.removeLast();
		return polygon;
	}
	return {};
}

}  // namespace


QStringList generatorIds()
{
	return { scale_bar_id, north_lines_id, legend_id };
}

QString generatorTitle(const QString& id)
{
	if (id == scale_bar_id)
		return tr("Scale bar");
	if (id == north_lines_id)
		return tr("North lines");
	if (id == legend_id)
		return tr("Legend");
	return id;
}

std::vector<GeneratorField> generatorFields(const QString& id)
{
	auto const field = [](const char* key, const QString& label, GeneratorField::Kind kind, double minimum = 0, double maximum = 0, int decimals = 0, const QString& suffix = {}) {
		GeneratorField f;
		f.key = QLatin1String(key);
		f.label = label;
		f.kind = kind;
		f.minimum = minimum;
		f.maximum = maximum;
		f.decimals = decimals;
		f.suffix = suffix;
		return f;
	};
	auto const mm = tr(" mm");
	auto const m = tr(" m");
	std::vector<GeneratorField> fields;
	if (id == scale_bar_id)
	{
		auto style = field("style", tr("Style:"), GeneratorField::Choice);
		style.choices = QStringList{ QStringLiteral("blocks"), QStringLiteral("ticks") };
		style.choice_labels = QStringList{ tr("Alternating blocks"), tr("Line with ticks") };
		fields.push_back(style);
		fields.push_back(field("length_m", tr("Length (0: automatic):"), GeneratorField::Double, 0, 1000000, 0, m));
		fields.push_back(field("divisions", tr("Divisions:"), GeneratorField::Int, 1, 50));
		fields.push_back(field("show_ratio", tr("Show the scale as a ratio"), GeneratorField::Bool));
		fields.push_back(field("contour_interval_m", tr("Contour interval (0: not shown):"), GeneratorField::Double, 0, 1000, 1, m));
		fields.push_back(field("line_symbol", tr("Line symbol (none: automatic):"), GeneratorField::LineSymbol));
		fields.push_back(field("area_symbol", tr("Area symbol (none: automatic):"), GeneratorField::AreaSymbol));
		fields.push_back(field("text_symbol", tr("Text symbol (none: automatic):"), GeneratorField::TextSymbol));
	}
	else if (id == north_lines_id)
	{
		fields.push_back(field("spacing_m", tr("Spacing on the ground (0: default):"), GeneratorField::Double, 0, 100000, 0, m));
		fields.push_back(field("angle", tr("Angle, clockwise from up:"), GeneratorField::Double, -180, 180, 2, tr(" °")));
		auto clip = field("clip", tr("Clip to:"), GeneratorField::Choice);
		clip.choices = QStringList{ QStringLiteral("extent"), QStringLiteral("selection") };
		clip.choice_labels = QStringList{ tr("The extent of the map objects"), tr("The selected closed path") };
		fields.push_back(clip);
		fields.push_back(field("line_symbol", tr("Line symbol (none: automatic):"), GeneratorField::LineSymbol));
	}
	else if (id == legend_id)
	{
		fields.push_back(field("points", tr("Point symbols"), GeneratorField::Bool));
		fields.push_back(field("lines", tr("Line symbols"), GeneratorField::Bool));
		fields.push_back(field("areas", tr("Area symbols"), GeneratorField::Bool));
		fields.push_back(field("texts", tr("Text symbols"), GeneratorField::Bool));
		fields.push_back(field("only_used", tr("Only symbols which are used"), GeneratorField::Bool));
		fields.push_back(field("include_hidden", tr("Include hidden symbols"), GeneratorField::Bool));
		fields.push_back(field("show_number", tr("Show the symbol number"), GeneratorField::Bool));
		fields.push_back(field("icon_width", tr("Sample width:"), GeneratorField::Double, 1, 200, 1, mm));
		fields.push_back(field("icon_height", tr("Sample height:"), GeneratorField::Double, 1, 200, 1, mm));
		fields.push_back(field("line_spacing", tr("Space between rows:"), GeneratorField::Double, 0, 100, 1, mm));
		fields.push_back(field("label_width", tr("Label width:"), GeneratorField::Double, 5, 500, 1, mm));
		fields.push_back(field("max_column_height", tr("Column height (0: one column):"), GeneratorField::Double, 0, 5000, 1, mm));
		fields.push_back(field("text_symbol", tr("Text symbol (none: automatic):"), GeneratorField::TextSymbol));
	}
	return fields;
}

QJsonObject defaultGeneratorParams(const QString& id, const Map& /*map*/)
{
	QJsonObject params;
	auto const set = [&params](const char* key, const QJsonValue& value) {
		params.insert(QLatin1String(key), value);
	};
	if (id == scale_bar_id)
	{
		set("style", QStringLiteral("blocks"));
		set("length_m", 0);
		set("divisions", 5);
		set("show_ratio", true);
		set("contour_interval_m", 0);
		set("line_symbol", QString{});
		set("area_symbol", QString{});
		set("text_symbol", QString{});
	}
	else if (id == north_lines_id)
	{
		set("spacing_m", 0);
		set("angle", 0);
		set("clip", QStringLiteral("extent"));
		set("line_symbol", QString{});
	}
	else if (id == legend_id)
	{
		set("points", true);
		set("lines", true);
		set("areas", true);
		set("texts", true);
		set("only_used", false);
		set("include_hidden", true);
		set("show_number", false);
		set("icon_width", 10);
		set("icon_height", 5);
		set("line_spacing", 1);
		set("label_width", 45);
		set("max_column_height", 0);
		set("text_symbol", QString{});
	}
	return params;
}

double scaleBarAutoLength(unsigned int scale_denominator)
{
	auto const limit = 0.06 * qMax(1u, scale_denominator);  // 60 mm on paper, in metres
	auto best = 0.0;
	for (int exponent = 0; exponent <= 9; ++exponent)
	{
		auto const power = std::pow(10.0, exponent);
		for (auto factor : { 1.0, 2.0, 5.0 })
		{
			if (factor * power <= limit * (1 + 1e-9))
				best = factor * power;
		}
	}
	return best;
}

double northLinesDefaultSpacing(const Map& map)
{
	if (map.symbolSetId().startsWith(QLatin1String("ISSprOM")))
		return 120;
	if (map.symbolSetId().startsWith(QLatin1String("ISOM")))
		return 300;
	return map.getScaleDenominator() < 7500 ? 120 : 300;
}

int runGenerator(Map& map, const QString& id, const QJsonObject& params, const MapCoordF& anchor, QString* error)
{
	auto const build_function = buildFunction(id);
	if (!build_function)
	{
		setError(error, tr("There is no generator %1.").arg(id));
		return 0;
	}
	
	auto used_params = params;
	if (id == north_lines_id && used_params.value(QLatin1String("clip")).toString() == QLatin1String("selection")
	    && outlineFromParams(used_params).size() < 3)
	{
		auto const outline = selectedOutline(map);
		if (outline.size() < 3)
		{
			setError(error, tr("Select a closed path as the outline first."));
			return 0;
		}
		QJsonArray points;
		for (auto const& point : outline)
			points.append(QJsonArray{ qRound(1000 * point.x()) / 1000.0, qRound(1000 * point.y()) / 1000.0 });
		used_params.insert(QLatin1String("outline"), points);
	}
	
	if (map.getNumColors() == 0)
	{
		auto* black = new MapColor(tr("Black"), 0);
		black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
		black->setRgbFromCmyk();
		map.addColor(black, 0);
	}
	
	Build build;
	if (!build_function({ map, 0 }, used_params, anchor, build, error) || build.objects.empty())
		return 0;
	
	auto* undo = new CombinedUndoStep(&map);
	int part_index;
	if (id == north_lines_id)
	{
		part_index = mapContentPartIndex(map);
		if (part_index < 0)
		{
			delete undo;
			setError(error, tr("The map has no part for map objects."));
			return 0;
		}
	}
	else
	{
		part_index = layoutPartIndex(map);
		if (part_index < 0)
		{
			auto* part = new MapPart(layoutPartName(), &map);
			part->setRole(MapPart::Role::Layout);
			map.addPart(part, std::size_t(map.getNumParts()));
			undo->push(new MapPartUndoStep(&map, MapPartUndoStep::RemoveMapPart, part));
			part_index = map.findPartIndex(part);
		}
	}
	
	// The undo step of the new objects refers to the current part.
	auto const current = map.getCurrentPartIndex();
	map.setCurrentPartIndex(std::size_t(part_index));
	auto const group = nextObjectGroupId(map);
	addBuilt(map, id, used_params, anchor, build, part_index, group, undo);
	map.setCurrentPartIndex(current);
	map.push(undo);
	return group;
}

std::vector<int> generatedGroups(const Map& map, bool selected_only)
{
	std::vector<int> groups;
	auto const consider = [&groups](const Object* object) {
		if (object->group() != 0 && !object->getTag(generator_tag).isEmpty()
		    && std::find(begin(groups), end(groups), object->group()) == end(groups))
			groups.push_back(object->group());
	};
	if (selected_only)
	{
		for (auto const* object : map.selectedObjects())
			consider(object);
	}
	else
	{
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			auto const* part = map.getPart(std::size_t(p));
			for (int i = 0; i < part->getNumObjects(); ++i)
				consider(part->getObject(i));
		}
	}
	return groups;
}

std::vector<int> scaleDependentGroups(const Map& map)
{
	std::vector<int> groups;
	for (auto group : generatedGroups(map, false))
	{
		auto const objects = groupObjects(map, group);
		if (!objects.empty() && objects.front()->getTag(generator_tag) == scale_bar_id)
			groups.push_back(group);
	}
	return groups;
}

std::vector<Object*> groupObjects(const Map& map, int group, int* part_index)
{
	std::vector<Object*> objects;
	if (group == 0)
		return objects;
	for (int p = 0; p < map.getNumParts() && objects.empty(); ++p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			if (object->group() == group && !object->getTag(generator_tag).isEmpty())
				objects.push_back(object);
		}
		if (!objects.empty() && part_index)
			*part_index = p;
	}
	return objects;
}

int regenerateGroups(Map& map, const std::vector<int>& groups, QString* error)
{
	auto* undo = new CombinedUndoStep(&map);
	auto const current = map.getCurrentPartIndex();
	auto rebuilt = 0;
	for (auto group : groups)
	{
		auto part_index = -1;
		auto const old_objects = groupObjects(map, group, &part_index);
		if (old_objects.empty())
			continue;
		auto const id = old_objects.front()->getTag(generator_tag);
		auto const params = QJsonDocument::fromJson(old_objects.front()->getTag(params_tag).toUtf8()).object();
		auto const build_function = buildFunction(id);
		if (!build_function)
		{
			setError(error, tr("There is no generator %1.").arg(id));
			continue;
		}
		auto const first = MapCoordF(old_objects.front()->getRawCoordinateVector().front());
		const MapCoordF anchor(first.x() + params.value(QLatin1String("ax")).toDouble(), first.y() + params.value(QLatin1String("ay")).toDouble());
		
		Build build;
		if (!build_function({ map, group }, params, anchor, build, error) || build.objects.empty())
			continue;
		
		// Both undo steps refer to the current part.
		map.setCurrentPartIndex(std::size_t(part_index));
		auto* part = map.getPart(std::size_t(part_index));
		auto* add_step = new AddObjectsUndoStep(&map);
		for (auto* object : old_objects)
			add_step->addObject(part->findObjectIndex(object), object);
		add_step->removeContainedObjects(false);
		undo->push(add_step);
		addBuilt(map, id, params, anchor, build, part_index, group, undo);
		++rebuilt;
	}
	map.setCurrentPartIndex(current);
	if (rebuilt == 0)
	{
		delete undo;
		return 0;
	}
	map.push(undo);
	map.emitSelectionChanged();
	return rebuilt;
}


}  // namespace OpenOrienteering
