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


#include "rectangle_box_tool.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <QtGlobal>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/object_groups.h"
#include "core/objects/object.h"
#include "core/ops/op.h"
#include "core/ops/rectangle_box.h"
#include "gui/map/map_editor.h"
#include "tools/draw_rectangle_tool.h"


namespace OpenOrienteering {

RectangleBoxTool::RectangleBoxTool(MapEditorController* editor, QAction* tool_action, const ops::RectangleBoxParams& params)
: DrawRectangleTool(editor, tool_action, false)
, params(params)
{
	// nothing else
}

RectangleBoxTool::~RectangleBoxTool() = default;


void RectangleBoxTool::finishDrawing()
{
	// the design notes, "Rectangle objects": the box spans the
	// first two corners and the last corner drawn.
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
			ops::RectangleBoxGeometry geometry;
			geometry.top_left = MapCoordF(coords[0]);
			geometry.top_right = MapCoordF(coords[1]);
			geometry.bottom_left = MapCoordF(coords[last]);
			auto box_params = params;
			box_params.frame = drawing_symbol;
			box_params.group = nextObjectGroupId(*map());
			auto result = ops::rectangleBox(*map(), geometry, box_params);
			abortDrawing();
			editor->applyOpResult(std::move(result));
			return;
		}
	}
	DrawRectangleTool::finishDrawing();
}


}  // namespace OpenOrienteering
