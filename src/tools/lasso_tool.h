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


#ifndef OPENORIENTEERING_TOOL_LASSO_H
#define OPENORIENTEERING_TOOL_LASSO_H

#include <vector>

#include <QObject>
#include <QString>

#include "core/map_coord.h"
#include "tools/tool_base.h"

class QAction;
class QPainter;
class QRectF;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;


/**
 * A tool which selects the objects inside a freehand outline.
 *
 * See the design notes, "Lasso selection".
 */
class LassoTool : public MapEditorToolBase
{
Q_OBJECT
public:
	LassoTool(MapEditorController* editor, QAction* tool_action);
	~LassoTool() override;
	
	/**
	 * Selects the objects of the current part inside the polygon, or toggles
	 * their selection. Returns true when the selection changed.
	 */
	bool selectPolygon(const std::vector<MapCoordF>& polygon, bool toggle);

protected:
	void initImpl() override;
	void updateStatusText() override;
	
	void dragStart() override;
	void dragMove() override;
	void dragFinish() override;
	void dragCanceled() override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	void objectSelectionChangedImpl() override;

private:
	std::vector<MapCoordF> outline;
};


}  // namespace OpenOrienteering

#endif
