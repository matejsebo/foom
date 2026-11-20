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


#ifndef FOOM_ROUTE_STORE_H
#define FOOM_ROUTE_STORE_H

#include <vector>

#include <QString>

#include "core/map_coord.h"

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {


/**
 * The routes of the route analysis which are kept in the map, as a part
 * of the course data.
 *
 * See the design notes, "Route analysis: all courses, and
 * the routes in the file".
 */

/** The route of a leg, or the path of a marked route with its time. */
struct StoredRoute
{
	/** The codes of the objects between which the leg runs; for a marked route its code twice. */
	QString from;
	QString to;
	/** 0 for the fastest route of the leg; from 1 on for the routes which are led over via points. */
	int index = 0;
	/** False for a leg for which no route was found: the points are then its two ends. */
	bool found = true;
	double length_m = 0.0;
	double time_s = 0.0;
	double climb_m = 0.0;
	/** In mm of the map, from where the leg begins to where it ends. */
	std::vector<MapCoordF> points;
	/** The places over which the route was led, in their order; none for the fastest route. */
	std::vector<MapCoordF> via;
	
	bool isMarkedRoute() const { return from == to; }
	
	/** The route begins and ends at these places, within 0.2 mm: it is still the route of its leg. */
	bool fits(const MapCoordF& begin, const MapCoordF& end) const;
};

bool operator==(const StoredRoute& lhs, const StoredRoute& rhs);
inline bool operator!=(const StoredRoute& lhs, const StoredRoute& rhs) { return !(lhs == rhs); }


struct StoredRoutes
{
	/** The search had heights: the climbs are known. */
	bool with_heights = false;
	std::vector<StoredRoute> routes;
	
	bool isEmpty() const { return routes.empty(); }
	
	/** The route between two codes, the fastest or the one with an index; none when it is not kept. */
	const StoredRoute* find(const QString& from, const QString& to, int index = 0) const;
	
	/** The routes of a leg which are led over via points, by their index. */
	std::vector<const StoredRoute*> viaRoutes(const QString& from, const QString& to) const;
	
	/** The index which the next route over via points of a leg gets: one more than the highest which is kept. */
	int nextIndex(const QString& from, const QString& to) const;
	
	/** Takes a route out; false when it is not kept. */
	bool remove(const QString& from, const QString& to, int index);
	
	/** Keeps a route, in the place of the one between the same codes with the same index. */
	void put(StoredRoute route);
	
	/**
	 * Takes the routes of another search in. Routes which were searched
	 * with heights and without are not mixed: when the others differ in
	 * this, those which were kept before are dropped.
	 */
	void take(const StoredRoutes& others);
	
	/**
	 * Takes the routes over via points of another store in, for the legs
	 * whose fastest route is here with the same ends, when both stores were
	 * searched with heights or both without. Returns their number.
	 */
	int takeViaRoutes(const StoredRoutes& others);
};

bool operator==(const StoredRoutes& lhs, const StoredRoutes& rhs);
inline bool operator!=(const StoredRoutes& lhs, const StoredRoutes& rhs) { return !(lhs == rhs); }


/** Writes the element <routes> of the map file. */
void saveStoredRoutes(const StoredRoutes& routes, QXmlStreamWriter& xml);

/** Reads the element <routes> on which the reader stands. */
StoredRoutes loadStoredRoutes(QXmlStreamReader& xml);


}  // namespace OpenOrienteering

#endif
