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

#ifndef FOOM_AFFINE_TRANSFORM_TOOL_H
#define FOOM_AFFINE_TRANSFORM_TOOL_H

#include <cstddef>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>

#include "core/map_coord.h"
#include "core/map_transform.h"
#include "tools/tool_base.h"

class QAction;
class QKeyEvent;
class QPainter;
class QRectF;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;
class PointPairsWindow;


/**
 * The map-affine tool: pairs of a map point and the same point on the reference,
 * listed in a window beside the map, then Enter or Transform transforms the map.
 *
 * See the design notes, "Affine by point pairs" and
 * "Pair tables, perimeters and the dialogs beside the map".
 */
class AffineTransformTool : public MapEditorToolBase
{
Q_OBJECT
public:
	AffineTransformTool(MapEditorController* editor, QAction* tool_action);
	~AffineTransformTool() override;
	
	/// The complete pairs so far, used or not.
	const std::vector<PointPair>& pairs() const { return point_pairs; }
	
	/// The window beside the map, while the tool is active.
	PointPairsWindow* pairsWindow() const;
	
	void setPairUsed(std::size_t index, bool used);
	void removePair(std::size_t index);
	void removeAllPairs();
	
	/// Replaces the pairs by those of a points file.
	bool loadPairs(const QString& path, QString* error);
	/// Writes the pairs as a points file.
	bool savePairs(const QString& path, QString* error) const;
	
	/// What Enter and Transform do.
	virtual void transformMap();
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	
	/// Creates the window; the rubbersheet tool adds its sections.
	virtual PointPairsWindow* createWindow();
	
	/// Updates the table, the drawing and the status bar.
	void pairsChanged();
	
	/// The number of used pairs.
	int usedPairs() const;
	
	/// Ends the tool, once.
	void finish();
	
private:
	std::vector<PointPair> point_pairs;
	MapCoordF pending;
	bool has_pending = false;
	bool finishing = false;
	QPointer<PointPairsWindow> window;
};


}  // namespace OpenOrienteering

#endif
