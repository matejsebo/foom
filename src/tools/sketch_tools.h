/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef FOOM_SKETCH_TOOLS_H
#define FOOM_SKETCH_TOOLS_H

#include <vector>

#include <QtGlobal>
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
 * The pen of the sketch layer: a drag adds a stroke to the sketch part.
 *
 * See the design notes, "Sketch layer", second change.
 */
class SketchPenTool : public MapEditorToolBase
{
Q_OBJECT
public:
	SketchPenTool(MapEditorController* editor, QAction* tool_action);
	~SketchPenTool() override;
	
	/** Chooses the pen which new strokes and notes use: colour 0 to 7, width 0 to 2. */
	static void setPen(int color, int width);
	static int penColor();
	static int penWidth();
	
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
	std::vector<MapCoordF> points;
};


/**
 * The eraser of the sketch layer: a click erases under the cursor, a drag
 * along its trail, when the button is released.
 */
class SketchEraserTool : public MapEditorToolBase
{
Q_OBJECT
public:
	SketchEraserTool(MapEditorController* editor, QAction* tool_action);
	~SketchEraserTool() override;
	
	/** The radius of the eraser on screen, in pixels. */
	static constexpr int radius_pixels = 10;
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	void clickRelease() override;
	void dragStart() override;
	void dragMove() override;
	void dragFinish() override;
	void dragCanceled() override;
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;
	void objectSelectionChangedImpl() override;
	
private:
	qreal radius() const;
	
	std::vector<MapCoordF> trail;
};


/**
 * The note tool of the sketch layer: a click asks for a text and adds a note.
 */
class SketchNoteTool : public MapEditorToolBase
{
Q_OBJECT
public:
	SketchNoteTool(MapEditorController* editor, QAction* tool_action);
	~SketchNoteTool() override;
	
	/** Adds a note in the pen's colour. Returns its group, or 0 for an empty text. */
	int addNote(const MapCoordF& position, const QString& text);
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	void clickRelease() override;
	void objectSelectionChangedImpl() override;
};


}  // namespace OpenOrienteering

#endif
