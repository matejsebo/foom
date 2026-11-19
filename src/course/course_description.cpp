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


#include "course_description.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <utility>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizeF>
#include <QUuid>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "course/course_edit.h"
#include "course/course_render.h"

namespace OpenOrienteering {

namespace {

using Row = CourseDescriptionRow;
using Layout = CourseDescriptionLayout;

// The symbols with which a description is drawn, as number times 1000.
constexpr int symbol_crossing_point = 15003;
constexpr int symbol_passage        = 15004;
constexpr int symbol_map_flip       = 15006;
constexpr int symbol_dash           = 16001;
constexpr int symbol_funnel         = 16002;
constexpr int symbol_no_tapes       = 16003;
constexpr int symbol_start          = 17001;
constexpr int symbol_control        = 17002;
constexpr int symbol_finish         = 17003;
constexpr int symbol_arrow          = 17004;
constexpr int symbol_route_start    = 17005;
constexpr int symbol_thick_rule     = 18001;
constexpr int symbol_thin_rule      = 18002;
constexpr int symbol_first_text     = 19001;  // the styles of the texts follow in their order
constexpr int symbol_background     = 20001;

/// The size of the box for which the symbols are drawn, in mm.
constexpr double design_box = 6.0;

// Lengths in box sizes.
constexpr double column_edges[] = { 0.0, 1.0, 2.2, 3.2, 4.2, 5.2, 6.5, 7.5, 8.5 };
constexpr double block_width = 8.5;
/// Where the texts of the full rows are centred: the rule between D and E.
constexpr double full_row_middle = 4.2;
constexpr double dash_places[] = { 1.16, 1.84, 2.54, 5.94, 6.64, 7.34 };
constexpr double funnel_place = 1.44;
constexpr double margin = 1.0 / 3.0;
constexpr double title_base_line = 0.7;
constexpr double number_base_line = 0.75;
constexpr double size_base_line = 0.725;
// A size of two numbers, as "5/10": the first above a slash, the second below it.
constexpr double size_first_place = 5.575;
constexpr double size_first_base_line = 0.4;
constexpr double size_second_place = 6.12;
constexpr double size_second_base_line = 0.9;
constexpr double slash_begin[] = { 5.3, 0.75 };
constexpr double slash_end[] = { 6.3, 0.26 };
constexpr double class_pitch = 0.5;
constexpr double empty_class_row = 0.4;
constexpr double text_first_line = 0.8;

// What is drawn plainly, when the map has not the symbols.
constexpr double plain_thick = 0.5;
constexpr double plain_thin = 0.25;
/// The sizes of the plain texts in box sizes, by their styles.
constexpr double plain_em[] = { 0.65, 0.6, 0.6, 0.6, 0.42, 0.7, 0.7 };
constexpr bool plain_bold[] = { true, true, true, false, false, true, false };


double distance(const QPointF& a, const QPointF& b)
{
	return std::hypot(b.x() - a.x(), b.y() - a.y());
}

bool isLeftAligned(Layout::Text::Style style)
{
	return style == Layout::Text::TextTitle || style == Layout::Text::TextRow;
}


/// Where an item of a course lies in the map.
struct Place
{
	bool valid = false;
	QPointF entry;
	QPointF exit;
	/// The length of a marked route along its path, in mm.
	double path = 0.0;
};

Place placeOf(const Map& map, const CourseObject* course_object, bool route)
{
	Place place;
	auto const* object = course_object && !course_object->object.isNull() ? map.findObject(course_object->object) : nullptr;
	if (!object || object->getRawCoordinateVector().empty())
		return place;
	
	auto const& coords = object->getRawCoordinateVector();
	place.valid = true;
	place.entry = MapCoordF(coords.front());
	place.exit = place.entry;
	if (route && object->getType() == Object::Path)
	{
		auto const* path = object->asPath();
		path->update();
		place.exit = MapCoordF(coords.back());
		place.path = path->getPaperLength();
	}
	return place;
}

/// The symbol of the map with a number which is given as in the cells.
const Symbol* symbolOfNumber(const Map& map, int number)
{
	auto const first = number / 1000;
	auto const second = number % 1000;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getNumberComponent(0) != first)
			continue;
		auto const component = symbol->getNumberComponent(1);
		if (component == second || (second == 0 && component < 0))
			return symbol;
	}
	return nullptr;
}


/// A marked route which waits for the item behind it.
struct PendingRoute
{
	bool active = false;
	QString code;
	QString text;
	bool funnel = false;
	Place place;
	/// An item with a place is before the route.
	bool has_previous = false;
	/// The straight from the place before to the beginning of the route, in mm.
	double before = 0.0;
};


