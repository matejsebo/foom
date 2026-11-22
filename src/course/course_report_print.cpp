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


#include "course_report_print.h"

#include <cstddef>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPagedPaintDevice>
#include <QPdfWriter>
#include <QTextDocument>

#include "check/issue.h"
#include "course/course_data.h"
#include "course/course_render.h"
#include "course/course_report.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseReportPrint", text);
}

QString metres(double value)
{
	return QString::number(qRound(value)) + QLatin1String(" m");
}

QString escaped(const QString& text)
{
	return text.toHtmlEscaped();
}

/// The rules of the parts of the check, in the order of courseCheckSections().
const std::vector<std::vector<const char*>>& sectionRules()
{
	static const std::vector<std::vector<const char*>> rules = {
	    { rule::course_start_missing, rule::course_finish_missing, rule::course_climb_zero },
	    { rule::control_unused },
	    { rule::description_d_empty, rule::description_de_missing, rule::description_fg_missing },
	    { rule::punch_consecutive, rule::punch_repeated },
	    { rule::course_identical },
	    { rule::leg_opposite },
	    { rule::control_too_close },
	    { rule::relay_structure, rule::course_part, rule::course_not_checked },
	};
	return rules;
}

QString severityText(Issue::Severity severity)
{
	switch (severity)
	{
	case Issue::Error:
		return tr("Error");
	case Issue::Warning:
		return tr("Warning");
	case Issue::Info:
		break;
	}
	return tr("Note");
}

/// A table with a row of headings and rows of cells; the cells are escaped here.
QString table(const QStringList& headings, const std::vector<QStringList>& rows)
{
	QString html = QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">\n<tr>");
	for (auto const& heading : headings)
		html += QLatin1String("<th>") + escaped(heading) + QLatin1String("</th>");
	html += QLatin1String("</tr>\n");
	for (auto const& row : rows)
	{
		html += QLatin1String("<tr>");
		for (auto const& cell : row)
			html += QLatin1String("<td>") + escaped(cell) + QLatin1String("</td>");
		html += QLatin1String("</tr>\n");
	}
	return html + QLatin1String("</table>\n");
}

QString none()
{
	return QLatin1String("<p>") + escaped(tr("None.")) + QLatin1String("</p>\n");
}

QString heading(int level, const QString& text)
{
	auto const tag = QString::number(level);
	return QLatin1String("<h") + tag + QLatin1Char('>') + escaped(text) + QLatin1String("</h") + tag + QLatin1String(">\n");
}

QString checkHtml(const std::vector<Issue>& issues)
{
	int counts[3] = {};
	for (auto const& issue : issues)
		++counts[issue.severity];
	
	QString html = heading(2, tr("Consistency check"));
	html += QLatin1String("<p>") + escaped(tr("Errors: %1, warnings: %2, notes: %3.")
	                                       .arg(counts[Issue::Error]).arg(counts[Issue::Warning]).arg(counts[Issue::Info]))
	        + QLatin1String("</p>\n");
	
	auto const titles = courseCheckSections();
	for (int s = 0; s < titles.size(); ++s)
	{
		html += heading(3, titles[s]);
		std::vector<QStringList> rows;
		for (auto const& issue : issues)
		{
			if (courseCheckSection(issue.rule) == s)
				rows.push_back({ severityText(issue.severity), issue.message });
		}
		html += rows.empty() ? none() : table({ tr("Kind"), tr("What") }, rows);
	}
	return html;
}

