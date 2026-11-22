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


#include "route_tool.h"

#include <cmath>
#include <cstddef>
#include <utility>

#include <Qt>
#include <QColor>
#include <QCursor>
#include <QFlags>
#include <QKeyEvent>
#include <QLatin1String>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>

#include "core/map.h"
#include "core/map_view.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_store.h"
#include "course/route_terrain.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/modifier_key.h"
#include "gui/widgets/course_widget.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

RouteTool::RouteTool(MapEditorController* editor, QAction* tool_action, CourseWidget* dock, Mode mode)
: MapEditorToolBase { QCursor(QPixmap(QString::fromLatin1(":/images/cursor-draw-point.png")), 11, 11), Other, editor, tool_action }
, tool_mode { mode }
, dock { dock }
{
	// The routes which are kept may have changed: what the tool draws is drawn anew.
	connect(map(), &Map::courseDataChanged, this, [this]() { updateDirtyRect(); });
}

RouteTool::~RouteTool() = default;


void RouteTool::initImpl()
{
	updateStatusText();
}


void RouteTool::updateStatusText()
{
	QString text;
	if (tool_mode == FreeLeg)
	{
		text = tr("<b>Drag</b>: Search the fastest route from where the drag begins to where it ends. ");
	}
	else if (has_leg && !pending_via.empty())
	{
		text = tr("<b>Drag or click</b>: Add the next via point. ") + tr("<b>%1</b>: Add more after it. ").arg(ModifierKey::shift())
		       + tr("<b>%1</b>: Drop the via points. ").arg(ModifierKey::escape());
	}
	else
	{
		text = tr("<b>Drag from a route</b>: Lead the route of the leg over the place where the drag ends. ")
		       + tr("<b>%1</b>: Add more via points first. ").arg(ModifierKey::shift())
		       + tr("<b>%1+Click</b>: Remove a route over via points. ").arg(ModifierKey::control());
	}
	if (!message.isEmpty())
		text = message + QLatin1String(" | ") + text;
	setStatusBarText(text);
}


void RouteTool::say(const QString& text)
{
	message = text;
	updateStatusText();
	updateDirtyRect();
}


double RouteTool::reach() const
{
	return cur_map_widget ? 0.001 * cur_map_widget->getMapView()->pixelToLength(clickTolerance()) : 0.5;
}


RouteSearch& RouteTool::routeSearch()
{
	if (!search)
	{
		auto const settings = map()->courseData().route;
		// the design notes, "Route analysis: the cache of the rasters"
		auto const path = editor->getWindow() ? routeTerrainCachePath(editor->getWindow()->currentPath()) : QString{};
		terrain = std::make_unique<RouteTerrain>(cachedRouteTerrain(*map(), settings, path));
		search = std::make_unique<RouteSearch>(*terrain, settings, map()->getScaleDenominator());
		setRouteHeights(*search, *map());
	}
	return *search;
}


CourseRoutes RouteTool::shownRoutes() const
{
	if (!dock || dock->currentCourse().isEmpty())
		return {};
	return storedCourseRoutes(*map(), dock->previewData(), dock->currentCourse(), dock->previewPart());
}


void RouteTool::clickPress()
{
	pressed = true;
	if (tool_mode != ViaPoints || has_leg || (active_modifiers & Qt::ControlModifier))
		return;
	// The route on which the key goes down names the leg.
	auto const routes = shownRoutes();
	auto const leg = routeLegAt(routes, click_pos_map, reach());
	if (leg < 0)
		return;
	auto const& found = routes.legs[std::size_t(leg)];
	has_leg = true;
	leg_from = found.from;
	leg_to = found.to;
	leg_begin = found.route.points.front();
	leg_end = found.route.points.back();
	message.clear();
	updateStatusText();
	updateDirtyRect();
}


void RouteTool::clickRelease()
{
	released();
}


void RouteTool::dragMove()
{
	updateDirtyRect();
}


void RouteTool::dragFinish()
{
	released();
}


void RouteTool::released()
{
	if (!pressed)
		return;
	pressed = false;
	auto const moved = std::hypot(cur_pos_map.x() - click_pos_map.x(), cur_pos_map.y() - click_pos_map.y()) > reach();
	if (tool_mode == FreeLeg)
	{
		if (moved)
			searchFreeLeg(click_pos_map, cur_pos_map);
		else
			say(tr("Drag from the beginning of the leg to its end."));
		return;
	}
	
	if (active_modifiers & Qt::ControlModifier)
	{
		removeViaRouteAt(cur_pos_map);
		return;
	}
	if (!has_leg)
	{
		say(tr("Begin on a route of the course which is chosen in the course dock. \"Show routes\" shows them."));
		return;
	}
	// A click on the route itself is no via point; a click after the first via point is the next one.
	if (!moved && pending_via.empty())
	{
		dropPending();
		say(tr("Drag from the route to the place over which it shall lead."));
		return;
	}
	addViaPoint(cur_pos_map, bool(active_modifiers & Qt::ShiftModifier));
}