/// Lays the rows of a description out in blocks.
class BlockBuilder
{
public:
	BlockBuilder(Layout& layout, const CourseOptions& options)
	: layout(layout)
	, box(layout.box)
	, group(options.thickerRule() == 1 ? 3 : options.thickerRule() == 2 ? 4 : 0)
	, max_height(options.maximumRows() > 0 ? options.maximumRows() * layout.box : 0.0)
	, with_background(options.whiteBackground())
	{}
	
	void head(const CourseDescription& description, int title_mode)
	{
		auto top = beginRow(1.0, false, -1);
		text(Layout::Text::Title, description.title, full_row_middle, top + title_base_line * box);
		
		if (title_mode == 0)
		{
			// The classes in the first cell, one below the other.
			auto names = description.classes;
			if (names.isEmpty())
				names.push_back(description.course);
			auto const height = 1.0 + class_pitch * (names.size() - 1);
			top = beginRow(height, false, -1);
			headCells(top, height);
			for (int i = 0; i < names.size(); ++i)
				text(Layout::Text::Title, names[i], cellMiddle(0, 3), top + (title_base_line + class_pitch * i) * box);
			auto const base_line = top + ((height - 1.0) / 2 + title_base_line) * box;
			text(Layout::Text::Title, description.length, cellMiddle(3, 6), base_line);
			text(Layout::Text::Title, description.climb, cellMiddle(6, 8), base_line);
			return;
		}
		
		if (title_mode == 1)
		{
			if (description.classes.isEmpty())
			{
				beginRow(empty_class_row, false, -1);
			}
			else
			{
				top = beginRow(1.0, false, -1);
				text(Layout::Text::Title, description.classes.join(QLatin1String(", ")), full_row_middle, top + title_base_line * box);
			}
		}
		
		top = beginRow(1.0, false, -1);
		headCells(top, 1.0);
		auto const base_line = top + title_base_line * box;
		text(Layout::Text::Title, description.course, cellMiddle(0, 3), base_line);
		text(Layout::Text::Title, description.length, cellMiddle(3, 6), base_line);
		text(Layout::Text::Title, description.climb, cellMiddle(6, 8), base_line);
	}
	
	void row(const Row& row, int index)
	{
		auto const top = beginRow(1.0, row.kind == Row::Control, index);
		auto const base_line = top + number_base_line * box;
		auto const distance = row.distance < 0 ? QString() : QString::number(row.distance) + QLatin1String(" m");
		switch (row.kind)
		{
		case Row::Start:
			columns(top);
			pictogram(symbol_start, cellMiddle(0, 1), top);
			cells(row, top);
			break;
			
		case Row::Control:
			columns(top);
			text(Layout::Text::Number, QString::number(row.number), cellMiddle(0, 1), base_line);
			text(Layout::Text::Code, row.code, cellMiddle(1, 2), base_line);
			cells(row, top);
			break;
			
		case Row::TextBlock:
			text(Layout::Text::Distance, row.text, full_row_middle, top + title_base_line * box);
			break;
			
		case Row::CrossingPoint:
		case Row::Passage:
			pictogram(symbol_control, 0.5, top);
			pictogram(symbol_no_tapes, 1.1, top);
			pictogram(row.kind == Row::Passage ? symbol_passage : symbol_crossing_point, full_row_middle, top);
			pictogram(symbol_arrow, 7.4, top);
			pictogram(symbol_control, 8.0, top);
			break;
			
		case Row::MapFlip:
			pictogram(symbol_map_flip, full_row_middle, top);
			break;
			
		case Row::RouteToStart:
			tapes(top, false);
			text(Layout::Text::Distance, distance, full_row_middle, base_line);
			pictogram(symbol_route_start, 8.0, top);
			break;
			
		case Row::Route:
			pictogram(symbol_control, 0.5, top);
			tapes(top, false);
			text(Layout::Text::Distance, distance, full_row_middle, base_line);
			break;
			
		case Row::MapExchange:
			// Taped also without a marked route, with 0 m.
			pictogram(symbol_control, 0.5, top);
			tapes(top, false);
			text(Layout::Text::Distance, distance, full_row_middle, base_line);
			pictogram(symbol_route_start, 8.0, top);
			break;
			
		case Row::Finish:
			pictogram(symbol_control, 0.5, top);
			if (row.tapes == Row::NoTapes)
			{
				pictogram(symbol_no_tapes, 1.1, top);
				pictogram(symbol_arrow, 7.4, top);
			}
			else
			{
				tapes(top, row.tapes == Row::Funnel);
			}
			text(Layout::Text::Distance, distance, full_row_middle, base_line);
			pictogram(symbol_finish, 8.0, top);
			break;
		}
	}
	
