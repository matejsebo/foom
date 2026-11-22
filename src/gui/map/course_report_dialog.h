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


#ifndef FOOM_COURSE_REPORT_DIALOG_H
#define FOOM_COURSE_REPORT_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/course_report_print.h"

class QCheckBox;
class QTextBrowser;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * Shows the printed report of the courses, the consistency check and the
 * statistics, and prints it or saves it as a PDF or a HTML file.
 *
 * See the design notes, "The printed report".
 */
class CourseReportDialog : public QDialog
{
Q_OBJECT
public:
	/** The title is the heading of the report. */
	CourseReportDialog(Map* map, const QString& title, QWidget* parent = nullptr);
	~CourseReportDialog() override;
	
	/** The report as it is shown. */
	const QString& html() const { return report_html; }
	
	/** The options which the check boxes give. */
	CourseReportOptions options() const;
	
	bool savePdf(const QString& path) const;
	bool saveHtml(const QString& path) const;
	
private:
	void updateReport();
	void printClicked();
	void savePdfClicked();
	void saveHtmlClicked();
	
	Map* map;
	QString title;
	QString report_html;
	QCheckBox* check_box;
	QCheckBox* statistics_box;
	QTextBrowser* view;
};


}  // namespace OpenOrienteering

#endif