void RouteTool::addViaPoint(const MapCoordF& place, bool more)
{
	pending_via.push_back(place);
	if (more)
	{
		say(tr("%n via point(s).", nullptr, int(pending_via.size())));
		return;
	}
	
	auto data = map()->courseData();
	auto const added = addViaRoute(data.routes, routeSearch(), leg_from, leg_to, pending_via);
	auto const leg_name = QString(leg_from + QLatin1Char('-') + leg_to);
	dropPending();
	switch (added.reason)
	{
	case ViaRouteAdded::Added:
		last_route = added.route;
		changeCourseData(*map(), std::move(data));
		say(tr("Leg %1 over the via points: %2 m in %3; the fastest route has %4 m in %5.")
		    .arg(leg_name).arg(qRound(added.route.length_m)).arg(formatRouteTime(added.route.time_s))
		    .arg(qRound(added.fastest_length_m)).arg(formatRouteTime(added.fastest_time_s)));
		break;
	case ViaRouteAdded::NoRoute:
		say(tr("No route leads over the via points."));
		break;
	case ViaRouteAdded::OtherHeights:
		say(tr("The routes were analysed with another elevation setting than the map has now. Analyse the routes again first."));
		break;
	case ViaRouteAdded::NoFastestRoute:
		say(tr("The leg has no route. Analyse the routes first."));
		break;
	}
}


void RouteTool::removeViaRouteAt(const MapCoordF& place)
{
	auto const routes = shownRoutes();
	auto leg = -1;
	auto via = -1;
	if (!viaRouteAt(routes, place, reach(), leg, via))
	{
		say(tr("No route over via points is here."));
		return;
	}
	auto const& found = routes.legs[std::size_t(leg)];
	auto data = map()->courseData();
	if (data.routes.remove(found.from, found.to, found.via_indexes[std::size_t(via)]))
		changeCourseData(*map(), std::move(data));
	say(tr("The route over via points of the leg %1 was removed.").arg(QString(found.from + QLatin1Char('-') + found.to)));
}


void RouteTool::searchFreeLeg(const MapCoordF& from, const MapCoordF& to)
{
	last_route = routeSearch().fastest(from, to);
	auto const straight = std::hypot(to.x() - from.x(), to.y() - from.y()) * map()->getScaleDenominator() / 1000.0;
	if (last_route.found)
	{
		say(tr("The fastest route: %1 m in %2; the straight line has %3 m.")
		    .arg(qRound(last_route.length_m)).arg(formatRouteTime(last_route.time_s)).arg(qRound(straight)));
	}
	else
	{
		say(tr("No route found."));
	}
}


void RouteTool::dropPending()
{
	has_leg = false;
	leg_from.clear();
	leg_to.clear();
	pending_via.clear();
}


bool RouteTool::keyPress(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && has_leg)
	{
		dropPending();
		pressed = false;
		say({});
		return true;
	}
	return false;
}


void RouteTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	if (tool_mode == FreeLeg)
	{
		// The route of the leg which was drawn last, and the leg which is being drawn.
		if (last_route.found && last_route.points.size() >= 2)
		{
			QPolygonF line;
			for (auto const& point : last_route.points)
				line.push_back(widget->mapToViewport(point));
			// As wide as the routes which the editor draws with the course, 0.3 mm of the map.
			auto const width = qMax(qreal(2 * scaleFactor()), widget->getMapView()->lengthToPixel(300.0));
			QPen pen(QColor(225, 0, 50), width);
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			painter->setPen(pen);
			painter->drawPolyline(line);
		}
		if (pressed)
		{
			painter->setPen(QPen(QColor(225, 0, 50), scaleFactor(), Qt::DashLine));
			painter->drawLine(widget->mapToViewport(click_pos_map), widget->mapToViewport(cur_pos_map));
		}
	}
	else if (has_leg)
	{
		// From the beginning of the leg over the via points and the mouse to its end.
		QPolygonF line;
		line.push_back(widget->mapToViewport(leg_begin));
		for (auto const& point : pending_via)
			line.push_back(widget->mapToViewport(point));
		line.push_back(widget->mapToViewport(cur_pos_map));
		line.push_back(widget->mapToViewport(leg_end));
		painter->setPen(QPen(QColor(0, 90, 225), scaleFactor(), Qt::DashLine));
		painter->drawPolyline(line);
	}
	painter->restore();
}


int RouteTool::updateDirtyRectImpl(QRectF& rect)
{
	auto any = false;
	if (tool_mode == FreeLeg)
	{
		for (auto const& point : last_route.points)
		{
			rectIncludeSafe(rect, point);
			any = true;
		}
		if (pressed)
		{
			rectIncludeSafe(rect, click_pos_map);
			rectIncludeSafe(rect, cur_pos_map);
			any = true;
		}
	}
	else if (has_leg)
	{
		rectIncludeSafe(rect, leg_begin);
		rectIncludeSafe(rect, leg_end);
		rectIncludeSafe(rect, cur_pos_map);
		for (auto const& point : pending_via)
			rectIncludeSafe(rect, point);
		any = true;
	}
	if (!any)
		return -1;
	// Half the width of the route, which is 0.3 mm of the map.
	auto const half_width = cur_map_widget ? int(std::ceil(cur_map_widget->getMapView()->lengthToPixel(300.0) / 2)) : 0;
	return qMax(3, half_width + 1);
}


void RouteTool::objectSelectionChangedImpl()
{
	updateStatusText();
}


}  // namespace OpenOrienteering
