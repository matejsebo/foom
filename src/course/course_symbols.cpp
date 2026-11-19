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


#include "course_symbols.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"

namespace OpenOrienteering {

namespace {

/// What a symbol of upstream's set becomes.
struct Target
{
	int first = -1;
	int second = -1;
	Symbol::CourseRole role = Symbol::NoCourseRole;
	int columns = 0;
	/// For a row of upstream's set: the half width, in mm, of the middle of it which is kept; 0 for the whole symbol.
	int middle = 0;
	/// A name which replaces upstream's.
	const char* name = nullptr;
};

/**
 * The number of a direction in OCD course files (1 north, 2 west,
 * 3 south, 4 east, 5 north-west, 6 south-west, 7 south-east,
 * 8 north-east) for upstream's, which counts clockwise from north.
 */
int directionNumber(int clockwise)
{
	static const int numbers[] = { 0, 1, 8, 4, 7, 3, 6, 2, 5 };
	return clockwise >= 1 && clockwise <= 8 ? numbers[clockwise] : 0;
}

/**
 * Says what a symbol of upstream's set becomes; false for a symbol which
 * is left out. The table is that of the design notes,
 * "FOOM's course symbols".
 */
bool targetOf(const Symbol& symbol, Target& target)
{
	auto const a = symbol.getNumberComponent(0);
	auto const b = symbol.getNumberComponent(1);
	auto const c = symbol.getNumberComponent(2);
	auto const d_and_e = Symbol::DescriptionColumnD | Symbol::DescriptionColumnE;
	
	auto const description = [&target](int first, int second, int columns) {
		target.first = first;
		target.second = second;
		target.role = Symbol::DescriptionSymbol;
		target.columns = columns;
		return true;
	};
	auto const plain = [&target](int first, int second, Symbol::CourseRole role = Symbol::NoCourseRole, const char* name = nullptr) {
		target.first = first;
		target.second = second;
		target.role = role;
		target.name = name;
		return true;
	};
	
	switch (a)
	{
	case 0:
		if (b == 1 && c >= 1 && c <= 4)
		{
			// Northern, eastern, southern, western.
			static const int numbers[] = { 0, 101, 104, 103, 102 };
			return description(0, numbers[c], Symbol::DescriptionColumnC);
		}
		if (b == 2 && c >= 1 && c <= 4)
		{
			// North-eastern, south-eastern, south-western, north-western.
			static const int numbers[] = { 0, 208, 207, 206, 205 };
			return description(0, numbers[c], Symbol::DescriptionColumnC);
		}
		if (b >= 3 && b <= 5 && c < 0)
			return description(0, b * 100, Symbol::DescriptionColumnC);
		return false;
		
	case 1:
	case 2:
	case 3:
	case 4:
	case 6:
		return b >= 1 && c < 0 && description(a, b, d_and_e);
		
	case 5:
		if (b < 1 || c >= 0)
			return false;
		if (b <= 20)
			return description(5, b, d_and_e);
		if (b == 23)
			return description(5, 21, d_and_e);  // building pass through
		if (b == 24)
			return description(5, 22, d_and_e);  // stairway
		return false;
		
	case 7:
		return b < 0 && description(7, 1, d_and_e);
		
	case 8:
		return b >= 1 && c < 0 && description(8, b, Symbol::DescriptionColumnE);
		
	case 9:
		// The size of the feature.
		return b == 1 && plain(19, 5, Symbol::NoCourseRole, "Description: size");
		
	case 10:
		return b >= 1 && b <= 2 && c < 0 && description(10, b, Symbol::DescriptionColumnF);
		
	case 11:
		if (c >= 1 && c <= 8)
		{
			auto hundred = 0;
			switch (b)
			{
			case 1: case 2: case 3: case 4: case 5: case 6:
				hundred = b * 100;  // side, edge, part, corner inside, corner outside, tip
				break;
			case 8:
				hundred = 700;      // end
				break;
			case 14:
				hundred = 120;      // foot
				break;
			default:
				return false;
			}
			return description(12, hundred + directionNumber(c), Symbol::DescriptionColumnG);
		}
		if (c >= 0)
			return false;
		switch (b)
		{
		case 7:   return description(11, 1, Symbol::DescriptionColumnF | Symbol::DescriptionColumnG);  // bend
		case 9:   return description(12, 8, Symbol::DescriptionColumnG);   // upper part
		case 10:  return description(12, 9, Symbol::DescriptionColumnG);   // lower part
		case 11:  return description(12, 10, Symbol::DescriptionColumnG);  // top
		case 13:  return description(12, 11, Symbol::DescriptionColumnG);  // foot
		case 12:  return description(12, 13, Symbol::DescriptionColumnG);  // beneath
		case 15:  return description(12, 14, Symbol::DescriptionColumnG);  // between
		default:  return false;
		}
		
	case 12:
		if (c >= 0)
			return false;
		switch (b)
		{
		case 1:   return description(13, 1, Symbol::DescriptionColumnH);  // first aid post
		case 2:   return description(13, 2, Symbol::DescriptionColumnH);  // refreshment point
		case 3:   return description(13, 4, Symbol::DescriptionColumnH);  // radio or TV control
		case 4:   return description(13, 5, Symbol::DescriptionColumnH);  // control check
		default:  return false;
		}
		
	case 13:
		if (b == 0)
			return plain(19, 4, Symbol::NoCourseRole, "Description: distance");
		if (b == 3 || b == 4)
		{
			// The middle of the row of the crossing point and of the passage.
			plain(15, b);
			target.middle = b == 3 ? 6 : 12;
			return true;
		}
		return false;
		
	case 99:
		if (b == 7)
			return plain(19, 1, Symbol::NoCourseRole, "Description: number");
		if (b == 8)
			return plain(19, 2, Symbol::NoCourseRole, "Description: code");
		return false;
		
	case 701:  return plain(701, -1, Symbol::CourseStart);
	case 702:  return plain(703, -1, Symbol::CourseControl);
	case 703:  return plain(704, -1);
	case 704:  return plain(705, -1, Symbol::NoCourseRole, "Course line");
	case 705:  return plain(707, -1, Symbol::CourseMarkedRoute);
	case 706:  return plain(706, -1, Symbol::CourseFinish);
	case 707:  return plain(708, -1);
	case 708:  return plain(710, -1);
	case 709:
		if (b < 0)
			return plain(709, -1);
		if (b == 1)
			return plain(709, 4);
		if (b == 2)
			return plain(709, 2);
		return false;
	case 710:  return plain(709, 5);
	case 711:
	case 712:
	case 713:
	case 799:
		return plain(a, -1);
		
	default:
		return false;
	}
}


/// A name of upstream's set without the mark of the column before it: "|D| Terrace" is "Terrace".
QString cleanName(const QString& name)
{
	auto clean = name.trimmed();
	if (clean.size() > 3 && clean[0] == QLatin1Char('|') && clean[2] == QLatin1Char('|'))
		clean = clean.mid(3).trimmed();
	if (clean == QLatin1String("Knol"))
		clean = QStringLiteral("Knoll");
	return clean;
}


/// The scale of the overprint symbols for a map: see the specification, "Size by scale".
double overprintScale(unsigned int scale_denominator)
{
	if (scale_denominator <= 5000)
		return 1.0;
	if (scale_denominator < 15000)
		return 1.25;
	return 5.0 / 6.0;
}


const MapColor* colourNamed(const Map& map, const char* name)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == QLatin1String(name))
			return map.getColor(i);
	}
	return nullptr;
}