	void finish()
	{
		if (!open)
			return;
		layout.rules.push_back({ { left, y }, { left + block_width * box, y }, true });
		QRectF const frame { left, 0.0, block_width * box, y };
		layout.blocks.push_back(frame);
		if (with_background)
			layout.backgrounds.push_back(frame.adjusted(-margin * box, -margin * box, margin * box, margin * box));
		open = false;
	}
	
private:
	/// Begins a row of a height in boxes, in a new block when this one is full. Returns the top of the row.
	double beginRow(double height_in_boxes, bool control, int index)
	{
		auto const height = height_in_boxes * box;
		if (index >= 0 && open && max_height > 0.0 && y + height > max_height + 1e-6)
		{
			finish();
			left += (block_width + margin) * box;
			y = 0.0;
			++block;
			previous_control = false;
		}
		
		auto thick = true;
		if (control && previous_control)
			thick = group > 0 && controls >= group;
		if (thick)
			controls = 0;
		
		auto const right = left + block_width * box;
		layout.rules.push_back({ { left, y }, { right, y }, thick });
		layout.rules.push_back({ { left, y }, { left, y + height }, true });
		layout.rules.push_back({ { right, y }, { right, y + height }, true });
		layout.rows.push_back({ index, block, { left, y, block_width * box, height }, thick });
		
		if (control)
			++controls;
		previous_control = control;
		open = true;
		auto const top = y;
		y += height;
		return top;
	}
	
	static double cellMiddle(int first_column, int end_column)
	{
		return (column_edges[first_column] + column_edges[end_column]) / 2;
	}
	
	void vertical(double x_in_boxes, bool thick, double top, double height_in_boxes)
	{
		auto const x = left + x_in_boxes * box;
		layout.rules.push_back({ { x, top }, { x, top + height_in_boxes * box }, thick });
	}
	
	/// The rules between the columns of a start or a control.
	void columns(double top)
	{
		for (int i = 1; i < 8; ++i)
			vertical(column_edges[i], i == 3 || i == 6, top, 1.0);
	}
	
	/// The rules of the row with name, length and climb.
	void headCells(double top, double height_in_boxes)
	{
		vertical(column_edges[3], true, top, height_in_boxes);
		vertical(column_edges[6], true, top, height_in_boxes);
	}
	
	void cells(const Row& row, double top)
	{
		for (int column = 0; column < CourseObject::ColumnCount; ++column)
		{
			if (row.cell[std::size_t(column)])
				pictogram(row.cell[std::size_t(column)], cellMiddle(column + 2, column + 3), top);
		}
		auto const slash = row.size.indexOf(QLatin1Char('/'));
		if (slash < 0)
		{
			text(Layout::Text::Size, row.size, cellMiddle(5, 6), top + size_base_line * box);
		}
		else
		{
			text(Layout::Text::Size, row.size.left(slash), size_first_place, top + size_first_base_line * box);
			text(Layout::Text::Size, row.size.mid(slash + 1), size_second_place, top + size_second_base_line * box);
			layout.rules.push_back({ { left + slash_begin[0] * box, top + slash_begin[1] * box },
			                         { left + slash_end[0] * box, top + slash_end[1] * box }, false });
		}
	}
	
	/// The dashes of a marked route and its arrow.
	void tapes(double top, bool funnel)
	{
		auto first = true;
		for (auto place : dash_places)
		{
			if (first && funnel)
				pictogram(symbol_funnel, funnel_place, top);
			else
				pictogram(symbol_dash, place, top);
			first = false;
		}
		pictogram(symbol_arrow, 7.45, top);
	}
	
	void pictogram(int symbol, double x_in_boxes, double top)
	{
		layout.pictograms.push_back({ symbol, { left + x_in_boxes * box, top + box / 2 } });
	}
	
	void text(Layout::Text::Style style, const QString& text, double x_in_boxes, double base_line)
	{
		if (!text.isEmpty())
			layout.texts.push_back({ style, { left + x_in_boxes * box, base_line }, text });
	}
	
	Layout& layout;
	const double box;
	/// After how many controls a rule is thick; 0 for never.
	const int group;
	/// How high a block may be, in mm; 0 for any height.
	const double max_height;
	const bool with_background;
	double left = 0.0;
	double y = 0.0;
	int block = 0;
	bool open = false;
	bool previous_control = false;
	/// The controls since the last thick rule.
	int controls = 0;
};


void moveLayout(Layout& layout, const QPointF& offset)
{
	for (auto& frame : layout.blocks)
		frame.translate(offset);
	for (auto& background : layout.backgrounds)
		background.translate(offset);
	for (auto& row : layout.rows)
		row.rect.translate(offset);
	for (auto& rule : layout.rules)
	{
		rule.begin += offset;
		rule.end += offset;
	}
	for (auto& pictogram : layout.pictograms)
		pictogram.centre += offset;
	for (auto& text : layout.texts)
		text.anchor += offset;
}


/// The symbols of the map for a description, scaled to the size of its box.
class ScaledSymbols
{
public:
	ScaledSymbols(const Map& map, double scale, std::vector<std::unique_ptr<Symbol>>& copies)
	: map(map)
	, scale(scale)
	, copies(copies)
	{}
	
