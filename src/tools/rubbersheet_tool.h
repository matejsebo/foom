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

#include <QObject>
#include <QPointer>
#include <QString>

#include "core/map_coord.h"
#include "core/map_transform.h"
#include "tools/affine_transform_tool.h"

class QAction;
class QCheckBox;
class QKeyEvent;
class QLabel;
class QPainter;
class QPushButton;
class QRectF;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;
class PointPairsWindow;


/**
 * The map-rubbersheet tool: pairs as for the affine transformation, within perimeter
 * polygons taken from the selected closed paths, the objects' bounding box, a file,
 * or drawn with Define; the window beside the map holds the options.
 *
 * See the design notes, "Rubbersheeting" and
 * "Pair tables, perimeters and the dialogs beside the map".
 */
class RubbersheetTool : public AffineTransformTool
{
Q_OBJECT
public:
	RubbersheetTool(MapEditorController* editor, QAction* tool_action);
	~RubbersheetTool() override;
	
	/// The pairs so far, the perimeter and the options of the window.
	RubbersheetParams params() const;
	
	/// Starts a new perimeter polygon, or closes the one being defined.
	void toggleDefine();
	bool isDefining() const { return defining; }
	
	void removePerimeter();
	bool loadPerimeter(const QString& path, QString* error);
	bool savePerimeter(const QString& path, QString* error) const;
	
	void transformMap() override;
	
protected:
	void updateStatusText() override;
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	PointPairsWindow* createWindow() override;
	
private:
	void perimeterChanged();
	
	std::vector<std::vector<MapCoordF>> perimeter;
	std::vector<MapCoordF> polygon;  ///< The polygon being defined.
	bool defining = false;
	QPointer<QLabel> summary_label;
	QPointer<QPushButton> define_button;
	QPointer<QCheckBox> protected_check;
	QPointer<QCheckBox> layout_check;
	QPointer<QCheckBox> decrease_check;
};


}  // namespace OpenOrienteering

#endif