QString statisticsHtml(const CourseStatistics& statistics)
{
	QString html = heading(2, tr("Statistics"));
	
	html += heading(3, tr("Event"));
	html += table({ tr("Controls"), tr("Courses"), tr("Classes") },
	              { { QString::number(statistics.control_count), QString::number(statistics.course_count), QString::number(statistics.class_count) } });
	
	html += heading(3, tr("Courses"));
	std::vector<QStringList> rows;
	for (auto const& course : statistics.courses)
	{
		rows.push_back({ course.name, course.classes.join(QLatin1String(", ")), QString::number(course.runners),
		                 course.measured ? formatCourseLength(course.length_m) : QStringLiteral("-"),
		                 metres(course.climb_m), QString::number(course.controls) });
	}
	html += rows.empty() ? none() : table({ tr("Course"), tr("Classes"), tr("Runners"), tr("Length"), tr("Climb"), tr("Controls") }, rows);
	
	html += heading(3, tr("Course objects"));
	rows.clear();
	for (auto const& object : statistics.objects)
		rows.push_back({ object.code, QString::number(object.courses.size()), QString::number(object.runners), object.courses.join(QLatin1String(", ")) });
	html += rows.empty() ? none() : table({ tr("Code"), tr("Courses"), tr("Runners"), tr("In the courses") }, rows);
	
	html += heading(3, tr("Legs"));
	rows.clear();
	for (auto const& leg : statistics.legs)
	{
		rows.push_back({ leg.from, leg.to, leg.length_m < 0.0 ? QStringLiteral("-") : metres(leg.length_m),
		                 QString::number(leg.courses.size()), QString::number(leg.runners), leg.courses.join(QLatin1String(", ")) });
	}
	html += rows.empty() ? none() : table({ tr("From"), tr("To"), tr("Length"), tr("Courses"), tr("Runners"), tr("In the courses") }, rows);
	
	html += heading(3, tr("Controls and courses"));
	rows.clear();
	for (int i = 0; i < statistics.matrix_controls.size(); ++i)
		rows.push_back(QStringList(statistics.matrix_controls[i]) + statistics.matrix[std::size_t(i)]);
	html += rows.empty() ? none() : table(QStringList(tr("Control")) + statistics.matrix_courses, rows);
	
	html += heading(3, tr("Controls which are closer to each other than %1").arg(metres(statistics.close_limit_m)));
	rows.clear();
	for (auto const& pair : statistics.close_pairs)
		rows.push_back({ pair.first, pair.second, metres(pair.distance_m) });
	html += rows.empty() ? none() : table({ tr("Control"), tr("Control"), tr("Distance") }, rows);
	
	html += heading(3, tr("Legs between two controls in both directions"));
	rows.clear();
	for (auto const& opposite : statistics.opposite)
		rows.push_back({ opposite.first, opposite.second, opposite.forward.join(QLatin1String(", ")), opposite.backward.join(QLatin1String(", ")) });
	html += rows.empty() ? none() : table({ tr("Control"), tr("Control"), tr("One way"), tr("The other way") }, rows);
	
	html += heading(3, tr("Identical courses"));
	rows.clear();
	for (auto const& group : statistics.identical)
		rows.push_back({ group.join(QLatin1String(", ")) });
	html += rows.empty() ? none() : table({ tr("Courses") }, rows);
	
	html += heading(3, tr("Courses without start or finish"));
	html += statistics.without_start_or_finish.isEmpty() ? none()
	        : QLatin1String("<p>") + escaped(statistics.without_start_or_finish.join(QLatin1String(", "))) + QLatin1String("</p>\n");
	
	if (!statistics.not_verified.isEmpty())
	{
		html += heading(3, tr("Courses with variations, which are not looked at for legs and directions"));
		html += QLatin1String("<p>") + escaped(statistics.not_verified.join(QLatin1String(", "))) + QLatin1String("</p>\n");
	}
	return html;
}

}  // namespace



QStringList courseCheckSections()
{
	return {
		tr("Missing course items"),
		tr("Unused controls"),
		tr("Control description shortcomings"),
		tr("Errors and warnings in the punching unit IDs"),
		tr("Identical courses"),
		tr("Legs in opposite directions"),
		tr("Controls too close to each other"),
		tr("Relays and parts of courses"),
	};
}


int courseCheckSection(const QByteArray& rule)
{
	auto const& rules = sectionRules();
	for (std::size_t s = 0; s < rules.size(); ++s)
	{
		for (auto const* name : rules[s])
		{
			if (rule == name)
				return int(s);
		}
	}
	return -1;
}


QString courseReportHtml(const Map& map, const CourseData& data, const CourseReportOptions& options)
{
	auto const title = options.title.isEmpty() ? tr("Courses") : options.title;
	QString html = QStringLiteral("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>")
	               + escaped(title) + QLatin1String("</title>\n</head>\n<body>\n");
	html += heading(1, title);
	if (options.date.isValid())
		html += QLatin1String("<p>") + escaped(options.date.toString(Qt::ISODate)) + QLatin1String("</p>\n");
	if (options.check)
		html += checkHtml(checkCourses(map, data));
	if (options.statistics)
		html += statisticsHtml(courseStatistics(map, data));
	return html + QLatin1String("</body>\n</html>\n");
}


void paintCourseReport(const QString& html, QPagedPaintDevice* device)
{
	QTextDocument document;
	document.setHtml(html);
	// Without a page size of its own the document is laid out on the pages of the device.
	document.print(device);
}


bool writeCourseReportPdf(const QString& html, const QString& path)
{
	{
		QPdfWriter writer(path);
		if (!writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(15.0, 15.0, 15.0, 15.0), QPageLayout::Millimeter)))
			return false;
		writer.setResolution(300);
		writer.setCreator(QCoreApplication::applicationName());
		paintCourseReport(html, &writer);
	}
	// QPdfWriter says nothing about a file which it could not open; there is then none.
	return QFileInfo(path).size() > 0;
}


}  // namespace OpenOrienteering
