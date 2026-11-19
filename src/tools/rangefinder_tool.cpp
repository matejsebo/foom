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


#include "rangefinder_tool.h"

#include <Qt>
#include <QCursor>
#include <QKeyEvent>
#include <QLabel>

#include "core/map_coord.h"
#include "gui/map/map_editor.h"
#include "tools/tool.h"


namespace OpenOrienteering {

RangefinderReferenceTool::RangefinderReferenceTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
{
	useTouchCursor(false);
}

RangefinderReferenceTool::~RangefinderReferenceTool()
{
	if (help_label)
		editor->deletePopupWidget(help_label);
}


void RangefinderReferenceTool::initImpl()
{
	if (editor->isInMobileMode())
	{
		help_label = new QLabel(tr("Touch the map at the reference point"));
		editor->showPopupWidget(help_label, QString{}, MapEditorController::PopupLocationTop);
	}
}

void RangefinderReferenceTool::updateStatusText()
{
	setStatusBarText(tr("<b>Click</b>: The reference point of the rangefinder. "));
}

void RangefinderReferenceTool::objectSelectionChangedImpl()
{
	// NOP
}

void RangefinderReferenceTool::clickRelease()
{
	editor->rangefinderSetReference(click_pos_map);
	deactivate();
}

bool RangefinderReferenceTool::keyPress(QKeyEvent* event)
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
