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


#include "route_course.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <QChar>
#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/virtual_path.h"
#include "course/course_edit.h"
#include "course/course_map.h"
#include "course/course_parts.h"
#include "course/course_relay.h"
#include "course/course_render.h"
#include "course/route_terrain.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseRoutes", text);
}

QString metres(double value)
{
	return QString::number(qRound64(value)) + QLatin1String(" m ");
}

RouteResult resultOf(const StoredRoute& stored)
{
	RouteResult result;
	result.found = stored.found;
	if (stored.found)
	{
		result.points = stored.points;
		result.length_m = stored.length_m;
		result.time_s = stored.time_s;
		result.climb_m = stored.climb_m;
	}
	return result;
}

StoredRoute storedOf(const CourseRouteLeg& leg, const MapCoordF& begin, const MapCoordF& end)
{
	StoredRoute stored;
	stored.from = leg.from;
	stored.to = leg.to;
	stored.found = leg.route.found;
	if (leg.route.found)
	{
		stored.points = leg.route.points;
		stored.length_m = leg.route.length_m;
		stored.time_s = leg.route.time_s;
		stored.climb_m = leg.route.climb_m;
	}
	else
	{
		stored.points = { begin, end };
	}
	return stored;
}

/**
 * The routes of a course: from the kept ones where they fit, by the search
 * where there is one, and not known otherwise. What is searched is put
 * into the store.
 */
CourseRoutes routesOf(const Map& map, const CourseData& data, const QString& course_name, int part,
                      const StoredRoutes* kept, StoredRoutes* store, RouteSearch* search)
{
	CourseRoutes routes;
	routes.course = course_name;
	routes.part = part;
	routes.with_heights = search ? search->usesHeights() : (kept && kept->with_heights);
	auto const* course = data.findCourse(course_name);
	if (!course)
		return routes;
	for (auto const& item : course->items)
	{
		if (isVariationItem(item.type))
			return routes;
	}
	auto const stops = courseStops(map, data, *course);
	auto const mm_to_m = map.getScaleDenominator() / 1000.0;
	
	// The leg from a marked route with which the course begins to the start is not in the length of the course.
	auto const leading_route = stops.size() >= 2
	                           && course->items[std::size_t(stops[0].item)].type == CourseItem::MarkedRoute
	                           && course->items[std::size_t(stops[1].item)].type == CourseItem::Start;
	
	auto has_previous = false;
	for (std::size_t i = 0; i < stops.size(); ++i)
	{
		auto const& stop = stops[i];
		auto const& item = course->items[std::size_t(stop.item)];
		if (part >= 0 && coursePartOfItem(*course, stop.item) != part)
		{
			has_previous = false;
			continue;
		}
		if (has_previous)
		{
			auto const& previous = stops[i - 1];
			CourseRouteLeg leg;
			leg.from = course->items[std::size_t(previous.item)].code;
			leg.to = item.code;
			leg.straight_m = std::hypot(stop.entry.x() - previous.exit.x(), stop.entry.y() - previous.exit.y()) * mm_to_m;
			auto const* stored = kept ? kept->find(leg.from, leg.to) : nullptr;
			if (stored && stored->fits(previous.exit, stop.entry))
			{
				leg.route = resultOf(*stored);
			}
			else if (search)
			{
				leg.route = search->fastest(previous.exit, stop.entry);
				if (store)
					store->put(storedOf(leg, previous.exit, stop.entry));
			}
			else
			{
				leg.known = false;
			}
			// The routes over via points which are kept for the leg.
			if (kept)
			{
				for (auto const* via_route : kept->viaRoutes(leg.from, leg.to))
				{
					if (!via_route->found || !via_route->fits(previous.exit, stop.entry))
						continue;
					leg.via_routes.push_back(resultOf(*via_route));
					leg.via_indexes.push_back(via_route->index);
				}
			}
			if (!(leading_route && i == 1))
				routes.course_length_m += leg.straight_m;
			routes.legs.push_back(std::move(leg));
		}
		if (item.type == CourseItem::MarkedRoute)
		{
			// The marked route is followed: its path, and the time along it.
			CourseRouteLeg leg;
			leg.from = item.code;
			leg.to = item.code;
			leg.marked = true;
			std::vector<MapCoordF> path;
			// The points of the object, the handles of its curves among them, by parts: the route is timed along them.
			std::vector<std::vector<MapCoordF>> polygons;
			auto const* course_object = data.findObject(item.code);
			auto const* object = course_object ? map.findObject(course_object->object) : nullptr;
			if (object && object->getType() == Object::Path)
			{
				object->update();
				auto const& coords = object->getRawCoordinateVector();
				for (auto const& path_part : object->asPath()->parts())
				{
					for (auto const& coord : path_part.path_coords)
						path.push_back(coord.pos);
					polygons.emplace_back();
					for (auto i = path_part.first_index; i <= path_part.last_index && i < coords.size(); ++i)
						polygons.back().push_back(MapCoordF(coords[i]));
				}
			}
			for (std::size_t p = 1; p < path.size(); ++p)
				leg.straight_m += std::hypot(path[p].x() - path[p - 1].x(), path[p].y() - path[p - 1].y()) * mm_to_m;
			auto const* stored = kept && path.size() >= 2 ? kept->find(leg.from, leg.to) : nullptr;
			if (stored && stored->found && stored->fits(path.front(), path.back()))
			{
				leg.route = resultOf(*stored);
			}
			else if (search)
			{
				leg.route.found = path.size() >= 2;
				leg.route.length_m = leg.straight_m;
				// the design notes, "Route analysis: the time along a marked route".
				for (auto const& polygon : polygons)
				{
					for (std::size_t p = 1; p < polygon.size(); ++p)
					{
						auto const piece = search->followLine(polygon[p - 1], polygon[p]);
						leg.route.time_s += piece.time_s;
						leg.route.climb_m += piece.climb_m;
					}
				}
				leg.route.points = path;
				if (store && leg.route.found)
					store->put(storedOf(leg, path.front(), path.back()));
			}
			else
			{
				leg.known = false;
			}
			routes.course_length_m += leg.straight_m;
			routes.legs.push_back(std::move(leg));
		}
		has_previous = true;
	}
	
	for (auto const& leg : routes.legs)
	{
		if (leg.known && leg.route.found)
		{
			routes.length_m += leg.route.length_m;
			routes.time_s += leg.route.time_s;
			routes.climb_m += leg.route.climb_m;
		}
		else
		{
			routes.length_m += leg.straight_m;
			++(leg.known ? routes.missing : routes.unknown);
		}
	}
	routes.analysed = !routes.legs.empty();
	return routes;
}