/// Makes the symbols which upstream's set has not, in the map of the set.
class Maker
{
public:
	Maker(Map& set, const MapColor* purple, const MapColor* black, const MapColor* white, std::set<const Symbol*>& ours)
	: set(set)
	, purple(purple)
	, black(black)
	, white(white)
	, ours(ours)
	{}
	
	PointSymbol* point(int first, int second, const char* name)
	{
		auto* symbol = new PointSymbol();
		symbol->setInnerRadius(0);
		add(symbol, first, second, name);
		return symbol;
	}
	
	/// A line of a point symbol, through places in mm with the y axis down.
	void stroke(PointSymbol* symbol, const MapColor* colour, double width, std::initializer_list<QPointF> places, bool closed = false)
	{
		MapCoordVector coords;
		for (auto const& place : places)
			coords.emplace_back(place.x(), place.y());
		element(symbol, colour, width, std::move(coords), closed);
	}
	
	/// A curve of a point symbol from a place to a place, with two handles.
	void curve(PointSymbol* symbol, const MapColor* colour, double width, QPointF from, QPointF handle_1, QPointF handle_2, QPointF to)
	{
		MapCoordVector coords;
		coords.emplace_back(from.x(), from.y());
		coords.back().setCurveStart(true);
		coords.emplace_back(handle_1.x(), handle_1.y());
		coords.emplace_back(handle_2.x(), handle_2.y());
		coords.emplace_back(to.x(), to.y());
		element(symbol, colour, width, std::move(coords), false);
	}
	