	/// The symbol with the number, or nullptr when the map has none.
	const Symbol* get(int number)
	{
		auto const found = symbols.find(number);
		if (found != end(symbols))
			return found->second;
		
		auto const* symbol = symbolOfNumber(map, number);
		if (symbol && std::abs(scale - 1.0) > 1e-6)
		{
			copies.push_back(duplicate(*symbol));
			copies.back()->scale(scale);
			symbol = copies.back().get();
		}
		symbols[number] = symbol;
		return symbol;
	}
	
private:
	const Map& map;
	const double scale;
	std::vector<std::unique_ptr<Symbol>>& copies;
	std::map<int, const Symbol*> symbols;
};

int textSymbolNumber(Layout::Text::Style style)
{
	return symbol_first_text + int(style);
}

QFont plainFont(Layout::Text::Style style)
{
	QFont font(QStringLiteral("Arial"));
	font.setPixelSize(256);
	font.setBold(plain_bold[int(style)]);
	return font;
}

/// A text as it is painted plainly: tabulators are blanks there.
QString plainText(const QString& text)
{
	auto plain = text;
	return plain.replace(QLatin1Char('\t'), QLatin1String("  "));
}

Object* makeArea(const Symbol* symbol, const QRectF& rect)
{
	auto* path = new PathObject(symbol);
	path->addCoordinate(MapCoord(rect.left(), rect.top()));
	path->addCoordinate(MapCoord(rect.right(), rect.top()));
	path->addCoordinate(MapCoord(rect.right(), rect.bottom()));
	path->addCoordinate(MapCoord(rect.left(), rect.bottom()));
	path->closeAllParts();
	return path;
}

void paintPlain(QPainter* painter, const Layout& layout)
{
	painter->save();
	painter->setPen(Qt::NoPen);
	painter->setBrush(Qt::white);
	for (auto const& background : layout.backgrounds)
		painter->drawRect(background);
	
	painter->setBrush(Qt::NoBrush);
	for (auto thick : { false, true })
	{
		painter->setPen(QPen(Qt::black, (thick ? plain_thick : plain_thin) * layout.scale, Qt::SolidLine, Qt::SquareCap));
		for (auto const& rule : layout.rules)
		{
			if (rule.thick == thick)
				painter->drawLine(QLineF(rule.begin, rule.end));
		}
	}
	
	// The texts are laid out at a size at which fonts are exact, and scaled down.
	painter->setPen(Qt::NoPen);
	painter->setBrush(Qt::black);
	for (auto const& text : layout.texts)
	{
		auto const font = plainFont(text.style);
		auto const factor = plain_em[int(text.style)] * layout.box / 256.0;
		auto const shown = plainText(text.text);
		QPainterPath glyphs;
		glyphs.addText(0.0, 0.0, font, shown);
		painter->save();
		painter->translate(text.anchor);
		painter->scale(factor, factor);
		if (!isLeftAligned(text.style))
			painter->translate(-QFontMetricsF(font).horizontalAdvance(shown) / 2, 0.0);
		painter->drawPath(glyphs);
		painter->restore();
	}
	painter->restore();
}

}  // namespace



QString courseObjectField(const CourseObject& object, QChar letter)
{
	for (auto const& field : object.other)
	{
		if (field.letter == letter)
			return field.value;
	}
	return {};
}


void setCourseObjectField(CourseObject& object, QChar letter, const QString& value)
{
	auto const found = std::find_if(begin(object.other), end(object.other), [letter](const CourseField& field) {
		return field.letter == letter;
	});
	if (value.isEmpty())
	{
		if (found != end(object.other))
			object.other.erase(found);
	}
	else if (found != end(object.other))
	{
		found->value = value;
	}
	else
	{
		object.other.push_back({ letter, value });
	}
}


QString automaticCourseObjectText(const Map& map, const CourseObject& object)
{
	QStringList parts;
	for (int column = 0; column < CourseObject::ColumnCount; ++column)
	{
		auto const number = object.cell[std::size_t(column)];
		if (auto const* symbol = number ? symbolOfNumber(map, number) : nullptr)
		{
			auto const name = symbol->getPlainTextName();
			if (!name.isEmpty())
				parts.push_back(name);
		}
		if (column == CourseObject::ColumnF)
		{
			auto const size = courseObjectField(object, QLatin1Char('s'));
			if (!size.isEmpty())
				parts.push_back(size + QLatin1Char('m'));
		}
	}
	return parts.join(QLatin1String(", "));
}


QString courseObjectText(const Map& map, const CourseObject& object)
{
	return object.text.isEmpty() ? automaticCourseObjectText(map, object) : object.text;
}



