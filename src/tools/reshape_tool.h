/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_TOOL_RESHAPE_H
#define OPENORIENTEERING_TOOL_RESHAPE_H

#include <QObject>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"
#include "tools/tool.h"

class QAction;
class QCursor;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace OpenOrienteering {

class DrawLineAndAreaTool;
class MapEditorController;
class MapWidget;
class PathObject;


/**
 * A tool which replaces a stretch of a line or area border by a drawn stroke.
 *
 * The stroke is drawn with a helper DrawPathTool, starting on the object.
 * See the design notes, "Reshape".
 */
class ReshapeTool : public MapEditorTool
{
Q_OBJECT
public:
	ReshapeTool(MapEditorController* editor, QAction* tool_action);
	~ReshapeTool() override;
	
	void init() override;
	const QCursor& getCursor() const override;
	void finishEditing() override;
	
	bool mousePressEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseMoveEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseReleaseEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	void leaveEvent(QEvent* event) override;
	
	bool keyPressEvent(QKeyEvent* event) override;
	bool keyReleaseEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	
	void draw(QPainter* painter, MapWidget* widget) override;
	
	/**
	 * Replaces the stretch of the object under the stroke's start by the stroke.
	 *
	 * Returns false, after showing the reason, when there is no such object,
	 * the stroke's end is not on it, or the operation fails.
	 */
	bool reshapeWith(const MapCoordVector& stroke, bool other_arc);

public slots:
	void pathDirtyRectChanged(const QRectF& rect);
	void pathAborted();
	void pathFinished(OpenOrienteering::PathObject* stroke);

protected:
	/** Returns the nearest path object within the snap distance of the position, a selected one first. */
	PathObject* findTarget(const MapCoordF& map_coord, double snap_distance) const;
	
	/** Returns the snap distance from the settings in mm of map coordinates. */
	double snapDistance() const;
	
	void updateStatusText();
	void updateDirtyRect(const QRectF* path_rect = nullptr);

private:
	DrawLineAndAreaTool* path_tool = nullptr;
};


}  // namespace OpenOrienteering

#endif
