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

#ifndef FOOM_CORE_OBJECT_INFORMATION_H
#define FOOM_CORE_OBJECT_INFORMATION_H

#include <limits>
#include <vector>

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;
class Object;


/**
 * The details of one object.
 *
 * See the design notes, "Object information".
 */
struct ObjectInformation
{
	struct Vertex
	{
		double x = 0.0;           ///< Paper mm.
		double y = 0.0;
		QString flags;            ///< curve start, dash, gap, hole, close; separated by commas.
	};
	
	QString uuid;
	QString kind;                 ///< Point, Line, Area or Text.
	QString symbol;               ///< Number and name.
	QString part;
	int index = 0;                ///< In the part, from 1.
	QString group;
	QString created;              ///< UTC, ISO 8601, or "unknown".
	QString modified;
	double rotation_deg = std::numeric_limits<double>::quiet_NaN();
	QString text;
	std::vector<Vertex> vertices;
	double length_mm = std::numeric_limits<double>::quiet_NaN();
	double length_m = std::numeric_limits<double>::quiet_NaN();
	double area_mm2 = std::numeric_limits<double>::quiet_NaN();
	double area_m2 = std::numeric_limits<double>::quiet_NaN();
	QStringList colours;
	QStringList tags;             ///< key=value
};

/// Collects the details of the object of the map.
ObjectInformation objectInformation(const Map& map, const Object& object);

/// Plain text of the details, one line per field and per coordinate.
QString objectInformationText(const ObjectInformation& info);


}  // namespace OpenOrienteering

#endif
