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

#ifndef FOOM_LOCAL_WARP_TOOL_H
#define FOOM_LOCAL_WARP_TOOL_H

#include <QDialog>
#include <QObject>

#include "core/map_coord.h"
#include "core/map_transform.h"
#include "tools/tool_base.h"

class QAction;
class QCheckBox;
class QKeyEvent;
class QPainter;
class QRectF;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;


/**
 * The map-local-warp tool: each drag from a centre to a target, released within
 * the radius, warps what lies within the radius.
 *
 * See the design notes, "Local transformation".
 */
class LocalWarpTool : public MapEditorToolBase
{
Q_OBJECT
public:
	LocalWarpTool(MapEditorController* editor, QAction* tool_action, const LocalWarpParams& settings);
	~LocalWarpTool() override;
	
protected:
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	
	void dragStart() override;
	void dragMove() override;
	void dragFinish() override;
	void dragCanceled() override;
	bool keyPress(QKeyEvent* event) override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	
private:
	LocalWarpParams settings;
	MapCoordF center;
	MapCoordF current;
	bool dragging = false;
};


/**
 * The radius and the options of the local transformation, asked when the tool starts.
 */
class LocalWarpDialog : public QDialog
{
Q_OBJECT
public:
	explicit LocalWarpDialog(QWidget* parent);
	~LocalWarpDialog() override;
	
	LocalWarpParams settings() const;
	
private:
	QSpinBox* radius_edit;
	QCheckBox* protected_check;
	QCheckBox* layout_check;
};


}  // namespace OpenOrienteering

#endif
