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


#include "course_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>

#include <QtMath>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QLatin1Char>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QStringList>
#include <QUuid>

#include "core/map.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_edit.h"

namespace OpenOrienteering {

namespace {

/// What is taken off the extent of a symbol for the gap of a connection line, in mm.
constexpr double gap_inset = 0.175;

/// How far a number stands off at the sides, in mm.
constexpr double side_offset = 1.5;

/// The sizes of a number when the map has no text symbol for it: Arial of 4 mm.
constexpr double plain_em = 4.0;
constexpr double plain_advance = 0.556;
constexpr double plain_descent = 0.212;
constexpr double plain_above = 1.023;
constexpr double plain_line_width = 0.35;

double distance(const QPointF& a, const QPointF& b)
{
	return std::hypot(b.x() - a.x(), b.y() - a.y());
}

/// The direction from a to b in degrees, from east, anticlockwise.
/// The y axis of the map points down.
double directionTo(const QPointF& a, const QPointF& b)
{
	return qRadiansToDegrees(std::atan2(-(b.y() - a.y()), b.x() - a.x()));
}

/// The unit vector of a direction, in map coordinates.
QPointF unit(double degrees)
{
	auto const radians = qDegreesToRadians(degrees);
	return { std::cos(radians), -std::sin(radians) };
}

/// A direction in (-180, 180].
double normalized(double degrees)
{
	degrees = std::fmod(degrees, 360.0);
	if (degrees > 180.0)
		degrees -= 360.0;
	else if (degrees <= -180.0)
		degrees += 360.0;
	return degrees;
}

bool isSymbolNumber(const Symbol* symbol, int number)
{
	return symbol->getNumberComponent(0) == number && symbol->getNumberComponent(1) <= 0;
}


/// An item of a course with a place in the map.
struct Stop
{
	const CourseItem* item = nullptr;
	const Object* object = nullptr;
	/// Where the item is entered and where it is left.
	QPointF entry;
	QPointF exit;
	/// The length of a marked route along its path, in mm.
	double path = 0.0;
	/// How far a connection line stays away, in mm.
	double gap = 0.0;
	double extent = 0.0;
	/// A map exchange or flip lies between this stop and the one before.
	bool after_break = false;
};

struct Stops
{
	std::vector<Stop> stops;
	/// The course has a variation.
	bool has_variation = false;
	/// Every item which names an object has its place.
	bool complete = true;
};

Stops findStops(const Map& map, const CourseData& data, const Course& course)
{
	Stops result;
	auto const line_distance = std::max(0.0, data.options.lineDistance());
	auto after_break = false;
	for (auto const& item : course.items)
	{
		switch (item.type)
		{
		case CourseItem::Start:
		case CourseItem::Control:
		case CourseItem::Finish:
		case CourseItem::MarkedRoute:
			break;
		case CourseItem::MapExchange:
		case CourseItem::MapFlip:
			after_break = true;
			continue;
		case CourseItem::TextBlock:
		case CourseItem::CrossingPoint:
		case CourseItem::Passage:
			continue;
		default:
			if (isVariationItem(item.type))
				result.has_variation = true;
			continue;
		}
		
		auto const* course_object = data.findObject(item.code);
		auto const* object = course_object && !course_object->object.isNull() ? map.findObject(course_object->object) : nullptr;
		if (!object || object->getRawCoordinateVector().empty())
		{
			result.complete = false;
			continue;
		}
		
		Stop stop;
		stop.item = &item;
		stop.object = object;
		stop.after_break = after_break;
		after_break = false;
		auto const& coords = object->getRawCoordinateVector();
		stop.entry = MapCoordF(coords.front());
		if (item.type == CourseItem::MarkedRoute && object->getType() == Object::Path)
		{
			auto const* path = object->asPath();
			path->update();
			stop.exit = MapCoordF(coords.back());
			stop.path = path->getPaperLength();
			stop.gap = line_distance;
		}
		else
		{
			stop.exit = stop.entry;
			stop.extent = courseSymbolExtent(object->getSymbol());
			stop.gap = std::max(0.0, stop.extent - gap_inset) + line_distance;
		}
		result.stops.push_back(stop);
	}
	return result;
}

/// The objects of the overrides of a course for a number (to is empty) or a leg.
std::vector<const Object*> overrideObjects(const Map& map, const CourseData& data, const QString& course,
                                           CourseOverride::What what, const QString& from, const QString& to)
{
	std::vector<const Object*> objects;
	for (auto const& override : data.overrides)
	{
		if (override.what != what || override.course != course || override.code_from != from)
			continue;
		if (what == CourseOverride::Line && override.code_to != to)
			continue;
		if (auto const* object = override.object.isNull() ? nullptr : map.findObject(override.object))
			objects.push_back(object);
	}
	return objects;
}


/// The sizes of the box of a number, in mm.
struct NumberBox
{
	double width = 0.0;
	double below = 0.0;  ///< from the base line down
	double above = 0.0;  ///< from the base line up
};

NumberBox numberBox(const TextSymbol* symbol, const QString& text)
{
	NumberBox box;
	if (!symbol)
	{
		box.width = plain_advance * plain_em * text.size();
		box.below = plain_descent * plain_em;
		box.above = plain_above * plain_em;
		return box;
	}
	auto const& metrics = symbol->getFontMetrics();
	auto const scaling = symbol->calculateInternalScaling();
	auto const em = symbol->getFontSize();
	auto const ascent = metrics.ascent() / scaling;
	auto const descent = metrics.descent() / scaling;
	box.width = metrics.horizontalAdvance(text) / scaling;
	box.below = descent;
	// The box reaches above the ascent by what the cell of the font
	// exceeds its size, the internal leading.
	box.above = ascent + std::max(0.0, ascent + descent - em);
	return box;
}

/// Puts the box of a number at the point p by the direction in which it lies.
/// Returns the box in map coordinates.
QRectF placeNumberBox(const QPointF& p, double direction, const NumberBox& box)
{
	auto const height = box.below + box.above;
	auto const a = std::abs(direction);
	auto left = 0.0;
	if (a < 30.0)
		left = p.x() + side_offset;
	else if (a < 60.0)
		left = p.x();
	else if (a <= 120.0)
		left = p.x() - box.width / 2;
	else if (a <= 150.0)
		left = p.x() - box.width;
	else
		left = p.x() - side_offset - box.width;
	
	// The y axis of the map points down: the upper side has the lesser y.
	auto top = 0.0;
	if (a < 30.0 || a > 150.0)
		top = p.y() - height / 2;
	else if (direction > 0.0)
		top = p.y() - height;  // the lower side at p
	else
		top = p.y();           // the upper side at p
	return { left, top, box.width, height };
}

}  // namespace



double courseSymbolExtent(const Symbol* symbol)
{
	if (!symbol)
		return 0.0;
	if (symbol->getType() == Symbol::Line)
		return 0.0005 * symbol->asLine()->getLineWidth();
	if (symbol->getType() != Symbol::Point)
		return 0.0;
	
	auto const* point = symbol->asPoint();
	auto extent = 0.0;
	if (point->getOuterColor() && point->getOuterWidth() > 0)
		extent = 0.001 * (point->getInnerRadius() + point->getOuterWidth());
	else if (point->getInnerColor())
		extent = 0.001 * point->getInnerRadius();
	
	for (int i = 0; i < point->getNumElements(); ++i)
	{
		auto const* element_symbol = point->getElementSymbol(i);
		auto const* element = point->getElementObject(i);
		auto reach = 0.0;
		for (auto const& coord : element->getRawCoordinateVector())
			reach = std::max(reach, std::hypot(coord.x(), coord.y()));
		switch (element_symbol->getType())
		{
		case Symbol::Point:
			extent = std::max(extent, reach + courseSymbolExtent(element_symbol));
			break;
		case Symbol::Line:
			extent = std::max(extent, reach + 0.0005 * element_symbol->asLine()->getLineWidth());
			break;
		default:
			extent = std::max(extent, reach);
			break;
		}
	}
	return extent;
}


const LineSymbol* courseLineSymbol(const Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Line && isSymbolNumber(symbol, 705))
			return symbol->asLine();
	}
	return nullptr;
}