	/// A ring around the middle of a point symbol, as an element.
	void ring(PointSymbol* symbol, const MapColor* colour, int inner_radius, int width)
	{
		auto* circle = new PointSymbol();
		circle->setInnerRadius(inner_radius);
		circle->setOuterWidth(width);
		circle->setOuterColor(colour);
		auto* object = new PointObject(circle);
		object->setPosition(MapCoord(0, 0));
		symbol->addElement(symbol->getNumElements(), object, circle);
	}
	
	void line(int first, int second, const char* name, const MapColor* colour, double width)
	{
		auto* symbol = new LineSymbol();
		symbol->setLineWidth(width);
		symbol->setColor(colour);
		// Rules which meet at a corner fill it.
		symbol->setCapStyle(LineSymbol::SquareCap);
		symbol->setJoinStyle(LineSymbol::MiterJoin);
		add(symbol, first, second, name);
	}
	
	TextSymbol* text(int first, int second, const char* name, const MapColor* colour, int size, bool bold)
	{
		auto* symbol = new TextSymbol();
		symbol->setFontFamily(QStringLiteral("Arial"));
		symbol->setFontSize(size);
		symbol->setBold(bold);
		symbol->setColor(colour);
		add(symbol, first, second, name);
		return symbol;
	}
	
	void area(int first, int second, const char* name, const MapColor* colour)
	{
		auto* symbol = new AreaSymbol();
		symbol->setColor(colour);
		add(symbol, first, second, name);
	}
	
