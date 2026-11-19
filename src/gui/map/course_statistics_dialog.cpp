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


#include "course_statistics_dialog.h"

#include <cstddef>

#include <Qt>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QIODevice>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "core/map.h"
#include "course/course_render.h"
#include "gui/widgets/course_widget.h"

namespace OpenOrienteering {

namespace {

QTableWidget* makeTable(const char* name, const QStringList& headers)
{
	auto* table = new QTableWidget(0, headers.size());
	table->setObjectName(QString::fromLatin1(name));
	table->setHorizontalHeaderLabels(headers);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setStretchLastSection(true);
	return table;
}

void addRow(QTableWidget* table, const QStringList& cells)
{
	auto const row = table->rowCount();
	table->insertRow(row);
	for (int column = 0; column < cells.size(); ++column)
		table->setItem(row, column, new QTableWidgetItem(cells[column]));
}

QString lengthText(double metres)
{
	return metres < 0.0 ? QStringLiteral("-") : CourseStatisticsDialog::tr("%1 m").arg(qRound(metres));
}

}  // namespace



CourseStatisticsDialog::CourseStatisticsDialog(Map* map, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
, statistics(courseStatistics(*map, map->courseData()))
{
	setWindowTitle(tr("Course and event statistics"));
	const QString separator = QStringLiteral(", ");
	
	auto* course_table = makeTable("course-stat-courses", { tr("Course"), tr("Classes"), tr("Runners"), tr("Length"), tr("Climb"), tr("Controls") });
	for (auto const& course : statistics.courses)
	{
		addRow(course_table, { course.name, course.classes.join(separator), QString::number(course.runners),
		                       course.measured ? formatCourseLength(course.length_m) : QStringLiteral("-"),
		                       lengthText(course.climb_m), QString::number(course.controls) });
	}
	
	auto* object_table = makeTable("course-stat-objects", { tr("Code"), tr("Kind"), tr("Courses"), tr("Runners"), tr("In the courses") });
	for (auto const& object : statistics.objects)
	{
		addRow(object_table, { object.code, CourseWidget::kindName(object.kind), QString::number(object.courses.size()),
		                       QString::number(object.runners), object.courses.join(separator) });
	}
	
	leg_table = makeTable("course-stat-legs", { tr("From"), tr("To"), tr("Length"), tr("Courses"), tr("Runners"), tr("In the courses") });
	hide_single_check = new QCheckBox(tr("Hide legs used in one course only"));
	hide_single_check->setObjectName(QStringLiteral("course-stat-hide-single"));
	auto* leg_page = new QWidget();
	auto* leg_layout = new QVBoxLayout(leg_page);
	leg_layout->addWidget(leg_table);
	leg_layout->addWidget(hide_single_check);
	fillLegs();
	
	auto* matrix_table = makeTable("course-stat-matrix", QStringList(tr("Control")) + statistics.matrix_courses);
	matrix_table->horizontalHeader()->setStretchLastSection(false);
	for (int i = 0; i < statistics.matrix_controls.size(); ++i)
		addRow(matrix_table, QStringList(statistics.matrix_controls[i]) + statistics.matrix[std::size_t(i)]);
	matrix_table->resizeColumnsToContents();
	
	auto* event_edit = new QPlainTextEdit();
	event_edit->setObjectName(QStringLiteral("course-stat-event"));
	event_edit->setReadOnly(true);
	event_edit->setPlainText(courseEventText(statistics));
	
	auto* tabs = new QTabWidget();
	tabs->addTab(course_table, tr("Courses"));
	tabs->addTab(object_table, tr("Course objects"));
	tabs->addTab(leg_page, tr("Legs"));
	tabs->addTab(matrix_table, tr("Controls and courses"));
	tabs->addTab(event_edit, tr("Event"));
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto* export_button = buttons->addButton(tr("Export..."), QDialogButtonBox::ActionRole);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addWidget(buttons);
	resize(720, 480);
	
	connect(hide_single_check, &QAbstractButton::toggled, this, &CourseStatisticsDialog::fillLegs);
	connect(export_button, &QAbstractButton::clicked, this, &CourseStatisticsDialog::exportClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &CourseStatisticsDialog::reject);
}

CourseStatisticsDialog::~CourseStatisticsDialog() = default;


void CourseStatisticsDialog::fillLegs()
{
	const QString separator = QStringLiteral(", ");
	leg_table->setRowCount(0);
	for (auto const& leg : statistics.legs)
	{
		if (hide_single_check->isChecked() && leg.courses.size() < 2)
			continue;
		addRow(leg_table, { leg.from, leg.to, lengthText(leg.length_m), QString::number(leg.courses.size()),
		                    QString::number(leg.runners), leg.courses.join(separator) });
	}
}


QString CourseStatisticsDialog::text() const
{
	return courseStatisticsText(statistics);
}


bool CourseStatisticsDialog::exportTo(const QString& path) const
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	auto const bytes = text().toUtf8();
	return file.write(bytes) == bytes.size();
}


void CourseStatisticsDialog::exportClicked()
{
	auto const path = QFileDialog::getSaveFileName(this, tr("Export course statistics"), QString{}, tr("Text files (*.txt)"));
	if (path.isEmpty())
		return;
	if (!exportTo(path))
		QMessageBox::warning(this, windowTitle(), tr("The file could not be written."));
}


}  // namespace OpenOrienteering
