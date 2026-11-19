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


#ifndef FOOM_COURSE_STATISTICS_DIALOG_H
#define FOOM_COURSE_STATISTICS_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/course_report.h"

class QCheckBox;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * Shows the statistics of the courses and of the event: the courses, the
 * course objects, the legs, the controls against the courses, and the
 * lists of the event.
 *
 * See the design notes, "Statistics and the consistency check".
 */
class CourseStatisticsDialog : public QDialog
{
Q_OBJECT
public:
	explicit CourseStatisticsDialog(Map* map, QWidget* parent = nullptr);
	~CourseStatisticsDialog() override;
	
	/** The statistics as the text which is exported. */
	QString text() const;
	
	/** Writes the text into a file, in UTF-8. */
	bool exportTo(const QString& path) const;
	
private:
	void fillLegs();
	void exportClicked();
	
	CourseStatistics statistics;
	QTableWidget* leg_table;
	QCheckBox* hide_single_check;
};


}  // namespace OpenOrienteering

#endif