namespace {

CourseObject* findCourseObject(CourseData& data, const QString& code)
{
	auto const found = std::find_if(begin(data.objects), end(data.objects), [&code](const CourseObject& object) {
		return object.code == code;
	});
	return found == end(data.objects) ? nullptr : &*found;
}

/// The bit of a column in the columns of a description symbol.
Symbol::DescriptionColumn columnBit(CourseObject::Column column)
{
	switch (column)
	{
	case CourseObject::ColumnC:  return Symbol::DescriptionColumnC;
	case CourseObject::ColumnD:  return Symbol::DescriptionColumnD;
	case CourseObject::ColumnE:  return Symbol::DescriptionColumnE;
	case CourseObject::ColumnF:  return Symbol::DescriptionColumnF;
	case CourseObject::ColumnG:  return Symbol::DescriptionColumnG;
	case CourseObject::ColumnH:
	case CourseObject::ColumnCount:
		break;
	}
	return Symbol::DescriptionColumnH;
}

/// A text as one line with single blanks.
QString oneLine(const QString& text)
{
	return text.simplified();
}

/**
 * Changes the cells or the size of an object by a function. The text
 * follows when it was the automatic text of what the object had.
 */
template <class Change>
void changeCells(const Map& map, CourseObject& object, Change change)
{
	auto const follows = !object.text.isEmpty() && object.text == automaticCourseObjectText(map, object);
	change(object);
	if (follows)
		object.text = automaticCourseObjectText(map, object);
}

}  // namespace


bool hasDescriptionCells(CourseObject::Kind kind)
{
	return kind == CourseObject::Start || kind == CourseObject::Control;
}


bool hasDescriptionText(CourseObject::Kind kind)
{
	return hasDescriptionCells(kind) || kind == CourseObject::MarkedRoute || kind == CourseObject::TextBlock;
}


int descriptionCellNumber(const Symbol& symbol)
{
	return symbol.getNumberComponent(0) * 1000 + std::max(symbol.getNumberComponent(1), 0);
}


std::vector<const Symbol*> descriptionSymbols(const Map& map, CourseObject::Column column)
{
	std::vector<const Symbol*> symbols;
	auto const bit = columnBit(column);
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->isForDescriptionColumn(bit))
			symbols.push_back(symbol);
	}
	std::stable_sort(begin(symbols), end(symbols), [](const Symbol* lhs, const Symbol* rhs) {
		return descriptionCellNumber(*lhs) < descriptionCellNumber(*rhs);
	});
	return symbols;
}


bool setCourseObjectCell(const Map& map, CourseData& data, const QString& code, CourseObject::Column column, int number)
{
	auto* object = findCourseObject(data, code);
	if (!object || !hasDescriptionCells(object->kind) || column < 0 || column >= CourseObject::ColumnCount)
		return false;
	if (number != 0)
	{
		auto const* symbol = symbolOfNumber(map, number);
		if (!symbol || !symbol->isForDescriptionColumn(columnBit(column)))
			return false;
	}
	changeCells(map, *object, [column, number](CourseObject& changed) {
		changed.cell[std::size_t(column)] = number;
		// The cell F shows a symbol or a size.
		if (column == CourseObject::ColumnF && number != 0)
			setCourseObjectField(changed, QLatin1Char('s'), {});
	});
	return true;
}


bool setCourseObjectSize(const Map& map, CourseData& data, const QString& code, const QString& size)
{
	auto* object = findCourseObject(data, code);
	if (!object || !hasDescriptionCells(object->kind))
		return false;
	// The texts add the "m".
	auto value = size.trimmed();
	if (value.endsWith(QLatin1Char('m'), Qt::CaseInsensitive))
		value = value.left(value.size() - 1).trimmed();
	if (std::any_of(value.begin(), value.end(), [](QChar c) { return c.isSpace(); }))
		return false;
	changeCells(map, *object, [&value](CourseObject& changed) {
		setCourseObjectField(changed, QLatin1Char('s'), value);
		if (!value.isEmpty())
			changed.cell[CourseObject::ColumnF] = 0;
	});
	return true;
}


bool setCourseObjectText(CourseData& data, const QString& code, const QString& text)
{
	auto* object = findCourseObject(data, code);
	if (!object || !hasDescriptionText(object->kind))
		return false;
	object->text = oneLine(text);
	return true;
}


bool setMarkedRouteFunnel(CourseData& data, const QString& code, bool funnel)
{
	auto* object = findCourseObject(data, code);
	if (!object || object->kind != CourseObject::MarkedRoute)
		return false;
	setCourseObjectField(*object, QLatin1Char('m'), funnel ? QStringLiteral("f") : QString{});
	return true;
}


