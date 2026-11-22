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


#include "draw_line_text_tool.h"

#include <algorithm>
#include <cstddef>

#include <QtGlobal>
#include <QColor>
#include <QFlags>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRgb>
#include <QString>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "core/path_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/path_text_layout.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/virtual_path.h"
#include "gui/modifier_key.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/text_object_editor_helper.h"
#include "undo/object_undo.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

const TextSymbol* lineTextSymbol(const Symbol* symbol)
{
	if (symbol && symbol->getType() == Symbol::Text && symbol->asText()->followsPath())
		return symbol->asText();
	return nullptr;
}

}  // namespace


DrawLineTextTool::DrawLineTextTool(MapEditorController* editor, QAction* tool_action)
: DrawPathTool(editor, tool_action, true, false)
, text_symbol(lineTextSymbol(editor->activeSymbol()))
, text_renderables(map())
, preview_text(new TextObject(), { text_renderables })
{
	connect(this, &DrawLineAndAreaTool::dirtyRectChanged, this, &DrawLineTextTool::pathDirtyRectChanged);
	connect(this, &DrawLineAndAreaTool::pathFinished, this, &DrawLineTextTool::startText);
	if (auto* widget = editor->getMainWidget())
		widget->installEventFilter(this);
}

DrawLineTextTool::~DrawLineTextTool()
{
	// The filter goes with this object; the map widget may be gone already.
	if (text_editor)
		finishText();
}

bool DrawLineTextTool::eventFilter(QObject* watched, QEvent* event)
{
	// Upstream's shortcuts of single letters (M: Measure) would fire while the path is
	// drawn; the letter is taken by the map widget instead, where the path tool ignores it.
	if (event->type() == QEvent::ShortcutOverride && !text_editor && editingInProgress())
	{
		auto const* key = static_cast<QKeyEvent*>(event);
		auto const modifiers = key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
		if (modifiers == Qt::NoModifier && !key->text().isEmpty() && key->text().at(0).isPrint())
		{
			event->accept();
			return true;
		}
	}
	return DrawPathTool::eventFilter(watched, event);
}


void DrawLineTextTool::setDrawingSymbol(const Symbol* symbol)
{
	if (symbol == text_symbol)
		return;

	if (text_editor)
	{
		// Avoid using a deleted symbol.
		if (map()->findSymbolIndex(text_symbol) == -1)
			abortText();
		else
			finishText();
	}
	if (editingInProgress())
		abortDrawing();

	if (!symbol || symbol->isHidden())
	{
		text_symbol = nullptr;
		deactivate();
	}
	else if (auto const* line_text = lineTextSymbol(symbol))
	{
		text_symbol = line_text;
	}
	else
	{
		text_symbol = nullptr;
		switchToDefaultDrawTool(symbol);
	}
}


void DrawLineTextTool::pathDirtyRectChanged(const QRectF& rect)
{
	if (rect.isValid())
		map()->setDrawingBoundingBox(rect, 6, true);
	else
		map()->clearDrawingBoundingBox();
}


void DrawLineTextTool::startText(PathObject* path)
{
	if (!path || !text_symbol || path->parts().empty())
		return;

	// The first part, without a closing point.
	auto const& part = path->parts().front();
	if (!(part.length() > 0))
		return;
	auto const& all = path->getRawCoordinateVector();
	MapCoordVector coords(all.begin() + std::ptrdiff_t(part.first_index), all.begin() + std::ptrdiff_t(part.last_index) + 1);
	for (auto& coord : coords)
	{
		coord.setClosePoint(false);
		coord.setHolePoint(false);
	}
	if (coords.size() < 2)
		return;

	preview_text.reset(new TextObject(text_symbol));
	preview_text->setAnchorPath(coords);
	preview_text->setText(QString{});

	text_editor.reset(new TextObjectEditorHelper(preview_text.get(), editor));
	connect(text_editor.get(), &TextObjectEditorHelper::stateChanged, this, &DrawLineTextTool::updateTextPreview);
	connect(text_editor.get(), &TextObjectEditorHelper::finished, this, &DrawLineTextTool::finishText);

	setEditingInProgress(true);
	waiting_for_release = QGuiApplication::mouseButtons() != Qt::NoButton;
	updateTextPreview();
	updateTextStatus();
}

