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


#ifndef FOOM_COURSE_MAPS_DIALOG_H
#define FOOM_COURSE_MAPS_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/course_map.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * Asks which course maps are written as PDF files, and where.
 *
 * See the design notes, "The maps of the courses as PDF files".
 */
class CourseMapsDialog : public QDialog
{
Q_OBJECT
public:
	CourseMapsDialog(const Map* map, const QString& folder, QWidget* parent = nullptr);
	~CourseMapsDialog() override;
	
	/** What was chosen; the name of the files is left to the caller. */
	CourseMapExportOptions options() const;
	
private:
	void browse();
	void updateButtons();
	
	QListWidget* course_list;
	QComboBox* relay_box;
	QComboBox* extent_box;
	QCheckBox* canvases_check;
	QCheckBox* routes_check;
	QLineEdit* folder_edit;
	QWidget* ok_button;
};


}  // namespace OpenOrienteering

#endif