CourseDescription describeCourse(const Map& map, const CourseData& data, const QString& course_name)
{
	CourseDescription description;
	auto const* course = data.findCourse(course_name);
	if (!course)
		return description;
	for (auto const& item : course->items)
	{
		if (isVariationItem(item.type))
			return description;
	}
	
	description.known = true;
	description.title = data.options.eventTitle();
	for (auto const& entry : data.classes)
	{
		if (entry.course == course->name)
			description.classes.push_back(entry.name);
	}
	description.course = course->name;
	auto const length = measureCourse(map, data, *course);
	if (length.known)
		description.length = QString::number(std::floor(length.total_m / 100.0 + 0.5) / 10.0, 'f', 1) + QLatin1String(" km");
	if (course->climb_m > 0)
		description.climb = QString::number(course->climb_m) + QLatin1String(" m");
	
	// From mm in the map to m, rounded to 10.
	auto const metres_per_mm = map.getScaleDenominator() / 1000.0;
	auto const metres = [metres_per_mm](double mm) {
		return int(std::floor(mm * metres_per_mm / 10.0 + 0.5)) * 10;
	};
	
	auto& rows = description.rows;
	PendingRoute pending;
	Place last;
	auto part = 0;
	auto number = 0;
	
	auto const add = [&rows, &part](Row::Kind kind) -> Row& {
		rows.emplace_back();
		rows.back().kind = kind;
		rows.back().part = part;
		return rows.back();
	};
	// A marked route which no item took in gets a row of its own.
	auto const flush = [&]() {
		if (!pending.active)
			return;
		auto& row = add(Row::Route);
		row.code = pending.code;
		row.text = pending.text;
		row.tapes = Row::Taped;
		if (pending.place.valid)
			row.distance = metres(pending.before + pending.place.path);
		pending.active = false;
	};
	
	for (auto const& item : course->items)
	{
		auto const* object = item.namesObject() ? data.findObject(item.code) : nullptr;
		switch (item.type)
		{
		case CourseItem::MarkedRoute:
			flush();
			pending = {};
			pending.active = true;
			pending.code = item.code;
			pending.place = placeOf(map, object, true);
			pending.has_previous = last.valid;
			if (last.valid && pending.place.valid)
				pending.before = distance(last.exit, pending.place.entry);
			if (object)
			{
				pending.text = object->text;
				pending.funnel = courseObjectField(*object, QLatin1Char('m')) == QLatin1String("f");
			}
			if (pending.place.valid)
				last = pending.place;
			break;
			
		case CourseItem::Start:
		{
			auto const place = placeOf(map, object, false);
			if (pending.active && !pending.has_previous)
			{
				auto& row = add(Row::RouteToStart);
				row.code = pending.code;
				row.text = pending.text;
				row.tapes = Row::Taped;
				if (pending.place.valid && place.valid)
					row.distance = metres(pending.place.path + distance(pending.place.exit, place.entry));
				pending.active = false;
			}
			flush();
			auto& row = add(Row::Start);
			row.code = item.code;
			if (object)
			{
				row.cell = object->cell;
				row.size = courseObjectField(*object, QLatin1Char('s'));
				row.text = courseObjectText(map, *object);
			}
			if (place.valid)
				last = place;
			break;
		}
			
		case CourseItem::Control:
		{
			flush();
			auto& row = add(Row::Control);
			row.code = item.code;
			row.number = ++number;
			if (object)
			{
				row.cell = object->cell;
				row.size = courseObjectField(*object, QLatin1Char('s'));
				row.text = courseObjectText(map, *object);
			}
			auto const place = placeOf(map, object, false);
			if (place.valid)
				last = place;
			break;
		}
			
		case CourseItem::Finish:
		{
			auto const place = placeOf(map, object, false);
			auto& row = add(Row::Finish);
			row.code = item.code;
			if (pending.active)
			{
				row.tapes = pending.funnel ? Row::Funnel : Row::Taped;
				row.text = pending.text;
				if (pending.place.valid && place.valid)
					row.distance = metres(pending.before + pending.place.path + distance(pending.place.exit, place.entry));
				pending.active = false;
			}
			else if (last.valid && place.valid)
			{
				row.distance = metres(distance(last.exit, place.entry));
			}
			if (place.valid)
				last = place;
			break;
		}
			
		case CourseItem::MapExchange:
		{
			auto& row = add(Row::MapExchange);
			row.distance = 0;
			if (pending.active)
			{
				row.tapes = Row::Taped;
				row.code = pending.code;
				row.text = pending.text;
				row.distance = pending.place.valid ? metres(pending.before + pending.place.path) : -1;
				pending.active = false;
			}
			++part;
			last = {};
			break;
		}
			
		case CourseItem::MapFlip:
			flush();
			add(Row::MapFlip);
			++part;
			last = {};
			break;
			
		case CourseItem::TextBlock:
		{
			flush();
			auto& row = add(Row::TextBlock);
			row.code = item.code;
			if (object)
				row.text = object->text;
			break;
		}
			
		case CourseItem::CrossingPoint:
			flush();
			add(Row::CrossingPoint);
			break;
			
		case CourseItem::Passage:
			flush();
			add(Row::Passage);
			break;
			
		default:
			break;
		}
	}
	flush();
	description.parts = part + 1;
	return description;
}


