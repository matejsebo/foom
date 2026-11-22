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


#include "stairway_tool.h"

#include <utility>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/object_groups.h"
#include "core/objects/object.h"
#include "core/ops/op.h"
#include "core/ops/stairway.h"
#include "gui/map/map_editor.h"
#include "tools/draw_rectangle_tool.h"


namespace OpenOrienteering {

StairwayTool::StairwayTool(MapEditorController* editor, QAction* tool_action, double tread_mm)
: DrawRectangleTool(editor, tool_action, false)
, tread_mm(tread_mm)
{
	// nothing else
}

StairwayTool::~StairwayTool() = default;


void StairwayTool::finishDrawing()
{
	// As the rectangle box tool: the footprint spans the first two corners and the last corner drawn.
	if (preview_path && angles.size() > 1 && drawing_symbol)
	{
		if (drawingParallelTo(angles[0]))
		{
			deleteClosePoint();
			preview_path->deleteCoordinate(0, false);
			preview_path->parts().front().setClosed(true, true);
		}
		auto const& coords = preview_path->getRawCoordinateVector();
		auto last = coords.size() - 1;
		if (last >= 3 && coords[last].isPositionEqualTo(coords[0]))
			--last;
		if (last >= 2)
		{
			ops::StairwayGeometry geometry;
			geometry.first = MapCoordF(coords[0]);
			geometry.second = MapCoordF(coords[1]);
			geometry.last = MapCoordF(coords[last]);
			ops::StairwayParams params;
			params.symbol = drawing_symbol;
			params.tread_mm = tread_mm;
			params.group = nextObjectGroupId(*map());
			auto result = ops::stairway(*map(), geometry, params);
			abortDrawing();
			editor->applyOpResult(std::move(result));
			return;
		}
	}
	DrawRectangleTool::finishDrawing();
}

bool StairwayTool::showsLegibilityFeedback() const
{
	// the design notes, "Legibility": the footprint becomes no
	// object, so its length, area and outline say nothing about the treads.
	return false;
}


}  // namespace OpenOrienteering
