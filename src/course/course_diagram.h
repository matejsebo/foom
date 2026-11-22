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


#ifndef FOOM_COURSE_DIAGRAM_H
#define FOOM_COURSE_DIAGRAM_H

#include <vector>

#include <QString>

namespace OpenOrienteering {

struct Course;


/**
 * The branch diagram of a course: its items in a grid, the branches of a
 * variation side by side, joined again at its end.
 *
 * Every item of the course has one cell. An item which follows another
 * stands one row lower in the same column. The begin of a variation is
 * followed by its branches, each in columns of its own beginning with its
 * marker in the row below the begin; a nested variation widens its branch.
 * The end of the variation stands in the column of its begin, in the row
 * below the longest branch. The lines join each cell to the cells which
 * come next in a run: the begin to the marker of every branch, the last
 * cell of every branch to the end.
 *
 * See the design notes, "Relays: the branch diagram".
 */
struct CourseDiagram
{
	struct Cell
	{
		enum Kind
		{
			Object,   ///< start, control, marked route, finish, text block
			Marker,   ///< crossing point, passage, map exchange, map flip
			Begin,    ///< the begin of a variation
			Branch,   ///< the marker of a branch
			End       ///< the end of a variation
		};
		
		/** The index of the item in the course. */
		int item = 0;
		int row = 0;
		int column = 0;
		Kind kind = Object;
		/** The code of an object, the letter of a team branch, the legs of a leg branch, or a short word. */
		QString text;
	};
	
	/** A line from a cell to one which comes next, as indexes into cells. */
	struct Line
	{
		int from = 0;
		int to = 0;
	};
	
	std::vector<Cell> cells;
	std::vector<Line> lines;
	int rows = 0;
	int columns = 0;
	
	/** The index of the cell of an item; -1 when there is none. */
	int cellOfItem(int item) const;
	/** The index of the cell at a place; -1 when there is none. */
	int cellAt(int row, int column) const;
};

/**
 * The diagram of a course. A variation without its end is closed at the
 * end of the course, without a cell for the end; a branch marker or an end
 * outside a variation is a cell of its own.
 */
CourseDiagram courseDiagram(const Course& course);


}  // namespace OpenOrienteering

#endif