QString courseDescriptionLine(const CourseDescriptionRow& row)
{
	auto const tab = QLatin1Char('\t');
	auto const distance = QString::number(std::max(0, row.distance));
	// The own text of a marked route, the semicolon standing for the distance.
	auto const own_text = [&row, &distance]() {
		auto text = row.text;
		auto const mark = text.indexOf(QLatin1Char(';'));
		if (mark >= 0)
			text.replace(mark, 1, distance);
		return text;
	};
	
	switch (row.kind)
	{
	case Row::Start:
		return row.text.isEmpty() ? CourseDescription::tr("Start") : CourseDescription::tr("Start") + tab + row.text;
	case Row::Control:
		return QString::number(row.number) + QLatin1Char('.') + tab + row.code + tab + row.text;
	case Row::TextBlock:
		return row.text;
	case Row::CrossingPoint:
		return CourseDescription::tr("Mandatory crossing point or points");
	case Row::Passage:
		return CourseDescription::tr("Mandatory passage through out of bounds area");
	case Row::MapFlip:
		return CourseDescription::tr("Map flip");
	case Row::RouteToStart:
		if (!row.text.isEmpty())
			return own_text();
		return CourseDescription::tr("Follow taped route %1 m to start").arg(distance);
	case Row::Route:
		if (!row.text.isEmpty())
			return own_text();
		return CourseDescription::tr("Follow taped route %1 m away from control").arg(distance);
	case Row::MapExchange:
		if (!row.text.isEmpty())
			return own_text();
		if (row.tapes == Row::NoTapes)
			return CourseDescription::tr("Map exchange");
		return CourseDescription::tr("Follow taped route %1 m to map exchange").arg(distance);
	case Row::Finish:
		if (!row.text.isEmpty())
			return own_text();
		if (row.tapes == Row::Taped)
			return CourseDescription::tr("%1 m from last control to finish. Follow taped route").arg(distance);
		if (row.tapes == Row::Funnel)
			return CourseDescription::tr("%1 m from last control to finish. Navigate to finish funnel, then follow tapes").arg(distance);
		return CourseDescription::tr("%1 m from last control to finish. Navigate to finish, no tapes").arg(distance);
	}
	return {};
}


QStringList courseDescriptionLines(const CourseDescription& description, int part)
{
	QStringList lines;
	lines.push_back(description.title);
	lines.push_back(CourseDescription::tr("Class %1").arg(description.classes.isEmpty() ? description.course : description.classes.join(QLatin1String(", "))));
	auto course_line = CourseDescription::tr("Course %1").arg(description.course);
	if (!description.length.isEmpty())
		course_line += QLatin1String(", ") + CourseDescription::tr("Length %1").arg(description.length);
	if (!description.climb.isEmpty())
		course_line += QLatin1String(", ") + CourseDescription::tr("Climb %1").arg(description.climb);
	lines.push_back(course_line);
	
	for (auto const& row : description.rows)
	{
		if (part < 0 || row.part == part)
			lines.push_back(courseDescriptionLine(row));
	}
	return lines;
}



CourseDescriptionLayout layoutCourseDescription(const CourseDescription& description, const CourseOptions& options, int part)
{
	CourseDescriptionLayout layout;
	layout.box = options.boxSize();
	layout.scale = layout.box / design_box;
	if (!description.known)
		return layout;
	
	BlockBuilder builder(layout, options);
	builder.head(description, options.titleMode());
	for (std::size_t i = 0; i < description.rows.size(); ++i)
	{
		if (part < 0 || description.rows[i].part == part)
			builder.row(description.rows[i], int(i));
	}
	builder.finish();
	return layout;
}


CourseDescriptionLayout layoutCourseDescriptionText(const CourseDescription& description, const CourseOptions& options, int part)
{
	CourseDescriptionLayout layout;
	layout.box = options.boxSize();
	layout.scale = layout.box / design_box;
	layout.in_text = true;
	if (!description.known)
		return layout;
	
	auto const lines = courseDescriptionLines(description, part);
	for (int i = 0; i < lines.size(); ++i)
	{
		auto const style = i < 3 ? Layout::Text::TextTitle : Layout::Text::TextRow;
		layout.texts.push_back({ style, { 0.0, (text_first_line + i) * layout.box }, lines[i] });
	}
	return layout;
}



CourseDescriptionDrawing::CourseDescriptionDrawing() = default;
CourseDescriptionDrawing::CourseDescriptionDrawing(CourseDescriptionDrawing&&) noexcept = default;
CourseDescriptionDrawing& CourseDescriptionDrawing::operator=(CourseDescriptionDrawing&&) noexcept = default;
CourseDescriptionDrawing::~CourseDescriptionDrawing() = default;


