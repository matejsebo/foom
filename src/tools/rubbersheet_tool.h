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

#ifndef FOOM_RUBBERSHEET_TOOL_H
#define FOOM_RUBBERSHEET_TOOL_H

#include <vector>

#include <QDialog>
#include <QObject>

#include "core/map_coord.h"
#include "core/map_transform.h"
#include "tools/affine_transform_tool.h"

class QAction;
class QCheckBox;
class QKeyEvent;
class QLabel;
class QPainter;
class QRectF;
class QWidget;

namespace OpenOrienteering {

class Map;
class MapEditorController;
class MapWidget;


/**
 * The map-rubbersheet tool: pairs as for the affine transformation, within a perimeter
 * taken from the selected closed path or the objects' bounding box; Enter opens the options.
 *
 * See the design notes, "Rubbersheeting".
 */
class RubbersheetTool : public AffineTransformTool
{
Q_OBJECT
public:
	RubbersheetTool(MapEditorController* editor, QAction* tool_action);
	~RubbersheetTool() override;
	
	/// The pairs so far and the perimeter, with the default options.
	RubbersheetParams params() const;
	
protected:
	void updateStatusText() override;
	bool keyPress(QKeyEvent* event) override;
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	
private:
	std::vector<MapCoordF> perimeter;
};


/**
 * The options of rubbersheeting, and loading and saving pairs and perimeter in text files.
 */
class RubbersheetDialog : public QDialog
{
Q_OBJECT
public:
	RubbersheetDialog(const Map& map, const RubbersheetParams& params, QWidget* parent);
	~RubbersheetDialog() override;
	
	RubbersheetParams params() const;
	
private:
	void updateSummary();
	
	const Map& map;
	RubbersheetParams current;
	QLabel* summary_label;
	QCheckBox* protected_check;
	QCheckBox* layout_check;
	QCheckBox* decrease_check;
};


}  // namespace OpenOrienteering

#endif
