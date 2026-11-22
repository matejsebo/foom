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


#ifndef FOOM_COURSE_DIAGRAM_WIDGET_H
#define FOOM_COURSE_DIAGRAM_WIDGET_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "course/course_diagram.h"

class QMouseEvent;
class QPaintEvent;
class QPoint;

namespace OpenOrienteering {


/**
 * Draws the branch diagram of a course, with the item which is current
 * marked; a click on a cell makes its item current.
 *
 * See the design notes, "Relays: the branch diagram".
 */
class CourseDiagramWidget : public QWidget
{
Q_OBJECT
public:
	explicit CourseDiagramWidget(QWidget* parent = nullptr);
	~CourseDiagramWidget() override;
	
	void setDiagram(const CourseDiagram& diagram);
	const CourseDiagram& diagram() const { return current_diagram; }
	
	/** The index of the item which is marked; -1 for none. */
	int currentItem() const { return current_item; }
	void setCurrentItem(int item);
	
	/** The rectangle of a cell, in the coordinates of the widget. */
	QRect cellRect(int cell) const;
	/** The item of the cell at a point; -1 when there is none. */
	int itemAt(const QPoint& pos) const;
	
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	
signals:
	/** A click made the item current. */
	void itemClicked(int item);
	
protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	
private:
	int cellWidth() const;
	int cellHeight() const;
	int rowPitch() const;
	int columnPitch() const;
	
	CourseDiagram current_diagram;
	int current_item = -1;
};


}  // namespace OpenOrienteering

#endif
