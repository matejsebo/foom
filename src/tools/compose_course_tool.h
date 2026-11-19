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


#ifndef FOOM_COMPOSE_COURSE_TOOL_H
#define FOOM_COMPOSE_COURSE_TOOL_H

#include <QObject>
#include <QPointer>
#include <QString>

#include "core/map_coord.h"
#include "course/course_compose.h"
#include "tools/tool_base.h"

class QAction;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace OpenOrienteering {

class CourseWidget;
class MapEditorController;
class MapWidget;


/**
 * Composes the course which is chosen in the course dock: a click puts a
 * start, a control or an object which is there at its end, a double click
 * the finish; a click on a leg begins inserting there; Ctrl and a click
 * take an object out of the course.
 *
 * What the clicks do is CourseComposer's business. See
 * the design notes, "Composing a course and the bulk commands".
 */
class ComposeCourseTool : public MapEditorToolBase
{
Q_OBJECT
public:
	ComposeCourseTool(MapEditorController* editor, QAction* tool_action, CourseWidget* dock);
	~ComposeCourseTool() override;
	
	const CourseComposer& composer() const { return course_composer; }
	
	bool mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	void leaveEvent(QEvent* event) override;
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	
	void clickRelease() override;
	void dragFinish() override;
	void mouseMove() override;
	bool keyPress(QKeyEvent* event) override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	void objectSelectionChangedImpl() override;
	
private:
	/** The reach of a click, in mm. */
	double reach() const;
	void courseChanged();
	void say(const CourseComposer::Result& result);
	
	CourseComposer course_composer;
	QPointer<CourseWidget> dock;
	QString message;
	/** The release which follows a double click is no click. */
	bool skip_release = false;
	bool mouse_in_map = false;
};


}  // namespace OpenOrienteering

#endif
