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


#ifndef OPENORIENTEERING_SIZED_SHAPE_TOOL_H
#define OPENORIENTEERING_SIZED_SHAPE_TOOL_H

#include <QObject>

#include "core/ops/sized_shape.h"
#include "tools/tool_base.h"

class QAction;
class QKeyEvent;

namespace OpenOrienteering {

class MapEditorController;


/**
 * Places a shape of a given size with every click.
 *
 * See the design notes, "Shapes of a given size".
 */
class SizedShapeTool : public MapEditorToolBase
{
Q_OBJECT
public:
	SizedShapeTool(MapEditorController* editor, QAction* tool_action, const ops::SizedShapeParams& params);
	~SizedShapeTool() override;

protected:
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;

private:
	ops::SizedShapeParams params;
};


}  // namespace OpenOrienteering

#endif