AllCourseRoutes allRoutesOf(const Map& map, const CourseData& data, const StoredRoutes* kept, StoredRoutes* store, RouteSearch* search)
{
	AllCourseRoutes all;
	for (auto const& course : data.courses)
	{
		if (!relayStructure(course).has_variations)
		{
			auto routes = routesOf(map, data, course.name, -1, kept, store, search);
			if (routes.analysed)
				all.tables.push_back(std::move(routes));
			continue;
		}
		// A relay by the variants which have a course map.
		auto const sheets = courseMapSheets(data, course.name, RelayMaps::Variants);
		if (sheets.empty())
		{
			all.skipped.push_back(course.name);
			continue;
		}
		QStringList seen;
		for (auto const& sheet : sheets)
		{
			if (sheet.sides.empty())
				continue;
			auto const& side = sheet.sides.front();
			auto const variant = courseVariantText(data, side);
			if (seen.contains(variant))
				continue;
			seen.push_back(variant);
			auto routes = routesOf(map, withRelayVariant(data, course.name, side.variant), course.name, -1, kept, store, search);
			routes.variant = variant;
			if (routes.analysed)
				all.tables.push_back(std::move(routes));
		}
	}
	return all;
}

/** Where a code stands in the list: by the kind of its object, then by its number. */
struct CodeRank
{
	int kind = 1000;
	QString letters;
	int number = 0;
	QString code;
	
	bool operator<(const CodeRank& other) const
	{
		if (kind != other.kind)
			return kind < other.kind;
		if (letters != other.letters)
			return letters < other.letters;
		if (number != other.number)
			return number < other.number;
		return code < other.code;
	}
};

CodeRank rankOf(const CourseData& data, const QString& code)
{
	CodeRank rank;
	rank.code = code;
	if (auto const* object = data.findObject(code))
		rank.kind = int(object->kind);
	auto digits = 0;
	while (digits < code.size() && code.at(code.size() - 1 - digits).isDigit())
		++digits;
	rank.letters = code.left(code.size() - digits);
	rank.number = code.right(digits).toInt();
	return rank;
}

}  // namespace



CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search)
{
	return routesOf(map, data, course, part, nullptr, nullptr, &search);
}


CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part, RouteSearch& search, StoredRoutes& store)
{
	if (store.isEmpty())
		store.with_heights = search.usesHeights();
	return routesOf(map, data, course, part, &store, &store, &search);
}