const TextSymbol* courseNumberSymbol(const Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Text && isSymbolNumber(symbol, 704))
			return symbol->asText();
	}
	return nullptr;
}



CourseLength measureCourse(const Map& map, const CourseData& data, const Course& course)
{
	CourseLength length;
	auto const found = findStops(map, data, course);
	if (found.has_variation || !found.complete)
		return length;
	
	auto const& stops = found.stops;
	auto paper = 0.0;
	for (std::size_t i = 0; i < stops.size(); ++i)
	{
		paper += stops[i].path;
		if (i == 0)
			continue;
		// Not counted: from a marked route with which the course begins to the start.
		auto const leading_route = i == 1
		                           && stops[0].item->type == CourseItem::MarkedRoute
		                           && stops[1].item->type == CourseItem::Start;
		if (!leading_route)
			paper += distance(stops[i - 1].exit, stops[i].entry);
	}
	length.known = true;
	length.measured_m = paper * map.getScaleDenominator() / 1000.0;
	length.total_m = length.measured_m + course.extra_km * 1000.0;
	return length;
}


QString formatCourseLength(double metres)
{
	auto const rounded = std::floor(metres / 10.0 + 0.5) / 100.0;
	return QString::number(rounded, 'f', 3) + QLatin1String(" km");
}



CourseDrawing::CourseDrawing() = default;
CourseDrawing::CourseDrawing(CourseDrawing&&) noexcept = default;
CourseDrawing& CourseDrawing::operator=(CourseDrawing&&) noexcept = default;
CourseDrawing::~CourseDrawing() = default;


