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


#include "course_diagram.h"

#include <algorithm>
#include <cstddef>

#include <QChar>
#include <QCoreApplication>

#include "course/course_data.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseDiagram", text);
}

bool isBegin(CourseItem::Type type)
{
	return type == CourseItem::TeamForkBegin || type == CourseItem::LegForkBegin;
}

bool isBranch(CourseItem::Type type)
{
	return type == CourseItem::ForkBranch || type == CourseItem::LegBranch;
}

bool isEnd(CourseItem::Type type)
{
	return type == CourseItem::TeamForkEnd || type == CourseItem::LegForkEnd;
}

QString markerText(CourseItem::Type type)
{
	switch (type)
	{
	case CourseItem::CrossingPoint:
		return tr("Crossing point");
	case CourseItem::Passage:
		return tr("Passage");
	case CourseItem::MapExchange:
		return tr("Map exchange");
	case CourseItem::MapFlip:
		return tr("Map flip");
	case CourseItem::TeamForkBegin:
		return tr("Team variation");
	case CourseItem::LegForkBegin:
		return tr("Leg variation");
	case CourseItem::ForkBranch:
	case CourseItem::LegBranch:
		return tr("Branch");
	case CourseItem::TeamForkEnd:
	case CourseItem::LegForkEnd:
		return tr("End");
	default:
		return {};
	}
}

class Builder
{
public:
	explicit Builder(const std::vector<CourseItem>& items) : items(items) {}
	
	/// The rows and columns which a sequence takes.
	struct Extent
	{
		int rows = 0;
		int columns = 1;
	};
	
	int add(std::size_t item, int row, int column, CourseDiagram::Cell::Kind kind, const QString& text)
	{
		CourseDiagram::Cell cell;
		cell.item = int(item);
		cell.row = row;
		cell.column = column;
		cell.kind = kind;
		cell.text = text;
		diagram.cells.push_back(cell);
		diagram.rows = std::max(diagram.rows, row + 1);
		diagram.columns = std::max(diagram.columns, column + 1);
		return int(diagram.cells.size()) - 1;
	}
	
	void link(const std::vector<int>& tails, int cell)
	{
		for (auto const tail : tails)
			diagram.lines.push_back({ tail, cell });
	}
	
	/// Lays out the items from i on, beginning at the row and column, until
	/// the end of the course, or, in a branch, until a branch marker or an end.
	/// The tails are the cells which lead to the next one; they are updated.
	Extent sequence(std::size_t& i, int row, int column, std::vector<int>& tails, bool in_branch)
	{
		Extent extent;
		while (i < items.size())
		{
			auto const& item = items[i];
			if (in_branch && (isBranch(item.type) || isEnd(item.type)))
				break;
			
			if (isBegin(item.type))
			{
				auto const begin = add(i, row + extent.rows, column, CourseDiagram::Cell::Begin, markerText(item.type));
				link(tails, begin);
				++i;
				auto const branch_row = row + extent.rows + 1;
				auto branch_column = column;
				auto longest = 0;
				auto letter = 0;
				std::vector<int> branch_tails;
				while (i < items.size() && !isEnd(items[i].type))
				{
					std::vector<int> tails_here = { begin };
					auto rows = 0;
					if (isBranch(items[i].type))
					{
						auto text = markerText(items[i].type);
						if (items[i].type == CourseItem::ForkBranch && letter < 26)
							text = QChar(ushort('A' + letter++));
						else if (items[i].type == CourseItem::LegBranch && !items[i].legs.isEmpty())
							text = items[i].legs;
						auto const marker = add(i, branch_row, branch_column, CourseDiagram::Cell::Branch, text);
						link(tails_here, marker);
						tails_here = { marker };
						rows = 1;
						++i;
					}
					auto const inner = sequence(i, branch_row + rows, branch_column, tails_here, true);
					longest = std::max(longest, rows + inner.rows);
					branch_column += inner.columns;
					branch_tails.insert(branch_tails.end(), tails_here.begin(), tails_here.end());
				}
				extent.columns = std::max(extent.columns, branch_column - column);
				extent.rows += 1 + longest;
				if (branch_tails.empty())
					branch_tails = { begin };
				if (i < items.size())
				{
					// The end of the variation.
					auto const end = add(i, row + extent.rows, column, CourseDiagram::Cell::End, markerText(items[i].type));
					link(branch_tails, end);
					tails = { end };
					extent.rows += 1;
					++i;
				}
				else
				{
					tails = branch_tails;
				}
				continue;
			}
			
			auto const kind = item.namesObject() ? CourseDiagram::Cell::Object
			                  : (isBranch(item.type) ? CourseDiagram::Cell::Branch
			                  : (isEnd(item.type) ? CourseDiagram::Cell::End : CourseDiagram::Cell::Marker));
			auto const cell = add(i, row + extent.rows, column, kind, item.namesObject() ? item.code : markerText(item.type));
			link(tails, cell);
			tails = { cell };
			extent.rows += 1;
			++i;
		}
		return extent;
	}
	
	const std::vector<CourseItem>& items;
	CourseDiagram diagram;
};

}  // namespace



int CourseDiagram::cellOfItem(int item) const
{
	auto const found = std::find_if(begin(cells), end(cells), [item](const Cell& cell) { return cell.item == item; });
	return found == end(cells) ? -1 : int(found - begin(cells));
}


int CourseDiagram::cellAt(int row, int column) const
{
	auto const found = std::find_if(begin(cells), end(cells), [row, column](const Cell& cell) {
		return cell.row == row && cell.column == column;
	});
	return found == end(cells) ? -1 : int(found - begin(cells));
}


CourseDiagram courseDiagram(const Course& course)
{
	Builder builder(course.items);
	std::size_t i = 0;
	std::vector<int> tails;
	builder.sequence(i, 0, 0, tails, false);
	return builder.diagram;
}


}  // namespace OpenOrienteering
