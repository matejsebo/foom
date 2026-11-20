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

#include <cmath>
#include <cstddef>

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
#include "course/course_parts.h"
#include "course/course_render.h"
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

}  // namespace



CourseRoutes analyseCourseRoutes(const Map& map, const CourseData& data, const QString& course_name, int part, RouteSearch& search)
{
	CourseRoutes routes;
	routes.course = course_name;
	routes.part = part;
	routes.with_heights = search.usesHeights();
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
	
	// The run begins at the first start; a marked route before it leads the runners there.
	std::size_t first = 0;
	for (std::size_t i = 0; i < stops.size(); ++i)
	{
		if (course->items[std::size_t(stops[i].item)].type == CourseItem::Start)
		{
			first = i;
			break;
		}
	}
	
	auto has_previous = false;
	for (std::size_t i = first; i < stops.size(); ++i)
	{
		auto const& stop = stops[i];
		auto const& item = course->items[std::size_t(stop.item)];
		if (part >= 0 && coursePartOfItem(*course, stop.item) != part)
		{
			has_previous = false;
			continue;
		}
		if (has_previous && !stop.after_break)
		{
			auto const& previous = stops[i - 1];
			CourseRouteLeg leg;
			leg.from = course->items[std::size_t(previous.item)].code;
			leg.to = item.code;
			leg.straight_m = std::hypot(stop.entry.x() - previous.exit.x(), stop.entry.y() - previous.exit.y()) * mm_to_m;
			leg.route = search.fastest(previous.exit, stop.entry);
			routes.legs.push_back(leg);
		}
		if (item.type == CourseItem::MarkedRoute)
		{
			// The marked route is followed: its path, and the time along it.
			CourseRouteLeg leg;
			leg.from = item.code;
			leg.to = item.code;
			leg.marked = true;
			auto const* course_object = data.findObject(item.code);
			auto const* object = course_object ? map.findObject(course_object->object) : nullptr;
			if (object && object->getType() == Object::Path)
			{
				object->update();
				for (auto const& path_part : object->asPath()->parts())
				{
					for (auto const& coord : path_part.path_coords)
						leg.route.points.push_back(coord.pos);
				}
			}
			for (std::size_t p = 1; p < leg.route.points.size(); ++p)
			{
				auto const& a = leg.route.points[p - 1];
				auto const& b = leg.route.points[p];
				leg.route.length_m += std::hypot(b.x() - a.x(), b.y() - a.y()) * mm_to_m;
				leg.route.time_s += search.straightTime(a, b);
			}
			leg.route.found = leg.route.points.size() >= 2;
			leg.straight_m = leg.route.length_m;
			routes.legs.push_back(leg);
		}
		has_previous = true;
	}
	
	for (auto const& leg : routes.legs)
	{
		routes.course_length_m += leg.straight_m;
		if (leg.route.found)
		{
			routes.length_m += leg.route.length_m;
			routes.time_s += leg.route.time_s;
			routes.climb_m += leg.route.climb_m;
		}
		else
		{
			routes.length_m += leg.straight_m;
			++routes.missing;
		}
	}
	routes.analysed = !routes.legs.empty();
	return routes;
}


RouteHeights routeHeightsOf(Map& map)
{
	auto const* georeferencing = &map.getGeoreferencing();
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, georeferencing->getProjectedCRSSpec());
	if (!model)
		return {};
	return [model, georeferencing](const MapCoordF& place) { return model->at(georeferencing->toProjectedCoords(place)); };
}


QString formatRouteTime(double seconds)
{
	auto const whole = qRound64(seconds);
	return QStringLiteral("%1:%2").arg(whole / 60, 2, 10, QLatin1Char('0')).arg(whole % 60, 2, 10, QLatin1Char('0'));
}


QString courseRoutesText(const CourseRoutes& routes)
{
	QStringList lines;
	lines.push_back(routes.with_heights
	                ? QStringLiteral("Course %1\tFastest route (leg)\tFastest route (total)\tClimb along route\tTime along route").arg(routes.course)
	                : QStringLiteral("Course %1\tFastest route (leg)\tFastest route (total)\tTime along route").arg(routes.course));
	auto total = 0.0;
	for (auto const& leg : routes.legs)
	{
		auto const name = leg.marked ? QString(leg.from + QLatin1String(": ")) : QString(QLatin1String("Leg ") + leg.from + QLatin1Char('-') + leg.to);
		if (!leg.route.found)
		{
			total += leg.straight_m;
			lines.push_back(name + QLatin1Char('\t') + tr("no route"));
			continue;
		}
		total += leg.route.length_m;
		lines.push_back(name + QLatin1Char('\t') + metres(leg.route.length_m) + QLatin1Char('\t') + metres(total) + QLatin1Char('\t')
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


}  // namespace OpenOrienteering
