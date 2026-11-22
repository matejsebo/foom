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


#include "course_report_dialog.h"

#include <Qt>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIODevice>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "core/map.h"

namespace OpenOrienteering {

CourseReportDialog::CourseReportDialog(Map* map, const QString& title, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint)
, map(map)
, title(title)
{
	setWindowTitle(tr("Course report"));
	
	check_box = new QCheckBox(tr("Consistency check"));
	check_box->setObjectName(QStringLiteral("course-report-check"));
	check_box->setChecked(true);
	statistics_box = new QCheckBox(tr("Statistics"));
	statistics_box->setObjectName(QStringLiteral("course-report-statistics"));
	statistics_box->setChecked(true);
	auto* options_layout = new QHBoxLayout();
	options_layout->addWidget(check_box);
	options_layout->addWidget(statistics_box);
	options_layout->addStretch(1);
	
	view = new QTextBrowser();
	view->setObjectName(QStringLiteral("course-report-view"));
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto* print_button = buttons->addButton(tr("Print..."), QDialogButtonBox::ActionRole);
	print_button->setObjectName(QStringLiteral("course-report-print"));
	auto* pdf_button = buttons->addButton(tr("Save as PDF..."), QDialogButtonBox::ActionRole);
	pdf_button->setObjectName(QStringLiteral("course-report-pdf"));
	auto* html_button = buttons->addButton(tr("Save as HTML..."), QDialogButtonBox::ActionRole);
	html_button->setObjectName(QStringLiteral("course-report-html"));
	
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(options_layout);
	layout->addWidget(view, 1);
	layout->addWidget(buttons);
	resize(720, 800);
	
	connect(check_box, &QAbstractButton::toggled, this, &CourseReportDialog::updateReport);
	connect(statistics_box, &QAbstractButton::toggled, this, &CourseReportDialog::updateReport);
	connect(print_button, &QAbstractButton::clicked, this, &CourseReportDialog::printClicked);
	connect(pdf_button, &QAbstractButton::clicked, this, &CourseReportDialog::savePdfClicked);
	connect(html_button, &QAbstractButton::clicked, this, &CourseReportDialog::saveHtmlClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &CourseReportDialog::reject);
	updateReport();
}

CourseReportDialog::~CourseReportDialog() = default;


CourseReportOptions CourseReportDialog::options() const
{
	CourseReportOptions result;
	result.check = check_box->isChecked();
	result.statistics = statistics_box->isChecked();
	result.title = title;
	result.date = QDate::currentDate();
	return result;
}


void CourseReportDialog::updateReport()
{
	report_html = courseReportHtml(*map, map->courseData(), options());
	view->setHtml(report_html);
}


bool CourseReportDialog::savePdf(const QString& path) const
{
	return writeCourseReportPdf(report_html, path);
}


bool CourseReportDialog::saveHtml(const QString& path) const
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	auto const bytes = report_html.toUtf8();
	return file.write(bytes) == bytes.size();
}


void CourseReportDialog::printClicked()
{
	QPrinter printer(QPrinter::HighResolution);
	printer.setDocName(title);
	QPrintDialog dialog(&printer, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	paintCourseReport(report_html, &printer);
}


void CourseReportDialog::savePdfClicked()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save as PDF"), {}, tr("PDF files (*.pdf)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
		path += QLatin1String(".pdf");
	if (!savePdf(path))
		QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(path));
}


void CourseReportDialog::saveHtmlClicked()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save as HTML"), {}, tr("HTML files (*.html *.htm)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive) && !path.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive))
		path += QLatin1String(".html");
	if (!saveHtml(path))
		QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(path));
}


}  // namespace OpenOrienteering