	void make()
	{
		auto const pen = 0.3;
		
		// The parts of the rows of marked routes and of the finish, after
		// upstream's rows 13.1 to 14.3: its ring, its dash and its chevron.
		auto* dash = point(16, 1, "Marked route: dash");
		stroke(dash, black, pen, { { -1.4, 0.0 }, { 1.4, 0.0 } });
		auto* funnel = point(16, 2, "Marked route: funnel");
		stroke(funnel, black, pen, { { -0.9, -1.8 }, { 0.9, 0.0 }, { -0.9, 1.8 } });
		auto* no_tapes = point(16, 3, "No tapes");
		stroke(no_tapes, black, pen, { { 0.9, -1.8 }, { -0.9, 0.0 }, { 0.9, 1.8 } });
		
		// The triangle is that of upstream's row of the map exchange.
		for (auto second : { 1, 5 })
		{
			auto* start = point(17, second, second == 1 ? "Description: start"
			                                            : "Description: start of a marked route");
			stroke(start, black, pen, { { 0.0, -2.0 }, { -2.0, 1.5 }, { 2.0, 1.5 } }, true);
		}
		auto* control = point(17, 2, "Description: control");
		control->setInnerRadius(1950);
		control->setOuterWidth(300);
		control->setOuterColor(black);
		auto* finish = point(17, 3, "Description: finish");
		finish->setInnerRadius(1950);
		finish->setOuterWidth(300);
		finish->setOuterColor(black);
		ring(finish, black, 1300, 300);
		auto* arrow = point(17, 4, "Description: arrow");
		stroke(arrow, black, pen, { { -0.9, -1.8 }, { 0.9, 0.0 }, { -0.9, 1.8 } });
		
		// The map flip is no symbol of the IOF: a sheet, and an arrow which turns over it.
		auto* flip = point(15, 6, "Map flip");
		stroke(flip, black, pen, { { -1.5, -0.3 }, { 1.5, -0.3 }, { 1.5, 2.5 }, { -1.5, 2.5 } }, true);
		curve(flip, black, pen, { -1.7, -1.1 }, { -1.7, -2.9 }, { 1.7, -2.9 }, { 1.7, -1.1 });
		stroke(flip, black, pen, { { 1.0, -1.8 }, { 1.7, -1.1 }, { 2.4, -1.8 } });
		
		// The rules, the texts and the background of the block.
		line(18, 1, "Description: thick rule", black, 0.5);
		line(18, 2, "Description: thin rule", black, 0.25);
		text(19, 3, "Description: title", black, 3500, true);
		text(19, 6, "Text description: title", black, 3000, true);
		auto* row = text(19, 7, "Text description: row", black, 3000, false);
		// The number, the code and the text of a control stand in columns.
		row->setCustomTabs({ 8000, 18000 });
		area(20, 1, "Description: background", white);
		
		// The objects which are no part of a course: where the descriptions
		// stand, and the texts of a course and of a relay.
		auto* place = point(720, -1, "Control descriptions");
		stroke(place, purple, 0.35, { { 6.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 6.0 } });
		stroke(place, purple, 0.35, { { 2.0, 2.0 }, { 5.0, 2.0 } });
		stroke(place, purple, 0.35, { { 2.0, 3.5 }, { 5.0, 3.5 } });
		place->setCourseRole(Symbol::CourseDescription);
		text(721, -1, "Course title", purple, 5000, true)->setCourseRole(Symbol::CourseTitle);
		text(722, -1, "Start number", purple, 5000, true)->setCourseRole(Symbol::CourseStartNumber);
		text(722, 1, "Variant code", purple, 4000, false)->setCourseRole(Symbol::CourseVariantCode);
	}
	
private:
	void add(Symbol* symbol, int first, int second, const char* name)
	{
		symbol->setNumberComponent(0, first);
		symbol->setNumberComponent(1, second);
		symbol->setNumberComponent(2, -1);
		symbol->setName(QString::fromLatin1(name));
		set.addSymbol(symbol, set.getNumSymbols());
		ours.insert(symbol);
	}
	
	void element(PointSymbol* symbol, const MapColor* colour, double width, MapCoordVector coords, bool closed)
	{
		auto* pen = new LineSymbol();
		pen->setLineWidth(width);
		pen->setColor(colour);
		pen->setCapStyle(LineSymbol::RoundCap);
		pen->setJoinStyle(LineSymbol::RoundJoin);
		auto* object = new PathObject(pen, std::move(coords));
		if (closed)
			object->closeAllParts();
		symbol->addElement(symbol->getNumElements(), object, pen);
	}
	
	Map& set;
	const MapColor* purple;
	const MapColor* black;
	const MapColor* white;
	std::set<const Symbol*>& ours;
};


/// Leaves of a row of upstream's set its middle: the elements which do not reach farther than the half width.
void keepMiddle(PointSymbol& row, int half_width_mm)
{
	auto const limit = qint32(half_width_mm * 1000);
	for (int i = row.getNumElements() - 1; i >= 0; --i)
	{
		auto const& coords = row.getElementObject(i)->getRawCoordinateVector();
		auto const inside = std::all_of(begin(coords), end(coords), [limit](const MapCoord& coord) {
			return qAbs(coord.nativeX()) <= limit;
		});
		if (!inside)
			row.deleteElement(i);
	}
}


/**
 * Makes FOOM's course symbols in the map of upstream's set, and returns
 * them; the other symbols of the set stay in that map and are not ours.
 */
