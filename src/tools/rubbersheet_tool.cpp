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

#include "rubbersheet_tool.h"

#include <algorithm>

#include <Qt>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QRectF>
#include <QString>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/objects/object.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "util/util.h"


namespace OpenOrienteering {

RubbersheetTool::RubbersheetTool(MapEditorController* editor, QAction* tool_action)
: AffineTransformTool(editor, tool_action)
{
	auto* map = editor->getMap();
	// The perimeter: the selected closed path, or the objects' bounding box grown by 1 mm.
	auto const* selected = map->getNumSelectedObjects() == 1 ? map->getFirstSelectedObject() : nullptr;
	if (selected && selected->getType() == Object::Path && !selected->asPath()->parts().empty()
	    && selected->asPath()->parts().front().isClosed())
	{
		auto const& part = selected->asPath()->parts().front();
		auto const& coords = selected->getRawCoordinateVector();
		for (auto i = part.first_index; i < part.last_index; ++i)
			perimeter.emplace_back(coords[i]);
	}
	else
	{
		QRectF bounds;
		map->applyOnAllObjects([&bounds](const Object* object) {
			for (auto const& c : object->getRawCoordinateVector())
				rectIncludeSafe(bounds, QPointF(MapCoordF(c)));
		});
		if (bounds.isValid() || bounds.width() > 0 || bounds.height() > 0)
		{
			bounds.adjust(-1, -1, 1, 1);
			perimeter = { MapCoordF(bounds.topLeft()), MapCoordF(bounds.topRight()), MapCoordF(bounds.bottomRight()), MapCoordF(bounds.bottomLeft()) };
		}
	}
}

RubbersheetTool::~RubbersheetTool() = default;


RubbersheetParams RubbersheetTool::params() const
{
	RubbersheetParams params;
	params.pairs = pairs();
	params.perimeter = perimeter;
	return params;
}


void RubbersheetTool::updateStatusText()
{
	AffineTransformTool::updateStatusText();
	if (!pairs().empty())
		setStatusBarText(tr("<b>Click</b>: A point of the map, then the same point on the reference. <b>Enter</b>: Rubbersheeting by %n pair(s)... <b>Backspace</b>: Take back the last point. <b>Esc</b>: Cancel.", nullptr, int(pairs().size())));
}

bool RubbersheetTool::keyPress(QKeyEvent* event)
{
	if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
		return AffineTransformTool::keyPress(event);
	if (pairs().empty())
		return true;
	RubbersheetDialog dialog(*editor->getMap(), params(), editor->getWindow());
	if (dialog.exec() != QDialog::Accepted)
		return true;
	if (editor->rubbersheetMapWith(dialog.params()))
		deactivate();
	else
		setStatusBarText(tr("Rubbersheeting needs at least one pair and a perimeter."));
	return true;
}

void RubbersheetTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (perimeter.size() >= 3)
	{
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setBrush(Qt::NoBrush);
		painter->setPen(QPen(QColor(0, 160, 220), 2.0 * scaleFactor(), Qt::DashLine));
		QPolygonF outline;
		for (auto const& c : perimeter)
			outline << widget->mapToViewport(c);
		painter->drawPolygon(outline);
		painter->restore();
	}
	AffineTransformTool::drawImpl(painter, widget);
}

int RubbersheetTool::updateDirtyRectImpl(QRectF& rect)
{
	auto border = AffineTransformTool::updateDirtyRectImpl(rect);
	for (auto const& c : perimeter)
		rectIncludeSafe(rect, QPointF(c));
	return perimeter.empty() ? border : std::max(border, int(8 * scaleFactor()));
}



RubbersheetDialog::RubbersheetDialog(const Map& map, const RubbersheetParams& params, QWidget* parent)
: QDialog(parent)
, map(map)
, current(params)
{
	setWindowTitle(tr("Rubbersheeting"));
	summary_label = new QLabel();
	protected_check = new QCheckBox(tr("Transform objects with protected symbols"));
	protected_check->setChecked(params.include_protected);
	layout_check = new QCheckBox(tr("Transform layout objects"));
	layout_check->setChecked(params.include_layout);
	decrease_check = new QCheckBox(tr("Decrease rubbersheeting near the perimeter"));
	decrease_check->setChecked(params.decrease_near_perimeter);
	
	auto* load_points = new QPushButton(tr("Load points..."));
	auto* save_points = new QPushButton(tr("Save points..."));
	auto* load_perimeter = new QPushButton(tr("Load perimeter..."));
	auto* save_perimeter = new QPushButton(tr("Save perimeter..."));
	QString const filter = tr("Text files") + QLatin1String(" (*.txt);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const fail = [this](const QString& error) {
		QMessageBox::warning(this, tr("Rubbersheeting"), error);
	};
	connect(load_points, &QPushButton::clicked, this, [this, filter, fail]() {
		auto const path = QFileDialog::getOpenFileName(this, tr("Load points"), {}, filter);
		if (path.isEmpty())
			return;
		QString error;
		auto pairs = readRubbersheetPoints(path, this->map, &error);
		if (!error.isEmpty())
			fail(error);
		else
			current.pairs = pairs;
		updateSummary();
	});
	connect(save_points, &QPushButton::clicked, this, [this, filter, fail]() {
		auto const path = QFileDialog::getSaveFileName(this, tr("Save points"), {}, filter);
		QString error;
		if (!path.isEmpty() && !writeRubbersheetPoints(path, this->map, current.pairs, &error))
			fail(error);
	});
	connect(load_perimeter, &QPushButton::clicked, this, [this, filter, fail]() {
		auto const path = QFileDialog::getOpenFileName(this, tr("Load perimeter"), {}, filter);
		if (path.isEmpty())
			return;
		QString error;
		auto perimeter = readRubbersheetPerimeter(path, this->map, &error);
		if (!error.isEmpty())
			fail(error);
		else if (perimeter.size() >= 3)
			current.perimeter = perimeter;
		updateSummary();
	});
	connect(save_perimeter, &QPushButton::clicked, this, [this, filter, fail]() {
		auto const path = QFileDialog::getSaveFileName(this, tr("Save perimeter"), {}, filter);
		QString error;
		if (!path.isEmpty() && !writeRubbersheetPerimeter(path, this->map, current.perimeter, &error))
			fail(error);
	});
	
	auto* files = new QGridLayout();
	files->addWidget(load_points, 0, 0);
	files->addWidget(save_points, 0, 1);
	files->addWidget(load_perimeter, 1, 0);
	files->addWidget(save_perimeter, 1, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(summary_label);
	layout->addWidget(protected_check);
	layout->addWidget(layout_check);
	layout->addWidget(decrease_check);
	layout->addLayout(files);
	layout->addWidget(buttons);
	updateSummary();
}

RubbersheetDialog::~RubbersheetDialog() = default;

RubbersheetParams RubbersheetDialog::params() const
{
	auto params = current;
	params.include_protected = protected_check->isChecked();
	params.include_layout = layout_check->isChecked();
	params.decrease_near_perimeter = decrease_check->isChecked();
	return params;
}

void RubbersheetDialog::updateSummary()
{
	summary_label->setText(tr("%n pair(s); ", nullptr, int(current.pairs.size()))
	                       + tr("a perimeter of %n vertices.", nullptr, int(current.perimeter.size())));
}


}  // namespace OpenOrienteering
