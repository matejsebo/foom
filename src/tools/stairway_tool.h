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


#ifndef OPENORIENTEERING_STAIRWAY_TOOL_H
#define OPENORIENTEERING_STAIRWAY_TOOL_H

#include <QObject>

#include "tools/draw_rectangle_tool.h"

class QAction;

namespace OpenOrienteering {

class MapEditorController;


/**
 * Draws the footprint of a stairway as a rectangle and makes its treads.
 *
 * See the design notes, "Stairway".
 */
class StairwayTool : public DrawRectangleTool
{
Q_OBJECT
public:
	StairwayTool(MapEditorController* editor, QAction* tool_action, double tread_mm);
	~StairwayTool() override;

protected:
	void finishDrawing() override;
	bool showsLegibilityFeedback() const override;  // foom: the footprint is no object

private:
	double tread_mm;
};


}  // namespace OpenOrienteering

#endif