CourseRoutes storedCourseRoutes(const Map& map, const CourseData& data, const QString& course, int part)
{
	return routesOf(map, data, course, part, &data.routes, nullptr, nullptr);
}


AllCourseRoutes analyseAllRoutes(const Map& map, const CourseData& data, RouteSearch& search, StoredRoutes& store)
{
	if (store.isEmpty())
		store.with_heights = search.usesHeights();
	return allRoutesOf(map, data, &store, &store, &search);
}


AllCourseRoutes storedAllRoutes(const Map& map, const CourseData& data)
{
	return allRoutesOf(map, data, &data.routes, nullptr, nullptr);
}


AllCourseRoutes analyseAllRoutesOfMap(Map& map, StoredRoutes& store)
{
	store = {};
	auto const settings = map.courseData().route;
	auto const terrain = buildRouteTerrain(map, settings);
	RouteSearch search(terrain, settings, map.getScaleDenominator());
	setRouteHeights(search, map);
	return analyseAllRoutes(map, map.courseData(), search, store);
}


ViaRouteAdded addViaRoute(StoredRoutes& store, RouteSearch& search, const QString& from, const QString& to, const std::vector<MapCoordF>& via)
{
	ViaRouteAdded added;
	auto const* fastest = store.find(from, to);
	if (!fastest || !fastest->found || fastest->isMarkedRoute() || fastest->points.size() < 2)
	{
		added.reason = ViaRouteAdded::NoFastestRoute;
		return added;
	}
	added.fastest_length_m = fastest->length_m;
	added.fastest_time_s = fastest->time_s;
	if (search.usesHeights() != store.with_heights)
	{
		added.reason = ViaRouteAdded::OtherHeights;
		return added;
	}
	added.route = search.fastestVia(fastest->points.front(), via, fastest->points.back());
	if (!added.route.found)
	{
		added.reason = ViaRouteAdded::NoRoute;
		return added;
	}
	StoredRoute stored;
	stored.from = from;
	stored.to = to;
	stored.index = store.nextIndex(from, to);
	stored.via = via;
	stored.points = added.route.points;
	stored.length_m = added.route.length_m;
	stored.time_s = added.route.time_s;
	stored.climb_m = added.route.climb_m;
	added.index = stored.index;
	added.reason = ViaRouteAdded::Added;
	// The fastest route is no longer looked at after this: the store may move it.
	store.put(std::move(stored));
	return added;
}


namespace {

/** How far a place is from a line of points, in mm. */
double distanceTo(const std::vector<MapCoordF>& points, const MapCoordF& place)
{
	auto nearest = -1.0;
	for (std::size_t i = 1; i < points.size(); ++i)
	{
		auto const& a = points[i - 1];
		auto const& b = points[i];
		auto const dx = b.x() - a.x();
		auto const dy = b.y() - a.y();
		auto const square = dx * dx + dy * dy;
		auto const along = square > 0.0 ? qBound(0.0, ((place.x() - a.x()) * dx + (place.y() - a.y()) * dy) / square, 1.0) : 0.0;
		auto const distance = std::hypot(place.x() - (a.x() + along * dx), place.y() - (a.y() + along * dy));
		if (nearest < 0.0 || distance < nearest)
			nearest = distance;
	}
	return nearest;
}

}  // namespace


int routeLegAt(const CourseRoutes& routes, const MapCoordF& place, double reach)
{
	auto found = -1;
	auto nearest = reach;
	for (std::size_t i = 0; i < routes.legs.size(); ++i)
	{
		auto const& leg = routes.legs[i];
		if (leg.marked || !leg.known || !leg.route.found)
			continue;
		auto const distance = distanceTo(leg.route.points, place);
		if (distance >= 0.0 && distance <= nearest)
		{
			nearest = distance;
			found = int(i);
		}
	}
	return found;
}


bool viaRouteAt(const CourseRoutes& routes, const MapCoordF& place, double reach, int& leg, int& via)
{
	auto found = false;
	auto nearest = reach;
	for (std::size_t i = 0; i < routes.legs.size(); ++i)
	{
		auto const& via_routes = routes.legs[i].via_routes;
		for (std::size_t v = 0; v < via_routes.size(); ++v)
		{
			auto const distance = distanceTo(via_routes[v].points, place);
			if (distance >= 0.0 && distance <= nearest)
			{
				nearest = distance;
				leg = int(i);
				via = int(v);
				found = true;
			}
		}
	}
	return found;
}


RouteHeights routeHeightsOf(Map& map)
{
	auto const* georeferencing = &map.getGeoreferencing();
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, georeferencing->getProjectedCRSSpec());
	if (!model)
		return {};
	return [model, georeferencing](const MapCoordF& place) { return model->at(georeferencing->toProjectedCoords(place)); };
}


