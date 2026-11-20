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


#ifndef FOOM_ROUTE_TOOL_H
#define FOOM_ROUTE_TOOL_H

#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>

#include "core/map_coord.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_terrain.h"
#include "tools/tool_base.h"

class QAction;
class QKeyEvent;
class QPainter;
class QRectF;

namespace OpenOrienteering {

class CourseWidget;
class MapEditorController;
class MapWidget;


/**
 * The tool of the route analysis, in one of two modes.
 *
 * Via points: the mouse key goes down on the route of a leg of the course
 * of the dock, the mouse is dragged to a place, and the key going up
 * searches the route of the leg over this place and keeps it with the
 * leg; with Shift more places are added first. Ctrl and a click take a
 * route over via points out.
 *
 * Free leg: a drag from one place to another searches the fastest route
 * between the two, which is drawn and not kept.
 *
 * See the design notes, "Route analysis: via points and the
 * leg by hand".
 */
class RouteTool : public MapEditorToolBase
{
Q_OBJECT
public:
	enum Mode { ViaPoints, FreeLeg };
	
	RouteTool(MapEditorController* editor, QAction* tool_action, CourseWidget* dock, Mode mode);
	~RouteTool() override;
	
	Mode mode() const { return tool_mode; }
	/** The route which was searched last. */
	const RouteResult& lastRoute() const { return last_route; }
	/** The via points which wait for more. */
	const std::vector<MapCoordF>& pendingViaPoints() const { return pending_via; }
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	
	void clickPress() override;
	void clickRelease() override;
	void dragMove() override;
	void dragFinish() override;
	bool keyPress(QKeyEvent* event) override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	void objectSelectionChangedImpl() override;
	
private:
	/** The reach of a click, in mm. */
	double reach() const;
	/** The search with the settings, the rasters and the heights of the map; made when it is first needed. */
	RouteSearch& routeSearch();
	/** The kept routes of the course, the part and the variant of the dock. */
	CourseRoutes shownRoutes() const;
	void released();
	void addViaPoint(const MapCoordF& place, bool more);
	void removeViaRouteAt(const MapCoordF& place);
	void searchFreeLeg(const MapCoordF& from, const MapCoordF& to);
	void dropPending();
	void say(const QString& text);
	
	Mode tool_mode;
	QPointer<CourseWidget> dock;
	std::unique_ptr<RouteTerrain> terrain;
	std::unique_ptr<RouteSearch> search;
	
	/** The leg to which via points are being added: its codes and the ends of its fastest route. */
	bool has_leg = false;
	QString leg_from;
	QString leg_to;
	MapCoordF leg_begin;
	MapCoordF leg_end;
	std::vector<MapCoordF> pending_via;
	
	bool pressed = false;
	RouteResult last_route;
	QString message;
};


}  // namespace OpenOrienteering

#endif
