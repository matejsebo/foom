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


#include "points_along_tool.h"

#include <memory>
#include <utility>
#include <vector>

#include "core/objects/object.h"
#include "core/ops/op.h"
#include "core/symbols/point_symbol.h"
#include "gui/map/map_editor.h"


namespace OpenOrienteering {

PointsAlongTool::PointsAlongTool(MapEditorController* editor, QAction* tool_action, PointSymbol* point, const DistributePointsTool::Settings& settings)
: DrawPathTool(editor, tool_action, true, false)
, point(point)
, settings(settings)
{
	connect(this, &DrawLineAndAreaTool::pathFinished, this, &PointsAlongTool::placePoints);
}

PointsAlongTool::~PointsAlongTool() = default;


void PointsAlongTool::placePoints(PathObject* path)
{
	if (!path || !point || path->getCoordinateCount() < 2)
		return;
	std::vector<PointObject*> points;
	DistributePointsTool::execute(path, point, settings, points);
	ops::OpResult result;
	for (auto* object : points)
		result.out.emplace_back(object);
	editor->applyOpResult(std::move(result));
}


}  // namespace OpenOrienteering
