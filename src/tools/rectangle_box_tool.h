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


#ifndef OPENORIENTEERING_RECTANGLE_BOX_TOOL_H
#define OPENORIENTEERING_RECTANGLE_BOX_TOOL_H

#include <QObject>

#include "core/ops/rectangle_box.h"
#include "tools/draw_rectangle_tool.h"

class QAction;

namespace OpenOrienteering {

class MapEditorController;


/**
 * Draws a rectangle box: a frame with an optional numbered grid.
 *
 * The rectangle is drawn as with the rectangle tool; when it is finished,
 * the generated objects replace it. See the design notes,
 * "Rectangle objects".
 */
class RectangleBoxTool : public DrawRectangleTool
{
Q_OBJECT
public:
	RectangleBoxTool(MapEditorController* editor, QAction* tool_action, const ops::RectangleBoxParams& params);
	~RectangleBoxTool() override;
	
protected:
	void finishDrawing() override;
	
private:
	ops::RectangleBoxParams params;
};


}  // namespace OpenOrienteering

#endif