CourseDescriptionDrawing drawCourseDescriptions(const Map& map, const CourseData& data, const QString& course)
{
	CourseDescriptionDrawing drawing;
	auto const description = describeCourse(map, data, course);
	if (!description.known)
		return drawing;
	drawing.drawn = true;
	
	auto const box = data.options.boxSize();
	ScaledSymbols symbols(map, box / design_box, drawing.symbols);
	for (auto const& course_object : data.objects)
	{
		if (course_object.kind != CourseObject::Description || course_object.object.isNull())
			continue;
		auto const* place = map.findObject(course_object.object);
		if (!place || place->getRawCoordinateVector().empty())
			continue;
		
		auto const origin = QPointF(MapCoordF(place->getRawCoordinateVector().front()));
		auto const in_text = courseObjectField(course_object, QLatin1Char('o')) == QLatin1String("t");
		auto layout = in_text ? layoutCourseDescriptionText(description, data.options)
		                      : layoutCourseDescription(description, data.options);
		moveLayout(layout, origin);
		
		// Plainly, when the map has not the symbols of the rules and the texts.
		auto plain = false;
		if (in_text)
		{
			plain = !symbols.get(textSymbolNumber(Layout::Text::TextTitle))
			        || !symbols.get(textSymbolNumber(Layout::Text::TextRow));
		}
		else
		{
			plain = !symbols.get(symbol_thick_rule) || !symbols.get(symbol_thin_rule);
			for (auto style : { Layout::Text::Number, Layout::Text::Code, Layout::Text::Title, Layout::Text::Distance })
				plain = plain || !symbols.get(textSymbolNumber(style));
		}
		
		// The texts, and for a description in text their box.
		QRectF text_box;
		for (auto const& text : layout.texts)
		{
			auto const left_aligned = isLeftAligned(text.style);
			auto const* symbol = plain ? nullptr : symbols.get(textSymbolNumber(text.style));
			if (symbol && symbol->getType() == Symbol::Text)
			{
				auto* object = new TextObject(symbol);
				object->setAnchorPosition(MapCoordF(text.anchor));
				object->setHorizontalAlignment(left_aligned ? TextObject::AlignLeft : TextObject::AlignHCenter);
				object->setVerticalAlignment(TextObject::AlignBaseline);
				object->setText(text.text);
				if (in_text)
				{
					object->update();
					text_box |= object->getExtent();
				}
				drawing.objects.emplace_back(object);
			}
			else if (plain && in_text)
			{
				auto const font = plainFont(text.style);
				auto const metrics = QFontMetricsF(font);
				auto const factor = plain_em[int(text.style)] * box / 256.0;
				text_box |= QRectF(text.anchor.x(), text.anchor.y() - metrics.ascent() * factor,
				                   metrics.horizontalAdvance(plainText(text.text)) * factor, (metrics.ascent() + metrics.descent()) * factor);
			}
		}
		if (in_text && !layout.texts.empty())
		{
			// From the description object to the end of the texts.
			text_box |= QRectF(origin, QSizeF(0.001, 0.001));
			layout.blocks.push_back(text_box);
			if (data.options.whiteBackground())
				layout.backgrounds.push_back(text_box.adjusted(-margin * box, -margin * box, margin * box, margin * box));
		}
		
		for (auto const& pictogram : layout.pictograms)
		{
			auto const* symbol = symbols.get(pictogram.symbol);
			if (!symbol || symbol->getType() != Symbol::Point)
				continue;
			auto* object = new PointObject(symbol);
			object->setPosition(MapCoordF(pictogram.centre));
			drawing.objects.emplace_back(object);
		}
		
		if (!plain)
		{
			for (auto const& rule : layout.rules)
			{
				auto const* symbol = symbols.get(rule.thick ? symbol_thick_rule : symbol_thin_rule);
				if (symbol->getType() != Symbol::Line)
					continue;
				auto* path = new PathObject(symbol);
				path->addCoordinate(MapCoord(rule.begin));
				path->addCoordinate(MapCoord(rule.end));
				drawing.objects.emplace_back(path);
			}
			auto const* area = symbols.get(symbol_background);
			if (area && area->getType() == Symbol::Area)
			{
				for (auto const& background : layout.backgrounds)
					drawing.objects.emplace_back(makeArea(area, background));
			}
		}
		
		drawing.layouts.push_back(std::move(layout));
		drawing.plain.push_back(plain);
	}
	return drawing;
}


void paintCourseDescriptions(QPainter* painter, Map* map, const CourseDescriptionDrawing& drawing, const QRectF& bounding_box, qreal scaling)
{
	if (!drawing.drawn)
		return;
	
	for (std::size_t i = 0; i < drawing.layouts.size(); ++i)
	{
		if (drawing.plain[i])
			paintPlain(painter, drawing.layouts[i]);
	}
	
	if (drawing.objects.empty())
		return;
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
}


}  // namespace OpenOrienteering