CourseDrawing drawCourse(const Map& map, const CourseData& data, const QString& course_name)
{
	CourseDrawing drawing;
	auto const* course = data.findCourse(course_name);
	if (!course)
		return drawing;
	auto const found = findStops(map, data, *course);
	if (found.has_variation)
		return drawing;
	drawing.drawn = true;
	
	auto const& stops = found.stops;
	auto const* line_symbol = courseLineSymbol(map);
	auto const* number_symbol = courseNumberSymbol(map);
	auto const with_objects = line_symbol && number_symbol;
	
	// The legs.
	for (std::size_t i = 1; i < stops.size(); ++i)
	{
		auto const& from = stops[i - 1];
		auto const& to = stops[i];
		if (to.after_break)
			continue;
		
		CourseLegDrawing leg;
		leg.from = from.item->code;
		leg.to = to.item->code;
		auto const overrides = overrideObjects(map, data, course->name, CourseOverride::Line, leg.from, leg.to);
		if (!overrides.empty())
		{
			leg.overridden = true;
			for (auto const* object : overrides)
				drawing.objects.emplace_back(object->duplicate());
		}
		else
		{
			auto const length = distance(from.exit, to.entry);
			if (length - from.gap - to.gap > 0.0)
			{
				auto const along = (to.entry - from.exit) / length;
				leg.begin = MapCoordF(from.exit + along * from.gap);
				leg.end = MapCoordF(to.entry - along * to.gap);
				leg.has_line = true;
				if (with_objects)
				{
					auto* path = new PathObject(line_symbol);
					path->addCoordinate(MapCoord(leg.begin));
					path->addCoordinate(MapCoord(leg.end));
					drawing.objects.emplace_back(path);
				}
			}
		}
		drawing.legs.push_back(leg);
	}
	
	// The starts: turned to where the next item is entered.
	for (std::size_t i = 0; i < stops.size(); ++i)
	{
		if (stops[i].item->type != CourseItem::Start)
			continue;
		CourseStartDrawing start;
		start.code = stops[i].item->code;
		if (i + 1 < stops.size() && !stops[i + 1].after_break)
			start.angle = normalized(directionTo(stops[i].exit, stops[i + 1].entry) - 90.0);
		drawing.starts.push_back(start);
	}
	
	// The numbers: the controls are counted through the course; a control
	// has one text, placed by the neighbours of its first visit.
	auto const full_stop = data.options.fullStopAfterNumber();
	auto const number_distance = std::max(0.0, data.options.numberDistance());
	QStringList codes;
	std::vector<QStringList> numbers_of_code;
	std::vector<std::size_t> first_visit;
	auto count = 0;
	for (std::size_t i = 0; i < stops.size(); ++i)
	{
		if (stops[i].item->type != CourseItem::Control)
			continue;
		auto number = QString::number(++count);
		if (full_stop)
			number += QLatin1Char('.');
		auto index = codes.indexOf(stops[i].item->code);
		if (index < 0)
		{
			index = codes.size();
			codes.push_back(stops[i].item->code);
			numbers_of_code.emplace_back();
			first_visit.push_back(i);
		}
		numbers_of_code[std::size_t(index)].push_back(number);
	}
	
	for (int n = 0; n < codes.size(); ++n)
	{
		auto const i = first_visit[std::size_t(n)];
		auto const& stop = stops[i];
		CourseNumberDrawing number;
		number.code = codes[n];
		number.text = numbers_of_code[std::size_t(n)].join(QLatin1Char('/'));
		
		// The free direction.
		auto const has_previous = i > 0 && !stop.after_break;
		auto const has_next = i + 1 < stops.size() && !stops[i + 1].after_break;
		if (has_previous && has_next)
		{
			auto a = directionTo(stop.entry, stops[i - 1].exit);
			auto b = directionTo(stop.entry, stops[i + 1].entry);
			if (a > b)
				std::swap(a, b);
			auto const middle = (a + b) / 2;
			number.direction = normalized(b - a > 180.0 ? middle : middle + 180.0);
		}
		else if (has_previous)
		{
			number.direction = normalized(directionTo(stop.entry, stops[i - 1].exit) + 180.0);
		}
		else if (has_next)
		{
			number.direction = normalized(directionTo(stop.entry, stops[i + 1].entry) + 180.0);
		}
		else
		{
			number.direction = 90.0;
		}
		
		auto const overrides = overrideObjects(map, data, course->name, CourseOverride::Number, number.code, {});
		auto const* moved = overrides.empty() ? nullptr : overrides.front();
		if (moved && moved->getType() == Object::Text)
		{
			number.overridden = true;
			auto* copy = moved->duplicate()->asText();
			copy->setText(number.text);
			number.anchor = copy->getAnchorCoordF();
			auto const box = numberBox(copy->getSymbol() ? copy->getSymbol()->asText() : nullptr, number.text);
			number.box = { number.anchor.x(), number.anchor.y() - box.above, box.width, box.below + box.above };
			drawing.objects.emplace_back(copy);
		}
		else
		{
			auto const box = numberBox(number_symbol, number.text);
			auto const p = stop.entry + unit(number.direction) * (stop.extent + number_distance);
			number.box = placeNumberBox(p, number.direction, box);
			number.anchor = MapCoordF(number.box.left(), number.box.bottom() - box.below);
			if (with_objects)
			{
				auto* text = new TextObject(number_symbol);
				text->setAnchorPosition(number.anchor);
				text->setHorizontalAlignment(TextObject::AlignLeft);
				text->setVerticalAlignment(TextObject::AlignBaseline);
				text->setText(number.text);
				drawing.objects.emplace_back(text);
			}
		}
		drawing.numbers.push_back(number);
	}
	
	// Without the two symbols there are no objects but the copies of
	// overrides, and these are left out as well: the drawing is then plain.
	if (!with_objects)
		drawing.objects.clear();
	return drawing;
}



