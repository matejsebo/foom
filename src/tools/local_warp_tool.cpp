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

#include "local_warp_tool.h"

#include <cmath>

#include <Qt>
#include <QCheckBox>
#include <QColor>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "util/util.h"


namespace OpenOrienteering {

LocalWarpTool::LocalWarpTool(MapEditorController* editor, QAction* tool_action, const LocalWarpParams& settings)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
, settings(settings)
{
	useTouchCursor(false);
}

LocalWarpTool::~LocalWarpTool() = default;


void LocalWarpTool::updateStatusText()
{
	if (dragging)
		setStatusBarText(tr("<b>Drag</b>: To the target, released within %1 mm of the centre.").arg(settings.radius, 0, 'f', 0));
	else
		setStatusBarText(tr("<b>Drag</b>: From the centre of the transformation to its target (radius %1 mm). <b>Esc</b>: End.").arg(settings.radius, 0, 'f', 0));
}

void LocalWarpTool::objectSelectionChangedImpl()
{
	// NOP
}

void LocalWarpTool::dragStart()
{
	center = click_pos_map;
	current = cur_pos_map;
	dragging = true;
	updateDirtyRect();
	updateStatusText();
}

void LocalWarpTool::dragMove()
{
	current = cur_pos_map;
	updateDirtyRect();
}

void LocalWarpTool::dragFinish()
{
	dragging = false;
	auto params = settings;
	params.center = center;
	params.target = cur_pos_map;
	updateDirtyRect();
	updateStatusText();
	if (!editor->localWarpMapWith(params))
		setStatusBarText(tr("The mouse must be released within the radius; nothing was changed."));
}

void LocalWarpTool::dragCanceled()
{
	dragging = false;
	updateDirtyRect();
	updateStatusText();
}

bool LocalWarpTool::keyPress(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape)
	{
		deactivate();
		return true;
	}
	return false;
}


void LocalWarpTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (!dragging)
		return;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QPen(QColor(220, 0, 0), 2.0 * scaleFactor()));  // scaleFactor() is unsigned
	auto const c = widget->mapToViewport(center);
	auto const edge = widget->mapToViewport(MapCoordF(center.x() + settings.radius, center.y()));
	auto const radius = std::hypot(edge.x() - c.x(), edge.y() - c.y());
	painter->drawEllipse(c, radius, radius);
	painter->drawLine(c, widget->mapToViewport(current));
	painter->restore();
}

int LocalWarpTool::updateDirtyRectImpl(QRectF& rect)
{
	if (!dragging)
		return -1;
	rectIncludeSafe(rect, QPointF(center.x() - settings.radius, center.y() - settings.radius));
	rectIncludeSafe(rect, QPointF(center.x() + settings.radius, center.y() + settings.radius));
	rectIncludeSafe(rect, QPointF(current));
	return int(4 * scaleFactor());
}



LocalWarpDialog::LocalWarpDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Local transformation"));
	LocalWarpParams const defaults;
	radius_edit = new QSpinBox();
	radius_edit->setRange(1, 1000);
	radius_edit->setSingleStep(5);
	radius_edit->setSuffix(QStringLiteral(" mm"));
	radius_edit->setValue(int(defaults.radius));
	protected_check = new QCheckBox(tr("Transform objects with protected symbols"));
	layout_check = new QCheckBox(tr("Transform layout objects"));
	auto* form = new QFormLayout();
	form->addRow(tr("Radius of transformation:"), radius_edit);
	form->addRow(protected_check);
	form->addRow(layout_check);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

LocalWarpDialog::~LocalWarpDialog() = default;

LocalWarpParams LocalWarpDialog::settings() const
{
	LocalWarpParams settings;
	settings.radius = radius_edit->value();
	settings.include_protected = protected_check->isChecked();
	settings.include_layout = layout_check->isChecked();
	return settings;
}


}  // namespace OpenOrienteering