double routeGridCellOf(Map& map)
{
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, map.getGeoreferencing().getProjectedCRSSpec());
	return model ? model->cellSize() : 0.0;
}


void setRouteHeights(RouteSearch& search, Map& map)
{
	search.setHeights(routeHeightsOf(map));
	search.setGridCell(routeGridCellOf(map));
}


QString formatRouteTime(double seconds)
{
	auto const whole = qRound64(seconds);
	return QStringLiteral("%1:%2").arg(whole / 60, 2, 10, QLatin1Char('0')).arg(whole % 60, 2, 10, QLatin1Char('0'));
}


QString courseRoutesText(const CourseRoutes& routes)
{
	QStringList lines;
	auto const name = routes.variant.isEmpty() ? routes.course : QString(routes.course + QLatin1Char('-') + routes.variant);
	lines.push_back(routes.with_heights
	                ? QStringLiteral("Course %1\tFastest route (leg)\tFastest route (total)\tClimb along route\tTime along route").arg(name)
	                : QStringLiteral("Course %1\tFastest route (leg)\tFastest route (total)\tTime along route").arg(name));
	auto total = 0.0;
	for (auto const& leg : routes.legs)
	{
		auto const leg_name = leg.marked ? QString(leg.from + QLatin1String(": ")) : QString(QLatin1String("Leg ") + leg.from + QLatin1Char('-') + leg.to);
		if (!leg.known || !leg.route.found)
		{
			total += leg.straight_m;
			lines.push_back(leg_name + QLatin1Char('\t') + (leg.known ? tr("no route") : tr("not analysed")));
			continue;
		}
		total += leg.route.length_m;
		lines.push_back(leg_name + QLatin1Char('\t') + metres(leg.route.length_m) + QLatin1Char('\t') + metres(total) + QLatin1Char('\t')
		                + (routes.with_heights ? QString(metres(leg.route.climb_m) + QLatin1Char('\t')) : QString{})
		                + formatRouteTime(leg.route.time_s));
	}
	// The length of the course as the course box rounds it, to 10 m.
	auto const course_length = std::floor(routes.course_length_m / 10.0 + 0.5) * 10.0;
	lines.push_back(QString{});
	lines.push_back(QStringLiteral("Length = %1 m").arg(qRound64(course_length)));
	lines.push_back(QStringLiteral("Extra length = %1 m").arg(qRound64(routes.length_m) - qRound64(course_length)));
	if (routes.with_heights)
		lines.push_back(QStringLiteral("Climb along course = %1 m").arg(qRound64(routes.climb_m)));
	lines.push_back(QStringLiteral("Time along course = %1").arg(formatRouteTime(routes.time_s)));
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}


QString allRoutesText(const CourseData& data, const StoredRoutes& store, const AllCourseRoutes& all)
{
	// Every route once, in the order of the objects at which they begin, a marked route before the legs which leave it.
	struct Entry
	{
		CodeRank from;
		bool is_leg;
		CodeRank to;
		const StoredRoute* route;
	};
	std::vector<Entry> entries;
	entries.reserve(store.routes.size());
	for (auto const& route : store.routes)
	{
		// The fastest routes; those over via points are in no list.
		if (route.index > 0)
			continue;
		entries.push_back({ rankOf(data, route.from), !route.isMarkedRoute(), rankOf(data, route.to), &route });
	}
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if (a.from < b.from || b.from < a.from)
			return a.from < b.from;
		if (a.is_leg != b.is_leg)
			return b.is_leg;
		return a.to < b.to;
	});
	
	QString text;
	for (auto const& entry : entries)
	{
		auto const& route = *entry.route;
		text += route.isMarkedRoute() ? QString(route.from + QLatin1String(": \t"))
		                              : QString(QLatin1String("Leg ") + route.from + QLatin1Char('-') + route.to + QLatin1String(": \t"));
		text += route.found ? QString(QString::number(qRound64(route.length_m)) + QLatin1Char('m')) : tr("no route");
		text += QLatin1Char('\n');
	}
	text += QLatin1Char('\n');
	
	// The tables: a line of dashes before every course, the variants of a relay under one.
	QString course;
	auto first = true;
	for (auto const& table : all.tables)
	{
		if (first || table.course != course)
			text += QLatin1String("-----\n");
		first = false;
		course = table.course;
		text += courseRoutesText(table);
		text += QLatin1Char('\n');
	}
	return text;
}


}  // namespace OpenOrienteering
