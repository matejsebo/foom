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


#ifndef FOOM_COURSE_CANVASES_DIALOG_H
#define FOOM_COURSE_CANVASES_DIALOG_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/course_canvas.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The canvases of a course project: which there are, and for each its
 * extent, its scale, its description box, its courses and the course
 * symbols which it hides.
 *
 * See the design notes, "Canvases: the dialog".
 */
class CourseCanvasesDialog : public QDialog
{
Q_OBJECT
public:
	explicit CourseCanvasesDialog(const Map* map, QWidget* parent = nullptr);
	~CourseCanvasesDialog() override;
	
	/** The canvases as they are in the dialog. */
	const CourseCanvases& canvases() const { return data; }
	
	/** Adds a canvas and makes it the current one. False for a name which is empty or taken. */
	bool addCanvas(const QString& name);
	bool renameCanvas(int index, const QString& name);
	void removeCanvas(int index);
	void setCurrentCanvas(int index);
	int currentCanvas() const;
	
	/** The text of an entry of the list of courses: "Map Exchange - Map exchange #1". */
	static QString entryText(const CanvasCourse& entry);
	
private:
	void refreshList(int current);
	void refreshCanvas();
	void valuesChanged();
	void courseChanged(QListWidgetItem* item);
	void symbolChanged(QListWidgetItem* item);
	void newClicked();
	void renameClicked();
	
	const Map* map;
	CourseCanvases data;
	/** What can be given to a canvas, in the order of the list. */
	std::vector<CanvasCourse> entries;
	bool refreshing = false;
	
	QCheckBox* active_check;
	QListWidget* canvas_list;
	QPushButton* rename_button;
	QPushButton* delete_button;
	QWidget* canvas_panel;
	QSpinBox* scale_edit;
	QComboBox* extent_box;
	QDoubleSpinBox* box_edit;
	QSpinBox* rows_edit;
	QListWidget* course_list;
	QListWidget* symbol_list;
};


}  // namespace OpenOrienteering

#endif
