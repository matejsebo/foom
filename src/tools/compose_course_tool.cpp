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


#include "compose_course_tool.h"

#include <Qt>
#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QFlags>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRectF>

#include "core/map.h"
#include "core/map_view.h"
#include "gui/map/map_widget.h"
#include "gui/modifier_key.h"
#include "gui/widgets/course_widget.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

ComposeCourseTool::ComposeCourseTool(MapEditorController* editor, QAction* tool_action, CourseWidget* dock)
: MapEditorToolBase { QCursor(QPixmap(QString::fromLatin1(":/images/cursor-draw-point.png")), 11, 11), Other, editor, tool_action }
, course_composer { *map() }
, dock { dock }
{
	if (dock)
	{
		course_composer.setCourse(dock->currentCourse());
		connect(dock, &CourseWidget::currentCourseChanged, this, &ComposeCourseTool::courseChanged);
	}
	connect(map(), &Map::courseDataChanged, this, &ComposeCourseTool::courseChanged);
}

ComposeCourseTool::~ComposeCourseTool() = default;


void ComposeCourseTool::initImpl()
{
	updateStatusText();
}


void ComposeCourseTool::courseChanged()
{
	if (dock)
		course_composer.setCourse(dock->currentCourse());
	updateStatusText();
	updateDirtyRect();
}


void ComposeCourseTool::updateStatusText()
{
	QString text;
	auto const remove = tr("<b>%1+Click</b>: Remove from the course. ").arg(ModifierKey::control());
	auto const on_line = tr("<b>Click on a line</b>: Insert a control there. ");
	switch (course_composer.refusal())
	{
	case CourseComposer::NoCourse:
		text = tr("<b>Compose course</b>: Choose a course in the course dock. ");
		break;
	case CourseComposer::HasVariation:
		text = tr("<b>Compose course</b>: A course with variations cannot be composed with this tool. ");
		break;
	default:
		if (course_composer.inInsertMode())
			text = tr("<b>Click</b>: Insert a control. ") + tr("<b>%1</b>: Stop inserting. ").arg(ModifierKey::escape()) + remove;
		else if (course_composer.isEmpty())
			text = tr("<b>Click</b>: Add the start. ");
		else if (course_composer.isComplete())
			text = on_line + remove;
		else
			text = tr("<b>Click</b>: Add a control. ") + tr("<b>Double click</b>: Add the finish. ") + on_line + remove;
	}
	if (!message.isEmpty())
		text = message + QLatin1String(" | ") + text;
	setStatusBarText(text);
}


void ComposeCourseTool::say(const CourseComposer::Result& result)
{
	message.clear();
	switch (result.reason)
	{
	case CourseComposer::NoSymbol:
		message = tr("The map has no symbol with the course role: %1. \"Add course symbols\" gives the course symbols.").arg(CourseWidget::kindName(result.kind));
		break;
	case CourseComposer::Complete:
		message = tr("The course has its finish.");
		break;
	case CourseComposer::BesideItself:
		message = tr("%1 stands there already.").arg(result.code);
		break;
	default:
		break;
	}
	updateStatusText();
	updateDirtyRect();
}


double ComposeCourseTool::reach() const
{
	return cur_map_widget ? 0.001 * cur_map_widget->getMapView()->pixelToLength(clickTolerance()) : 0.5;
}


void ComposeCourseTool::clickRelease()
{
	if (skip_release)
	{
		skip_release = false;
		return;
	}
	if (active_modifiers & Qt::ControlModifier)
		say(course_composer.removeAt(click_pos_map, reach()));
	else
		say(course_composer.click(click_pos_map, reach()));
}


void ComposeCourseTool::dragFinish()
{
	// A click during which the mouse slipped is a click.
	clickRelease();
}


bool ComposeCourseTool::mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (event->button() != Qt::LeftButton)
		return false;
	mousePositionEvent(event, map_coord, widget);
	skip_release = true;
	if (!(active_modifiers & Qt::ControlModifier))
		say(course_composer.doubleClick(map_coord, reach()));
	return true;
}


void ComposeCourseTool::mouseMove()
{
	mouse_in_map = true;
	updateDirtyRect();
}


void ComposeCourseTool::leaveEvent(QEvent* /*event*/)
{
	mouse_in_map = false;
	updateDirtyRect();
}


bool ComposeCourseTool::keyPress(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && course_composer.inInsertMode())
	{
		course_composer.leaveInsertMode();
		message.clear();
		updateStatusText();
		updateDirtyRect();
		return true;
	}
	return false;
}


void ComposeCourseTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (!mouse_in_map)
		return;
	auto const ends = course_composer.rubberEnds();
	if (ends.empty())
		return;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(QColor(190, 0, 190), scaleFactor(), Qt::DashLine));
	auto const mouse = widget->mapToViewport(cur_pos_map);
	for (auto const& end : ends)
		painter->drawLine(widget->mapToViewport(end), mouse);
	painter->restore();
}


int ComposeCourseTool::updateDirtyRectImpl(QRectF& rect)
{
	if (!mouse_in_map)
		return -1;
	auto const ends = course_composer.rubberEnds();
	if (ends.empty())
		return -1;
	for (auto const& end : ends)
		rectIncludeSafe(rect, end);
	rectIncludeSafe(rect, cur_pos_map);
	return 3;
}


void ComposeCourseTool::objectSelectionChangedImpl()
{
	updateStatusText();
}


}  // namespace OpenOrienteering