void paintCourseDrawing(QPainter* painter, Map* map, const CourseDrawing& drawing, const QRectF& bounding_box, qreal scaling)
{
	if (!drawing.drawn)
		return;
	
	if (!drawing.objects.empty())
	{
		MapRenderables renderables(map);
		for (auto const& object : drawing.objects)
		{
			object->update();
			renderables.insertRenderablesOfObject(object.get());
		}
		RenderConfig config = { *map, bounding_box, scaling, RenderConfig::Tool, 1.0 };
		renderables.draw(painter, config);
		for (auto const& object : drawing.objects)
			renderables.removeRenderablesOfObject(object.get(), false);
		return;
	}
	
	auto const purple = QColor(160, 51, 240);
	painter->save();
	painter->setPen(QPen(purple, plain_line_width, Qt::SolidLine, Qt::FlatCap));
	painter->setBrush(Qt::NoBrush);
	for (auto const& leg : drawing.legs)
	{
		if (leg.has_line)
			painter->drawLine(QLineF(leg.begin, leg.end));
	}
	
	// The text is laid out at a size at which fonts are exact, and scaled down.
	QFont font(QStringLiteral("Arial"));
	font.setPixelSize(256);
	auto const factor = plain_em / 256.0;
	painter->setPen(Qt::NoPen);
	painter->setBrush(purple);
	for (auto const& number : drawing.numbers)
	{
		QPainterPath glyphs;
		glyphs.addText(0.0, 0.0, font, number.text);
		painter->save();
		painter->translate(number.anchor);
		painter->scale(factor, factor);
		painter->drawPath(glyphs);
		painter->restore();
	}
	painter->restore();
}


}  // namespace OpenOrienteering
