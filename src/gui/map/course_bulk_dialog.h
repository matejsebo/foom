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


#ifndef FOOM_COURSE_BULK_DIALOG_H
#define FOOM_COURSE_BULK_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/course_bulk.h"

class QCheckBox;
class QComboBox;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * Puts a course object into many courses, or takes it out of them.
 *
 * See the design notes, "Composing a course and the bulk
 * commands".
 */
class CourseBulkDialog : public QDialog
{
Q_OBJECT
public:
	enum Mode { Insert, Remove };
	
	/** The code is that of the course object which is chosen at first. */
	CourseBulkDialog(Map* map, Mode mode, const QString& code, QWidget* parent = nullptr);
	~CourseBulkDialog() override;
	
	/**
	 * Does what the dialog says, as one undo step, and says what was done.
	 * Returns false, with the reason, when nothing was changed.
	 */
	bool apply(QString* message = nullptr);
	
	/** What the apply() which was done last said. */
	const QString& report() const { return last_report; }
	
	/** Applies and closes; when nothing was changed, says why and stays. */
	void accept() override;
	
private:
	CourseCondition condition() const;
	void updateWidgets();
	void checkAll(bool checked);
	
	Map* map;
	Mode mode;
	QComboBox* object_box;
	QListWidget* course_list;
	QRadioButton* all_radio = nullptr;
	QRadioButton* after_radio;
	QRadioButton* before_radio;
	QRadioButton* between_radio;
	QComboBox* first_box;
	QComboBox* second_box;
	QString last_report;
};


/**
 * Gives the controls new codes.
 */
class CourseRenumberDialog : public QDialog
{
Q_OBJECT
public:
	explicit CourseRenumberDialog(Map* map, QWidget* parent = nullptr);
	~CourseRenumberDialog() override;
	
	/** As CourseBulkDialog::apply(). */
	bool apply(QString* message = nullptr);
	const QString& report() const { return last_report; }
	void accept() override;
	
private:
	Map* map;
	QSpinBox* old_edit;
	QSpinBox* new_edit;
	QCheckBox* gaps_check;
	QString last_report;
};


}  // namespace OpenOrienteering

#endif
