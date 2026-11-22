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


#include "generator_place_tool.h"

#include <Qt>
#include <QChar>
#include <QCursor>
#include <QKeyEvent>
#include <QPointF>

#include "core/map_coord.h"
#include "gui/map/map_editor.h"
#include "layout/generator.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

GeneratorPlaceTool::GeneratorPlaceTool(MapEditorController* editor, const QString& id, const QJsonObject& params, const QRectF& preview)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, nullptr)
, id(id)
, params(params)
, preview(preview)
{
	useTouchCursor(false);
}

GeneratorPlaceTool::~GeneratorPlaceTool() = default;


void GeneratorPlaceTool::updateStatusText()
{
	// "Scale bar" becomes "scale bar", "QR code" stays.
	auto what = generatorTitle(id);
	if (what.size() > 1 && what.at(1).isLower())
		what[0] = what.at(0).toLower();
	setStatusBarText(tr("<b>Click</b>: Place the %1. <b>%2</b>: Cancel. ").arg(what, tr("Esc")));
}

void GeneratorPlaceTool::objectSelectionChangedImpl()
{
	// NOP
}

void GeneratorPlaceTool::mouseMove()
{
	have_position = true;
	updateDirtyRect();
}

void GeneratorPlaceTool::clickRelease()
{
	// The tool ends whether the generator succeeds or not; a failure has its message.
	editor->placeGeneratorAt(id, params, click_pos_map);
	deactivate();
}

bool GeneratorPlaceTool::keyPress(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape)
	{
		deactivate();
		return true;
	}
	return false;
}

void GeneratorPlaceTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	MapEditorToolBase::drawImpl(painter, widget);
	if (have_position && preview.isValid())
	{
		auto const at = QPointF(cur_pos_map);
		drawSelectionBox(painter, widget, MapCoordF(at + preview.topLeft()), MapCoordF(at + preview.bottomRight()));
	}
}

int GeneratorPlaceTool::updateDirtyRectImpl(QRectF& rect)
{
	if (!have_position || !preview.isValid())
		return -1;
	auto const at = QPointF(cur_pos_map);
	rectIncludeSafe(rect, at + preview.topLeft());
	rectInclude(rect, at + preview.bottomRight());
	return 5;
}


}  // namespace OpenOrienteering
