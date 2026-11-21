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

#include "affine_transform_tool.h"

#include <algorithm>

#include <Qt>
#include <QColor>
#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

AffineTransformTool::AffineTransformTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
{
	useTouchCursor(false);
}

AffineTransformTool::~AffineTransformTool() = default;


void AffineTransformTool::updateStatusText()
{
	QString text = has_pending ? tr("<b>Click</b>: The same point on the reference. ")
	                           : tr("<b>Click</b>: A point of the map. ");
	if (!point_pairs.empty())
	{
		text += tr("<b>Enter</b>: Transform the map by %n pair(s). ", nullptr, int(point_pairs.size()));
		if (point_pairs.size() >= 3)
		{
			auto const fit = fitPointPairs(point_pairs);
			if (fit.ok)
				text += tr("Largest residual: %1 mm. ").arg(*std::max_element(fit.residuals.begin(), fit.residuals.end()), 0, 'f', 2);
		}
	}
	if (has_pending || !point_pairs.empty())
		text += tr("<b>Backspace</b>: Take back the last point. ");
	text += tr("<b>Esc</b>: Cancel.");
	setStatusBarText(text);
}

void AffineTransformTool::objectSelectionChangedImpl()
{
	// NOP
}

void AffineTransformTool::clickRelease()
{
	if (has_pending)
	{
		point_pairs.push_back({ pending, click_pos_map });
		has_pending = false;
	}
	else
	{
		pending = click_pos_map;
		has_pending = true;
	}
	updateDirtyRect();
	updateStatusText();
}

bool AffineTransformTool::keyPress(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (point_pairs.empty())
			return true;
		if (editor->affineTransformMap(point_pairs))
			deactivate();
		else
			setStatusBarText(tr("These pairs cannot be fitted: two have the same map point, or the map points lie on one line."));
		return true;
	case Qt::Key_Escape:
		deactivate();
		return true;
	case Qt::Key_Backspace:
		if (has_pending)
		{
			has_pending = false;
		}
		else if (!point_pairs.empty())
		{
			pending = point_pairs.back().map;
			has_pending = true;
			point_pairs.pop_back();
		}
		updateDirtyRect();
		updateStatusText();
		return true;
	}
	return false;
}


void AffineTransformTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (point_pairs.empty() && !has_pending)
		return;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QPen(QColor(220, 0, 0), 2 * scaleFactor()));
	auto const radius = 5.0 * scaleFactor();  // scaleFactor() is unsigned
	auto const mark = [&](const MapCoordF& point) {
		painter->drawEllipse(widget->mapToViewport(point), radius, radius);
	};
	for (auto const& pair : point_pairs)
	{
		auto const from = widget->mapToViewport(pair.map);
		auto const to = widget->mapToViewport(pair.reference);
		mark(pair.map);
		painter->drawLine(from, to);
		painter->drawLine(to + QPointF(-radius, -radius), to + QPointF(radius, radius));
		painter->drawLine(to + QPointF(-radius, radius), to + QPointF(radius, -radius));
	}
	if (has_pending)
		mark(pending);
	painter->restore();
}

int AffineTransformTool::updateDirtyRectImpl(QRectF& rect)
{
	if (point_pairs.empty() && !has_pending)
		return -1;
	for (auto const& pair : point_pairs)
	{
		rectIncludeSafe(rect, pair.map);
		rectIncludeSafe(rect, pair.reference);
	}
	if (has_pending)
		rectIncludeSafe(rect, pending);
	return int(8 * scaleFactor());
}


}  // namespace OpenOrienteering
