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


#include "sized_shape_tool.h"

#include <utility>

#include <Qt>
#include <QCursor>
#include <QKeyEvent>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/ops/op.h"
#include "gui/map/map_editor.h"
#include "tools/tool.h"


namespace OpenOrienteering {

SizedShapeTool::SizedShapeTool(MapEditorController* editor, QAction* tool_action, const ops::SizedShapeParams& params)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
, params(params)
{
	useTouchCursor(false);
}

SizedShapeTool::~SizedShapeTool() = default;


void SizedShapeTool::updateStatusText()
{
	setStatusBarText(params.kind == ops::SizedShapeParams::Line
	                 ? tr("<b>Click</b>: Start of the line. ")
	                 : tr("<b>Click</b>: Centre of the shape. "));
}

void SizedShapeTool::objectSelectionChangedImpl()
{
	// NOP
}

void SizedShapeTool::clickRelease()
{
	auto shape_params = params;
	shape_params.symbol = editor->activeSymbol();
	auto result = ops::sizedShape(*map(), shape_params, click_pos_map);
	editor->applyOpResult(std::move(result));
}

bool SizedShapeTool::keyPress(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Escape:
	case Qt::Key_Tab:
		deactivate();
		return true;
	}
	return false;
}


}  // namespace OpenOrienteering