void DrawLineTextTool::finishText()
{
	if (!text_editor)
		return;

	auto const text = preview_text->getText();
	endText();
	if (!text.isEmpty())
	{
		auto* object = preview_text.release();
		object->setOutputDirty();
		auto const index = map()->addObject(object);
		map()->clearObjectSelection(false);
		map()->addObjectToSelection(object, true);
		map()->setObjectsDirty();

		auto* undo_step = new DeleteObjectsUndoStep(map());
		undo_step->addObject(index);
		map()->push(undo_step);
	}
	preview_text.reset(new TextObject());
}

void DrawLineTextTool::abortText()
{
	if (!text_editor)
		return;
	endText();
	preview_text.reset(new TextObject());
}

void DrawLineTextTool::endText()
{
	updateTextDirtyRect();
	text_editor.reset(nullptr);
	text_renderables.removeRenderablesOfObject(preview_text.get(), false);
	map()->clearDrawingBoundingBox();
	setEditingInProgress(false);
	finished_path_is_selected = false;
	updateStatusText();
}

void DrawLineTextTool::finishEditing()
{
	if (text_editor)
	{
		finishText();
		MapEditorTool::finishEditing();
	}
	else
	{
		DrawPathTool::finishEditing();
	}
}


bool DrawLineTextTool::mousePressEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (text_editor)
	{
		// A click on the map finishes the text; the click starts nothing else.
		finishText();
		waiting_for_release = true;
		return true;
	}
	if (waiting_for_release)
		return true;
	return DrawPathTool::mousePressEvent(event, map_coord, widget);
}

bool DrawLineTextTool::mouseMoveEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (text_editor || waiting_for_release)
		return true;
	return DrawPathTool::mouseMoveEvent(event, map_coord, widget);
}

bool DrawLineTextTool::mouseReleaseEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (waiting_for_release)
	{
		if (!(event->buttons() & ~event->button()))
			waiting_for_release = false;
		return true;
	}
	if (text_editor)
		return true;
	return DrawPathTool::mouseReleaseEvent(event, map_coord, widget);
}

bool DrawLineTextTool::mouseDoubleClickEvent(QMouseEvent* event, const MapCoordF& map_coord, MapWidget* widget)
{
	if (text_editor || waiting_for_release)
		return true;
	return DrawPathTool::mouseDoubleClickEvent(event, map_coord, widget);
}


bool DrawLineTextTool::keyPressEvent(QKeyEvent* event)
{
	if (!text_editor)
		return DrawPathTool::keyPressEvent(event);

	switch (event->key())
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		finishText();
		return true;
	case Qt::Key_Escape:
		if (event->modifiers() & Qt::ControlModifier)
			abortText();
		else
			finishText();
		return true;
	case Qt::Key_Tab:
		return true;  // no tabs in line text
	default:
		break;
	}
	text_editor->keyPressEvent(event);
	return true;
}

bool DrawLineTextTool::keyReleaseEvent(QKeyEvent* event)
{
	if (!text_editor)
		return DrawPathTool::keyReleaseEvent(event);
	text_editor->keyReleaseEvent(event);
	return true;
}


bool DrawLineTextTool::inputMethodEvent(QInputMethodEvent* event)
{
	if (text_editor && text_editor->inputMethodEvent(event))
		return true;
	return DrawPathTool::inputMethodEvent(event);
}

QVariant DrawLineTextTool::inputMethodQuery(Qt::InputMethodQuery property, const QVariant& argument) const
{
	if (text_editor)
		return text_editor->inputMethodQuery(property, argument);
	return DrawPathTool::inputMethodQuery(property, argument);
}


