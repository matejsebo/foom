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


#include "course_diagram_widget.h"

#include <algorithm>
#include <cstddef>

#include <Qt>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPoint>
#include <QPolygon>
#include <QSizePolicy>

namespace OpenOrienteering {

namespace {

/// The margin around the diagram, in pixels.
constexpr int margin = 6;

}  // namespace



CourseDiagramWidget::CourseDiagramWidget(QWidget* parent)
: QWidget(parent)
{
	setObjectName(QStringLiteral("course-diagram"));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	setMouseTracking(false);
}

CourseDiagramWidget::~CourseDiagramWidget() = default;


void CourseDiagramWidget::setDiagram(const CourseDiagram& diagram)
{
	current_diagram = diagram;
	updateGeometry();
	resize(sizeHint());
	update();
}


void CourseDiagramWidget::setCurrentItem(int item)
{
	if (item == current_item)
		return;
	current_item = item;
	update();
}


int CourseDiagramWidget::cellWidth() const
{
	// Wide enough for the longest code, and for a short word.
	QFontMetrics metrics(font());
	auto width = metrics.horizontalAdvance(QStringLiteral("MMMM"));
	for (auto const& cell : current_diagram.cells)
	{
		if (cell.kind == CourseDiagram::Cell::Object || cell.kind == CourseDiagram::Cell::Branch)
			width = std::max(width, metrics.horizontalAdvance(cell.text));
	}
	return width + 10;
}

int CourseDiagramWidget::cellHeight() const
{
	return QFontMetrics(font()).height() + 6;
}

int CourseDiagramWidget::rowPitch() const
{
	return cellHeight() + 10;
}

int CourseDiagramWidget::columnPitch() const
{
	return cellWidth() + 12;
}


QRect CourseDiagramWidget::cellRect(int cell) const
{
	if (cell < 0 || std::size_t(cell) >= current_diagram.cells.size())
		return {};
	auto const& entry = current_diagram.cells[std::size_t(cell)];
	return { margin + entry.column * columnPitch(), margin + entry.row * rowPitch(), cellWidth(), cellHeight() };
}


int CourseDiagramWidget::itemAt(const QPoint& pos) const
{
	for (std::size_t i = 0; i < current_diagram.cells.size(); ++i)
	{
		if (cellRect(int(i)).contains(pos))
			return current_diagram.cells[i].item;
	}
	return -1;
}


QSize CourseDiagramWidget::sizeHint() const
{
	if (current_diagram.cells.empty())
		return { 2 * margin, 2 * margin };
	return { 2 * margin + current_diagram.columns * columnPitch() - (columnPitch() - cellWidth()),
	         2 * margin + current_diagram.rows * rowPitch() - (rowPitch() - cellHeight()) };
}

QSize CourseDiagramWidget::minimumSizeHint() const
{
	return sizeHint();
}


void CourseDiagramWidget::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	auto const& palette = this->palette();
	auto const text_colour = palette.color(QPalette::WindowText);
	
	// The lines first: down from the cell, across in the gap, down into the next.
	painter.setPen(QPen(text_colour, 1.0));
	for (auto const& line : current_diagram.lines)
	{
		auto const from = cellRect(line.from);
		auto const to = cellRect(line.to);
		auto const start = QPoint(from.center().x(), from.bottom());
		auto const end = QPoint(to.center().x(), to.top());
		if (start.x() == end.x())
		{
			painter.drawLine(start, end);
			continue;
		}
		// The bend stands in the gap above the cell which is entered; the column
		// below a branch which is shorter than another is free down to there.
		auto const bend = end.y() - 5;
		painter.drawPolyline(QPolygon({ start, QPoint(start.x(), bend), QPoint(end.x(), bend), end }));
	}
	
	for (std::size_t i = 0; i < current_diagram.cells.size(); ++i)
	{
		auto const& cell = current_diagram.cells[i];
		auto const rect = cellRect(int(i));
		auto const current = cell.item == current_item;
		auto font = this->font();
		QBrush brush = palette.brush(QPalette::Base);
		QPen pen(text_colour, 1.0);
		switch (cell.kind)
		{
		case CourseDiagram::Cell::Object:
			break;
		case CourseDiagram::Cell::Marker:
			font.setItalic(true);
			break;
		case CourseDiagram::Cell::Begin:
		case CourseDiagram::Cell::End:
			brush = palette.brush(QPalette::Button);
			pen.setStyle(Qt::DashLine);
			font.setPointSizeF(font.pointSizeF() * 0.85);
			break;
		case CourseDiagram::Cell::Branch:
			font.setBold(true);
			brush = palette.brush(QPalette::AlternateBase);
			break;
		}
		if (current)
		{
			brush = palette.brush(QPalette::Highlight);
			pen.setColor(palette.color(QPalette::Highlight));
		}
		painter.setPen(pen);
		painter.setBrush(brush);
		painter.drawRoundedRect(rect, 3, 3);
		painter.setFont(font);
		painter.setPen(current ? palette.color(QPalette::HighlightedText) : text_colour);
		auto const text = QFontMetrics(font).elidedText(cell.text, Qt::ElideRight, rect.width() - 4);
		painter.drawText(rect, Qt::AlignCenter, text);
	}
}


void CourseDiagramWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}
	auto const item = itemAt(event->pos());
	if (item < 0)
		return;
	setCurrentItem(item);
	emit itemClicked(item);
}


}  // namespace OpenOrienteering
