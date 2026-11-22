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
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/point_pairs_window.h"
#include "tools/tool.h"
#include "util/util.h"


namespace OpenOrienteering {

AffineTransformTool::AffineTransformTool(MapEditorController* editor, QAction* tool_action)
: MapEditorToolBase(QCursor(Qt::CrossCursor), Other, editor, tool_action)
{
	useTouchCursor(false);
}

AffineTransformTool::~AffineTransformTool()
{
	// The tool itself is deleted later, outside the window's signals.
	delete window.data();
}


void AffineTransformTool::initImpl()
{
	if (!window)
	{
		window = createWindow();
		auto* table = window->table();
		connect(table, &PointPairTable::usedChanged, this, [this](int index, bool used) { setPairUsed(std::size_t(index), used); });
		connect(window, &PointPairsWindow::removeRequested, this, [this](int index) { removePair(std::size_t(index)); });
		connect(window, &PointPairsWindow::removeAllRequested, this, &AffineTransformTool::removeAllPairs);
		connect(window, &PointPairsWindow::loadRequested, this, [this]() {
			auto const path = QFileDialog::getOpenFileName(window, tr("Load points"), {}, PointPairsWindow::fileFilter());
			QString error;
			if (!path.isEmpty() && !loadPairs(path, &error))
				QMessageBox::warning(window, window->windowTitle(), error);
		});
		connect(window, &PointPairsWindow::saveRequested, this, [this]() {
			auto const path = QFileDialog::getSaveFileName(window, tr("Save points"), {}, PointPairsWindow::fileFilter());
			QString error;
			if (!path.isEmpty() && !savePairs(path, &error))
				QMessageBox::warning(window, window->windowTitle(), error);
		});
		connect(window, &PointPairsWindow::transformRequested, this, &AffineTransformTool::transformMap);
		connect(window, &PointPairsWindow::closed, this, &AffineTransformTool::finish);
		pairsChanged();
	}
	window->show();
}

PointPairsWindow* AffineTransformTool::pairsWindow() const
{
	return window;
}

PointPairsWindow* AffineTransformTool::createWindow()
{
	return new PointPairsWindow(*map(), tr("Affine transformation"), tr("Point pairs"), true, editor->getWindow());
}


void AffineTransformTool::setPairUsed(std::size_t index, bool used)
{
	if (index >= point_pairs.size())
		return;
	point_pairs[index].used = used;
	pairsChanged();
}

void AffineTransformTool::removePair(std::size_t index)
{
	if (index >= point_pairs.size())
		return;
	point_pairs.erase(point_pairs.begin() + std::ptrdiff_t(index));
	pairsChanged();
}

void AffineTransformTool::removeAllPairs()
{
	point_pairs.clear();
	has_pending = false;
	pairsChanged();
}

bool AffineTransformTool::loadPairs(const QString& path, QString* error)
{
	QString message;
	auto loaded = readRubbersheetPoints(path, *map(), &message);
	if (!message.isEmpty())
	{
		if (error)
			*error = message;
		return false;
	}
	point_pairs = loaded;
	has_pending = false;
	pairsChanged();
	return true;
}

bool AffineTransformTool::savePairs(const QString& path, QString* error) const
{
	return writeRubbersheetPoints(path, *map(), point_pairs, error);
}

void AffineTransformTool::transformMap()
{
	if (usedPairs() == 0)
	{
		setStatusBarText(tr("No pair is used; nothing was changed."));
		return;
	}
	if (editor->affineTransformMap(point_pairs))
		finish();
	else
		setStatusBarText(tr("These pairs cannot be fitted: two have the same map point, or the map points lie on one line."));
}

void AffineTransformTool::finish()
{
	if (finishing)
		return;
	finishing = true;
	if (window)
		window->hide();
	deactivate();
}


void AffineTransformTool::pairsChanged()
{
	if (window)
	{
		auto const fit = fitPointPairs(point_pairs);
		window->table()->setPairs(point_pairs, fit.ok ? fit.residuals : std::vector<double>{});
	}
	updateDirtyRect();
	updateStatusText();
}

int AffineTransformTool::usedPairs() const
{
	return int(std::count_if(point_pairs.begin(), point_pairs.end(), [](const PointPair& pair) { return pair.used; }));
}


void AffineTransformTool::updateStatusText()
{
	QString text = has_pending ? tr("<b>Click</b>: The same point on the reference. ")
	                           : tr("<b>Click</b>: A point of the map. ");
	auto const used = usedPairs();
	if (used > 0)
	{
		text += tr("<b>Enter</b>: Transform the map by %n pair(s). ", nullptr, used);
		if (used >= 3)
		{
			auto const fit = fitPointPairs(point_pairs);
			if (fit.ok)
			{
				double largest = 0;
				for (std::size_t i = 0; i < point_pairs.size(); ++i)
				{
					if (point_pairs[i].used)
						largest = std::max(largest, fit.residuals[i]);
				}
				text += tr("Largest residual: %1 mm. ").arg(largest, 0, 'f', 2);
			}
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
	pairsChanged();
}

bool AffineTransformTool::keyPress(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (!point_pairs.empty())
			transformMap();
		return true;
	case Qt::Key_Escape:
		finish();
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
		pairsChanged();
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
	QPen const used_pen(QColor(220, 0, 0), 2 * scaleFactor());
	QPen const unused_pen(QColor(140, 140, 140), 2 * scaleFactor());
	auto const radius = 5.0 * scaleFactor();  // scaleFactor() is unsigned
	auto const mark = [&](const MapCoordF& point) {
		painter->drawEllipse(widget->mapToViewport(point), radius, radius);
	};
	for (auto const& pair : point_pairs)
	{
		painter->setPen(pair.used ? used_pen : unused_pen);
		auto const from = widget->mapToViewport(pair.map);
		auto const to = widget->mapToViewport(pair.reference);
		mark(pair.map);
		painter->drawLine(from, to);
		painter->drawLine(to + QPointF(-radius, -radius), to + QPointF(radius, radius));
		painter->drawLine(to + QPointF(-radius, radius), to + QPointF(radius, -radius));
	}
	painter->setPen(used_pen);
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
