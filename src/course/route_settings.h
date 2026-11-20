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


#ifndef FOOM_ROUTE_SETTINGS_H
#define FOOM_ROUTE_SETTINGS_H

#include <utility>
#include <vector>

#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {


/**
 * The settings of the route analysis, which are a part of the course data.
 *
 * See the design notes, "Route analysis: the settings and
 * the elevation", and for the kinds "Route analysis: runnability and the
 * terrain rasters".
 */

/** How fast a symbol lets a runner go, in percent of the full speed. */
struct Runnability
{
	enum Kind
	{
		None,        ///< the symbol does not count
		Ground,      ///< open ground: the percentage of full speed there
		Vegetation,  ///< forest with undergrowth: the percentage of the speed in forest; it covers the ground below
		Hindrance,   ///< undergrowth drawn over ground or forest, and lines which are crossed: the slowest of what covers a place counts
		Surface,     ///< stony or broken ground: multiplies what is there
		Path,        ///< a road, a path or a ride: its percentage in place of everything
		Impassable
	};
	
	Kind kind = None;
	int percent = 100;
};

inline bool operator==(const Runnability& lhs, const Runnability& rhs) { return lhs.kind == rhs.kind && lhs.percent == rhs.percent; }
inline bool operator!=(const Runnability& lhs, const Runnability& rhs) { return !(lhs == rhs); }

/** The name of a kind in the map file: "ground", "path" and so on. */
QString runnabilityKindName(Runnability::Kind kind);

/** Returns false for a name which is none. */
bool runnabilityKindFromName(const QString& name, Runnability::Kind& kind);

/** The settings of the route analysis. */
struct RouteSettings
{
	/** The pace on flat ground of full runnability, in seconds per kilometre. */
	int base_pace = 200;
	bool use_runnability = true;
	bool use_elevation = true;
	/** The record's correction of the effect of the climb, from 0 to 1; kept and not used. */
	double climb_factor = 1.0;
	/** The runnability of single symbols where it is not the default, by their numbers ("406.000"). */
	std::vector<std::pair<QString, Runnability>> symbols;
	
	/** Nothing is set which a file would have to keep. */
	bool isDefault() const;
};

bool operator==(const RouteSettings& lhs, const RouteSettings& rhs);
inline bool operator!=(const RouteSettings& lhs, const RouteSettings& rhs) { return !(lhs == rhs); }

/** Writes the element <route_settings> of the map file. */
void saveRouteSettings(const RouteSettings& settings, QXmlStreamWriter& xml);

/** Reads the element <route_settings> on which the reader stands. */
RouteSettings loadRouteSettings(QXmlStreamReader& xml);

/**
 * Reads the OCD parameter string 1088: the file of the graph, then b the
 * base speed, r and e whether runnability and elevation are used, c the
 * correction of the climb. Returns false for a string without a base speed.
 */
bool routeSettingsFromOcdRecord(const QString& record, RouteSettings& settings);

/** Writes the parameter string 1088, without a file of the graph. */
QString routeSettingsToOcdRecord(const RouteSettings& settings);


}  // namespace OpenOrienteering

#endif
