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


#ifndef OPENORIENTEERING_DRAW_LINE_TEXT_TOOL_H
#define OPENORIENTEERING_DRAW_LINE_TEXT_TOOL_H

#include <memory>

#include <Qt>
#include <QObject>
#include <QRectF>
#include <QVariant>

#include "core/renderables/renderable.h"
#include "tools/draw_path_tool.h"

class QAction;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace OpenOrienteering {

class MapCoordF;
class MapEditorController;
class MapWidget;
class PathObject;
class Symbol;
class TextObject;
class TextObjectEditorHelper;
class TextSymbol;


/**
 * Draws line text: first the anchor path, as with the path tool, then the
 * text, typed along that path.
 *
 * See the design notes, "Drawing line text".
 *
 * This class is marked as final because its destructor calls virtual functions.
 */
class DrawLineTextTool final : public DrawPathTool
{
Q_OBJECT
public:
	DrawLineTextTool(MapEditorController* editor, QAction* tool_action);
	~DrawLineTextTool() override;

	/** Returns true while the text is being typed. */
	bool isTyping() const { return bool(text_editor); }

	void finishEditing() override;

	bool mousePressEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseMoveEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseReleaseEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;
	bool mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget) override;

	bool keyPressEvent(QKeyEvent* event) override;
	bool keyReleaseEvent(QKeyEvent* event) override;

	bool inputMethodEvent(QInputMethodEvent* event) override;
	QVariant inputMethodQuery(Qt::InputMethodQuery property, const QVariant& argument) const override;

	void draw(QPainter* painter, MapWidget* widget) override;

protected slots:
	void setDrawingSymbol(const OpenOrienteering::Symbol* symbol) override;

private:
	void pathDirtyRectChanged(const QRectF& rect);
	void startText(OpenOrienteering::PathObject* path);
	void finishText();
	void abortText();
	void endText();
	void updateTextPreview();
	void updateTextDirtyRect();
	void updateTextStatus();

	const TextSymbol* text_symbol = nullptr;
	MapRenderables text_renderables;
	std::unique_ptr<TextObject, MapRenderables::ObjectDeleter> preview_text;
	std::unique_ptr<TextObjectEditorHelper> text_editor;
	bool waiting_for_release = false;
};


}  // namespace OpenOrienteering

#endif
