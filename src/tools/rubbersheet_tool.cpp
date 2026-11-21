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
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
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

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/point_pairs_window.h"
#include "util/util.h"


namespace OpenOrienteering {

RubbersheetTool::RubbersheetTool(MapEditorController* editor, QAction* tool_action)
: AffineTransformTool(editor, tool_action)
{
	auto* map = editor->getMap();
	// The perimeter: the closed parts of the selected paths, or the objects' bounding box grown by 1 mm.
	for (auto const* selected : map->selectedObjects())
	{
		if (selected->getType() != Object::Path)
			continue;
		auto const& coords = selected->getRawCoordinateVector();
		for (auto const& part : selected->asPath()->parts())
		{
			if (!part.isClosed())
				continue;
			std::vector<MapCoordF> vertices;
			for (auto i = part.first_index; i < part.last_index; ++i)
				vertices.emplace_back(coords[i]);
			if (vertices.size() >= 3)
				perimeter.push_back(vertices);
		}
	}
	if (perimeter.empty())
	{
		QRectF bounds;
		map->applyOnAllObjects([&bounds](const Object* object) {
			for (auto const& c : object->getRawCoordinateVector())
				rectIncludeSafe(bounds, QPointF(MapCoordF(c)));
		});
		if (bounds.isValid() || bounds.width() > 0 || bounds.height() > 0)
		{
			bounds.adjust(-1, -1, 1, 1);
			perimeter.push_back({ MapCoordF(bounds.topLeft()), MapCoordF(bounds.topRight()), MapCoordF(bounds.bottomRight()), MapCoordF(bounds.bottomLeft()) });
		}
	}
}

RubbersheetTool::~RubbersheetTool() = default;


PointPairsWindow* RubbersheetTool::createWindow()
{
	// The dialog: perimeter, settings, points.
	auto* window = new PointPairsWindow(*map(), tr("Rubbersheeting transformation"), tr("Rubbersheeting points"), false, editor->getWindow());

	summary_label = new QLabel();
	summary_label->setObjectName(QStringLiteral("perimeter_summary"));
	define_button = new QPushButton(tr("Define"));
	define_button->setObjectName(QStringLiteral("define"));
	auto* load_button = new QPushButton(tr("Load..."));
	load_button->setObjectName(QStringLiteral("load_perimeter"));
	auto* remove_button = new QPushButton(tr("Remove"));
	remove_button->setObjectName(QStringLiteral("remove_perimeter"));
	auto* save_button = new QPushButton(tr("Save..."));
	save_button->setObjectName(QStringLiteral("save_perimeter"));
	auto* perimeter_layout = new QHBoxLayout();
	perimeter_layout->addWidget(summary_label, 1);
	for (auto* button : { define_button.data(), load_button, remove_button, save_button })
	{
		button->setAutoDefault(false);
		perimeter_layout->addWidget(button);
	}
	auto* perimeter_box = new QGroupBox(tr("Rubbersheeting perimeter"));
	perimeter_box->setLayout(perimeter_layout);
	window->addSection(perimeter_box);

	protected_check = new QCheckBox(tr("Transform objects with protected symbols"));
	layout_check = new QCheckBox(tr("Transform layout objects"));
	decrease_check = new QCheckBox(tr("Decrease rubbersheeting near the perimeter"));
	auto* settings_layout = new QVBoxLayout();
	settings_layout->addWidget(protected_check);
	settings_layout->addWidget(layout_check);
	settings_layout->addWidget(decrease_check);
	auto* settings_box = new QGroupBox(tr("Rubbersheeting settings"));
	settings_box->setLayout(settings_layout);
	window->addSection(settings_box);

	connect(define_button, &QPushButton::clicked, this, &RubbersheetTool::toggleDefine);
	connect(remove_button, &QPushButton::clicked, this, &RubbersheetTool::removePerimeter);
	connect(load_button, &QPushButton::clicked, this, [this, window]() {
		auto const path = QFileDialog::getOpenFileName(window, tr("Load perimeter"), {}, PointPairsWindow::fileFilter());
		QString error;
		if (!path.isEmpty() && !loadPerimeter(path, &error))
			QMessageBox::warning(window, window->windowTitle(), error);
	});
	connect(save_button, &QPushButton::clicked, this, [this, window]() {
		auto const path = QFileDialog::getSaveFileName(window, tr("Save perimeter"), {}, PointPairsWindow::fileFilter());
		QString error;
		if (!path.isEmpty() && !savePerimeter(path, &error))
			QMessageBox::warning(window, window->windowTitle(), error);
	});
	perimeterChanged();
	return window;
}


RubbersheetParams RubbersheetTool::params() const
{
	RubbersheetParams params;
	params.pairs = pairs();
	params.perimeter = perimeter;
	params.include_protected = protected_check && protected_check->isChecked();
	params.include_layout = layout_check && layout_check->isChecked();
	params.decrease_near_perimeter = decrease_check && decrease_check->isChecked();
	return params;
}


void RubbersheetTool::toggleDefine()
{
	if (defining)
	{
		if (polygon.size() >= 3)
			perimeter.push_back(polygon);
		polygon.clear();
		defining = false;
	}
	else
	{
		polygon.clear();
		defining = true;
	}
	perimeterChanged();
}

void RubbersheetTool::removePerimeter()
{
	perimeter.clear();
	polygon.clear();
	defining = false;
	perimeterChanged();
}

bool RubbersheetTool::loadPerimeter(const QString& path, QString* error)
{
	QString message;
	auto loaded = readRubbersheetPerimeter(path, *map(), &message);
	if (!message.isEmpty() || loaded.empty())
	{
		if (error)
			*error = message.isEmpty() ? tr("The file holds no polygon.") : message;
		return false;
	}
	perimeter = loaded;
	polygon.clear();
	defining = false;
	perimeterChanged();
	return true;
}

bool RubbersheetTool::savePerimeter(const QString& path, QString* error) const
{
	return writeRubbersheetPerimeter(path, *map(), perimeter, error);
}

void RubbersheetTool::perimeterChanged()
{
	if (summary_label)
	{
		std::size_t vertices = 0;
		for (auto const& p : perimeter)
			vertices += p.size();
		QString text = tr("No perimeter.");
		if (!perimeter.empty())
		{
			text = tr("%n polygon(s), ", nullptr, int(perimeter.size()));
			text += tr("%n vertices.", nullptr, int(vertices));
		}
		summary_label->setText(text);
	}
	if (define_button)
		define_button->setText(defining ? tr("Finish") : tr("Define"));
	updateDirtyRect();
	updateStatusText();
}


void RubbersheetTool::transformMap()
{
	if (defining)
		toggleDefine();
	WarpReport report;
	if (!editor->rubbersheetMapWith(params(), &report))
	{
		setStatusBarText(tr("Rubbersheeting needs at least one used pair and a perimeter."));
		return;
	}
	// The report, in metres on the ground (Proposed).
	auto const metres = map()->getGeoreferencing().getScaleDenominator() / 1000.0;
	editor->getWindow()->showStatusBarMessage(tr("Rubbersheeting: largest displacement %1 m, mean %2 m, %n coordinate(s) moved.", nullptr, report.count)
	                                          .arg(report.largest * metres, 0, 'f', 2).arg(report.mean * metres, 0, 'f', 2), 10000);
	finish();
}


void RubbersheetTool::updateStatusText()
{
	if (defining)
	{
		setStatusBarText(tr("<b>Click</b>: A vertex of the perimeter. <b>Enter</b>: Close the polygon. <b>Backspace</b>: Take back the last vertex. <b>Esc</b>: Drop the polygon."));
		return;
	}
	AffineTransformTool::updateStatusText();
	if (!pairs().empty())
		setStatusBarText(tr("<b>Click</b>: A point of the map, then the same point on the reference. <b>Enter</b>: Rubbersheeting by %n used pair(s). <b>Backspace</b>: Take back the last point. <b>Esc</b>: Cancel.", nullptr, usedPairs()));
}

void RubbersheetTool::clickRelease()
{
	if (!defining)
	{
		AffineTransformTool::clickRelease();
		return;
	}
	polygon.push_back(click_pos_map);
	perimeterChanged();
}

bool RubbersheetTool::keyPress(QKeyEvent* event)
{
	if (defining)
	{
		switch (event->key())
		{
		case Qt::Key_Return:
		case Qt::Key_Enter:
			toggleDefine();
			return true;
		case Qt::Key_Backspace:
			if (!polygon.empty())
				polygon.pop_back();
			perimeterChanged();
			return true;
		case Qt::Key_Escape:
			polygon.clear();
			defining = false;
			perimeterChanged();
			return true;
		}
		return false;
	}
	return AffineTransformTool::keyPress(event);
}

void RubbersheetTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	if (!perimeter.empty() || !polygon.empty())
	{
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setBrush(Qt::NoBrush);
		painter->setPen(QPen(QColor(0, 160, 220), 2.0 * scaleFactor(), Qt::DashLine));
		for (auto const& vertices : perimeter)
		{
			QPolygonF outline;
			for (auto const& c : vertices)
				outline << widget->mapToViewport(c);
			painter->drawPolygon(outline);
		}
		if (!polygon.empty())
		{
			painter->setPen(QPen(QColor(0, 160, 220), 2.0 * scaleFactor()));
			QPolygonF outline;
			for (auto const& c : polygon)
				outline << widget->mapToViewport(c);
			painter->drawPolyline(outline);
			auto const radius = 3.0 * scaleFactor();  // scaleFactor() is unsigned
			for (auto const& point : outline)
				painter->drawEllipse(point, radius, radius);
		}
		painter->restore();
	}
	AffineTransformTool::drawImpl(painter, widget);
}

int RubbersheetTool::updateDirtyRectImpl(QRectF& rect)
{
	auto border = AffineTransformTool::updateDirtyRectImpl(rect);
	for (auto const& vertices : perimeter)
	{
		for (auto const& c : vertices)
			rectIncludeSafe(rect, QPointF(c));
	}
	for (auto const& c : polygon)
		rectIncludeSafe(rect, QPointF(c));
	return perimeter.empty() && polygon.empty() ? border : std::max(border, int(8 * scaleFactor()));
}


}  // namespace OpenOrienteering