std::set<const Symbol*> makeCourseSymbols(Map& set, unsigned int scale_denominator)
{
	std::set<const Symbol*> ours;
	auto const* purple = colourNamed(set, "Purple");
	auto const* black = colourNamed(set, "Black for control descriptions");
	auto const* white = colourNamed(set, "White below framing");
	if (!purple || !black || !white)
		return ours;
	
	auto const upstream_symbols = set.getNumSymbols();
	for (int i = 0; i < upstream_symbols; ++i)
	{
		auto* symbol = set.getSymbol(i);
		Target target;
		if (!targetOf(*symbol, target))
			continue;
		
		if (target.middle > 0)
		{
			// The row stays what it is; its middle is a symbol of its own.
			if (symbol->getType() != Symbol::Point)
				continue;
			auto copy = duplicate(*static_cast<const PointSymbol*>(symbol));
			keepMiddle(*copy, target.middle);
			symbol = copy.release();
			set.addSymbol(symbol, set.getNumSymbols());
		}
		
		symbol->setNumberComponent(0, target.first);
		symbol->setNumberComponent(1, target.second);
		symbol->setNumberComponent(2, -1);
		symbol->setName(target.name ? QString::fromLatin1(target.name) : cleanName(symbol->getPlainTextName()));
		symbol->setCourseRole(target.role, target.columns);
		if (target.first >= 701 && target.first <= 799)
			symbol->scale(overprintScale(scale_denominator));
		ours.insert(symbol);
	}
	
	Maker(set, purple, black, white, ours).make();
	return ours;
}


/// Whether a map has a symbol with the number of a symbol. A component
/// which is not there is 0: the 703 of a map file is the 703.000 of an OCD file.
bool hasNumber(const Map& map, const Symbol& symbol)
{
	auto const same = [](int lhs, int rhs) { return std::max(lhs, 0) == std::max(rhs, 0); };
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* other = map.getSymbol(i);
		if (other->getNumberComponent(0) == symbol.getNumberComponent(0)
		    && same(other->getNumberComponent(1), symbol.getNumberComponent(1))
		    && same(other->getNumberComponent(2), symbol.getNumberComponent(2)))
			return true;
	}
	return false;
}


/// Puts the colours which a map did not have before above those which it had, each group in its order.
void raiseNewColours(Map& map, const std::vector<const MapColor*>& colours_before)
{
	std::vector<MapColor*> order;
	std::vector<MapColor*> old;
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto* colour = map.getMapColor(i);
		auto const was_there = std::find(begin(colours_before), end(colours_before), colour) != end(colours_before);
		(was_there ? old : order).push_back(colour);
	}
	if (order.empty() || old.empty())
		return;
	order.insert(end(order), begin(old), end(old));
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		if (map.getMapColor(int(i)) != order[i])
			map.setColor(order[i], int(i));
	}
	map.setColorsDirty();
}

}  // namespace



QString courseSymbolSetPath()
{
	return QStringLiteral("data:/symbol sets/10000/Course_Design_10000.omap");
}


CourseSymbolsReport addCourseSymbols(Map& map, const QString& symbol_set_path)
{
	CourseSymbolsReport report;
	auto const path = symbol_set_path.isEmpty() ? courseSymbolSetPath() : symbol_set_path;
	Map set;
	if (!QFileInfo::exists(path) || !set.loadFrom(path))
		return report;
	auto const ours = makeCourseSymbols(set, map.getScaleDenominator());
	if (ours.empty())
		return report;
	report.loaded = true;
	
	// The symbols go into the map by their numbers.
	set.sortSymbols(Symbol::lessByNumber);
	std::vector<bool> filter(std::size_t(set.getNumSymbols()), false);
	auto wanted = 0;
	for (int i = 0; i < set.getNumSymbols(); ++i)
	{
		auto const* symbol = set.getSymbol(i);
		if (ours.count(symbol) == 0)
			continue;
		if (hasNumber(map, *symbol))
		{
			++report.taken;
			continue;
		}
		filter[std::size_t(i)] = true;
		++wanted;
	}
	if (wanted == 0)
		return report;
	
	// The colours which the map has; those which come with the symbols are put above them.
	std::vector<const MapColor*> colours_before;
	for (int i = 0; i < map.getNumColors(); ++i)
		colours_before.push_back(map.getColor(i));
	
	set.setScaleDenominator(map.getScaleDenominator());
	auto const symbols_before = map.getNumSymbols();
	map.importMap(set, Map::MinimalSymbolImport, &filter, -1, false);
	report.added = map.getNumSymbols() - symbols_before;
	
	raiseNewColours(map, colours_before);
	map.setSymbolsDirty();
	return report;
}


}  // namespace OpenOrienteering