void DrawLineTextTool::updateTextPreview()
{
	auto* text = preview_text.get();
	text_renderables.removeRenderablesOfObject(text, false);
	text->update();
	text_renderables.insertRenderablesOfObject(text);
	updateTextDirtyRect();
}

void DrawLineTextTool::updateTextDirtyRect()
{
	QRectF rect;
	if (preview_text)
	{
		rectIncludeSafe(rect, preview_text->getExtent());
		for (auto const& coord : preview_text->getRawCoordinateVector())
			rectIncludeSafe(rect, QPointF(MapCoordF(coord)));
		if (text_symbol && rect.isValid())
		{
			// Room for the cursor, which stands across the path.
			auto const margin = text_symbol->getFontSize() * 1.5;
			rect.adjust(-margin, -margin, margin, margin);
		}
	}
	if (rect.isValid())
		map()->setDrawingBoundingBox(rect, 6, true);
	else
		map()->clearDrawingBoundingBox();
}

void DrawLineTextTool::updateTextStatus()
{
	setStatusBarText(tr("<b>Type</b> the text. ")
	                 + tr("<b>%1</b>, <b>%2</b>: Finish the text. ").arg(ModifierKey::return_key(), ModifierKey::escape())
	                 + tr("<b>%1+%2</b>: Cancel the text. ").arg(ModifierKey::control(), ModifierKey::escape()));
}


void DrawLineTextTool::draw(QPainter* painter, MapWidget* widget)
{
	if (!text_editor)
	{
		DrawPathTool::draw(painter, widget);
		return;
	}

	auto const& coords = preview_text->getRawCoordinateVector();
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);

	// The path, as a thin line.
	QPainterPath line;
	line.moveTo(widget->mapToViewport(coords.front()));
	for (std::size_t i = 0; i + 1 < coords.size(); )
	{
		if (coords[i].isCurveStart() && i + 3 < coords.size())
		{
			line.cubicTo(widget->mapToViewport(coords[i + 1]), widget->mapToViewport(coords[i + 2]), widget->mapToViewport(coords[i + 3]));
			i += 3;
		}
		else
		{
			line.lineTo(widget->mapToViewport(coords[i + 1]));
			i += 1;
		}
	}
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QPen(QColor(active_color), 1.0));
	painter->drawPath(line);

	// The text, as it will be drawn.
	painter->save();
	widget->applyMapTransform(painter);
	auto const view = widget->getMapView();
	RenderConfig config = { *map(), view->calculateViewedRect(widget->viewportToView(widget->rect())), view->calculateFinalZoomFactor(), RenderConfig::Tool, 1.0 };
	text_renderables.draw(painter, config);
	painter->restore();

	// The cursor, across the path.
	auto const layout = text_symbol->layoutPathText(preview_text.get());
	if (!layout.caret_offsets.empty())
	{
		auto const index = std::size_t(qBound(0, text_editor->cursorPosition(), int(layout.caret_offsets.size()) - 1));
		PathObject anchor_path(nullptr, coords);
		anchor_path.updatePathCoords();
		if (!anchor_path.parts().empty())
		{
			auto const& part = anchor_path.parts().front();
			auto const split = SplitPathCoord::at(PathCoord::length_type(layout.caret_offsets[index]), SplitPathCoord::begin(part.path_coords));
			auto tangent = split.tangentVector();
			if (tangent.length() > 0)
				tangent /= tangent.length();
			auto const up = MapCoordF(tangent.y(), -tangent.x());
			auto const bottom = split.pos + up * layout.box_bottom;
			auto const top = split.pos + up * layout.box_top;
			QPen pen(QColor(active_color), 2.0);
			painter->setPen(pen);
			painter->drawLine(QLineF(widget->mapToViewport(bottom), widget->mapToViewport(top)));
		}
	}

	painter->restore();
}


}  // namespace OpenOrienteering
