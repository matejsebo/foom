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

#include <vector>

#include <QObject>

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


/**
 * The map-affine tool: pairs of a map point and the same point on the reference,
 * then Enter transforms the map.
 *
 * See the design notes, "Affine by point pairs".
 */
class AffineTransformTool : public MapEditorToolBase
{
Q_OBJECT
public:
	AffineTransformTool(MapEditorController* editor, QAction* tool_action);
	~AffineTransformTool() override;
	
	/// The complete pairs so far.
	const std::vector<PointPair>& pairs() const { return point_pairs; }
	
protected:
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;
	
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	
private:
	std::vector<PointPair> point_pairs;
	MapCoordF pending;
	bool has_pending = false;
};


}  // namespace OpenOrienteering

#endif
