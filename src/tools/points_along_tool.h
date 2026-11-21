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


#ifndef OPENORIENTEERING_POINTS_ALONG_TOOL_H
#define OPENORIENTEERING_POINTS_ALONG_TOOL_H

#include <QObject>

#include "tools/distribute_points_tool.h"
#include "tools/draw_path_tool.h"

class QAction;

namespace OpenOrienteering {

class MapEditorController;
class PathObject;
class PointSymbol;


/**
 * Draws a path which is not kept and places point objects along it.
 *
 * See the design notes, "Points along a drawn path".
 */
class PointsAlongTool : public DrawPathTool
{
Q_OBJECT
public:
	PointsAlongTool(MapEditorController* editor, QAction* tool_action, PointSymbol* point, const DistributePointsTool::Settings& settings);
	~PointsAlongTool() override;

private:
	void placePoints(OpenOrienteering::PathObject* path);

	PointSymbol* point;
	DistributePointsTool::Settings settings;
};


}  // namespace OpenOrienteering

#endif
