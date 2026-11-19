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


#ifndef OPENORIENTEERING_TOOL_MOVE_PARALLEL_H
#define OPENORIENTEERING_TOOL_MOVE_PARALLEL_H

#include <memory>

#include <QObject>
#include <QString>

#include "tools/tool_base.h"

class QAction;
class QPainter;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;
class PathObject;


/**
 * A tool which offsets the selected path objects by dragging.
 *
 * The distance is the change of the cursor's signed distance to the selected
 * path nearest the press. See the design notes,
 * "Parallel offset".
 */
class MoveParallelTool : public MapEditorToolBase
{
Q_OBJECT
public:
	MoveParallelTool(MapEditorController* editor, QAction* tool_action);
	~MoveParallelTool() override;

protected:
	void initImpl() override;
	void updateStatusText() override;
	
	void dragStart() override;
	void dragMove() override;
	void dragFinish() override;
	void dragCanceled() override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	void objectSelectionChangedImpl() override;

private:
	/** A copy of the selected path nearest the press, measured against while dragging. */
	std::unique_ptr<PathObject> reference;
	double start_distance = 0.0;
	double distance = 0.0;
};


}  // namespace OpenOrienteering

#endif
