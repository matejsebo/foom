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


#ifndef FOOM_COURSE_SYMBOLS_H
#define FOOM_COURSE_SYMBOLS_H

#include <QString>

namespace OpenOrienteering {

class Map;


/**
 * FOOM's course symbols: the symbols of the course objects, the
 * description symbols of the columns C to H, and the parts of which the
 * block of the control descriptions is drawn.
 *
 * They are made of upstream's symbol set Course Design, whose pictograms
 * follow the IOF's control descriptions of 2018 and are drawn for a box
 * of 6 mm. The symbols get the numbers which OCD course files use, and
 * the roles and columns of course setting.
 *
 * See the design notes, "FOOM's course symbols".
 */

/** What addCourseSymbols() did. */
struct CourseSymbolsReport
{
	/** The symbol set was read. */
	bool loaded = false;
	/** The number of symbols which were added. */
	int added = 0;
	/** The number of course symbols whose number the map had; its symbols with these numbers are left as they are. */
	int taken = 0;
};

/** The file of upstream's symbol set among the data of the program. */
QString courseSymbolSetPath();

/**
 * Adds the course symbols to a map: those whose number the map has not.
 * Their colours come with them and lie above the colours which the map
 * had. The symbols of the start, the control, the finish and the other
 * overprint are sized for the scale of the map.
 *
 * The symbol set is read from the path, and from courseSymbolSetPath()
 * when none is given.
 */
CourseSymbolsReport addCourseSymbols(Map& map, const QString& symbol_set_path = {});


}  // namespace OpenOrienteering

#endif
