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


#ifndef FOOM_COURSE_REPORT_PRINT_H
#define FOOM_COURSE_REPORT_PRINT_H

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>

class QPagedPaintDevice;

namespace OpenOrienteering {

class Map;
struct CourseData;


/**
 * The printed report of the courses: the consistency check under its
 * headings, and the statistics of the courses and of the
 * event, as a page which can be printed and saved.
 *
 * See the design notes, "Statistics and the consistency
 * check", and "The printed report".
 */
struct CourseReportOptions
{
	/** The consistency check. */
	bool check = true;
	/** The statistics. */
	bool statistics = true;
	/** The heading of the report; when empty, "Courses". */
	QString title;
	/** Said under the heading when it is valid. */
	QDate date;
};

/** The headings of the parts of the consistency check, in their order. */
QStringList courseCheckSections();

/**
 * The part of the consistency check in which the issues of a rule stand,
 * as the index into courseCheckSections(); -1 for a rule which is not one
 * of the check of the courses.
 */
int courseCheckSection(const QByteArray& rule);

/** The report as a HTML document. */
QString courseReportHtml(const Map& map, const CourseData& data, const CourseReportOptions& options);

/** Lays the HTML out on the pages of the device and paints it there. */
void paintCourseReport(const QString& html, QPagedPaintDevice* device);

/** Writes the HTML as a PDF file of A4 pages. Returns false when the file cannot be written. */
bool writeCourseReportPdf(const QString& html, const QString& path);


}  // namespace OpenOrienteering

#endif
