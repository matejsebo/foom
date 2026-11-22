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


#include "course_bulk_dialog.h"

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

#include <Qt>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>

#include "core/map.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_storage.h"
#include "gui/widgets/course_widget.h"

namespace OpenOrienteering {

namespace {

/// The number at the end of a code; 0 when there is none.
int trailingNumber(const QString& code)
{
	auto first = code.size();
	while (first > 0 && code.at(first - 1).isDigit())
		--first;
	return code.mid(first).toInt();
}

/// Fills a list with the course objects which can be items of a course.
void fillObjects(QComboBox* box, const CourseData& data)
{
	std::vector<const CourseObject*> objects;
	for (auto const& object : data.objects)
	{
		if (canBeCourseItem(object.kind))
			objects.push_back(&object);
	}
	std::sort(begin(objects), end(objects), [](const CourseObject* lhs, const CourseObject* rhs) {
		return std::make_tuple(int(lhs->kind), trailingNumber(lhs->code), lhs->code)
		       < std::make_tuple(int(rhs->kind), trailingNumber(rhs->code), rhs->code);
	});
	for (auto const* object : objects)
		box->addItem(QString(object->code + QLatin1String("  (") + CourseWidget::kindName(object->kind) + QLatin1Char(')')), object->code);
}

}  // namespace



CourseBulkDialog::CourseBulkDialog(Map* map, Mode mode, const QString& code, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint)
, map(map)
, mode(mode)
{
	setWindowTitle(mode == Insert ? tr("Insert course object into courses") : tr("Delete course object from courses"));
	auto const& data = map->courseData();
	
	object_box = new QComboBox();
	object_box->setObjectName(QStringLiteral("course-bulk-object"));
	fillObjects(object_box, data);
	// The markers, which have no course object, after the objects.
	for (auto const& marker : {
	         std::make_pair(CourseItem::CrossingPoint, tr("Mandatory crossing point")),
	         std::make_pair(CourseItem::Passage, tr("Mandatory passage")),
	         std::make_pair(CourseItem::MapExchange, tr("Map exchange")),
	         std::make_pair(CourseItem::MapFlip, tr("Map flip")) })
	{
		object_box->addItem(marker.second, QString{});
		object_box->setItemData(object_box->count() - 1, int(marker.first), markerRole());
	}
	object_box->setCurrentIndex(std::max(0, object_box->findData(code)));
	
	course_list = new QListWidget();
	course_list->setObjectName(QStringLiteral("course-bulk-courses"));
	for (auto const& course : data.courses)
	{
		auto* item = new QListWidgetItem(course.name, course_list);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}
	auto* all_button = new QPushButton(tr("All"));
	auto* none_button = new QPushButton(tr("None"));
	auto* check_layout = new QHBoxLayout();
	check_layout->addWidget(all_button);
	check_layout->addWidget(none_button);
	check_layout->addStretch(1);
	auto* courses_layout = new QVBoxLayout();
	courses_layout->addWidget(course_list);
	courses_layout->addLayout(check_layout);
	
	auto* relation_layout = new QHBoxLayout();
	if (mode == Remove)
	{
		all_radio = new QRadioButton(tr("All"));
		all_radio->setObjectName(QStringLiteral("course-bulk-all"));
		relation_layout->addWidget(all_radio);
	}
	after_radio = new QRadioButton(tr("After"));
	after_radio->setObjectName(QStringLiteral("course-bulk-after"));
	before_radio = new QRadioButton(tr("Before"));
	before_radio->setObjectName(QStringLiteral("course-bulk-before"));
	between_radio = new QRadioButton(tr("Between"));
	between_radio->setObjectName(QStringLiteral("course-bulk-between"));
	relation_layout->addWidget(after_radio);
	relation_layout->addWidget(before_radio);
	relation_layout->addWidget(between_radio);
	relation_layout->addStretch(1);
	(all_radio ? all_radio : after_radio)->setChecked(true);
	
	first_box = new QComboBox();
	first_box->setObjectName(QStringLiteral("course-bulk-first"));
	fillObjects(first_box, data);
	second_box = new QComboBox();
	second_box->setObjectName(QStringLiteral("course-bulk-second"));
	fillObjects(second_box, data);
	auto* place_layout = new QHBoxLayout();
	place_layout->addWidget(first_box, 1);
	place_layout->addWidget(new QLabel(tr("and")));
	place_layout->addWidget(second_box, 1);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Course object:"), object_box);
	form->addRow(mode == Insert ? tr("To courses:") : tr("From courses:"), courses_layout);
	form->addRow(tr("Where:"), relation_layout);
	form->addRow(QString{}, place_layout);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
	
	connect(all_button, &QAbstractButton::clicked, this, [this]() { checkAll(true); });
	connect(none_button, &QAbstractButton::clicked, this, [this]() { checkAll(false); });
	for (auto* radio : { all_radio, after_radio, before_radio, between_radio })
	{
		if (radio)
			connect(radio, &QAbstractButton::toggled, this, &CourseBulkDialog::updateWidgets);
	}
	connect(buttons, &QDialogButtonBox::accepted, this, &CourseBulkDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CourseBulkDialog::reject);
	updateWidgets();
}

CourseBulkDialog::~CourseBulkDialog() = default;


void CourseBulkDialog::updateWidgets()
{
	first_box->setEnabled(!all_radio || !all_radio->isChecked());
	second_box->setEnabled(between_radio->isChecked());
}


void CourseBulkDialog::checkAll(bool checked)
{
	for (int i = 0; i < course_list->count(); ++i)
		course_list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}


CourseCondition CourseBulkDialog::condition() const
{
	CourseCondition result;
	if (all_radio && all_radio->isChecked())
	{
		result.relation = CourseCondition::All;
		return result;
	}
	if (after_radio->isChecked())
		result.relation = CourseCondition::After;
	else if (before_radio->isChecked())
		result.relation = CourseCondition::Before;
	else if (between_radio->isChecked())
		result.relation = CourseCondition::Between;
	result.first = first_box->currentData().toString();
	if (result.relation == CourseCondition::Between)
		result.second = second_box->currentData().toString();
	return result;
}


bool CourseBulkDialog::apply(QString* message)
{
	QString text;
	auto const code = object_box->currentData().toString();
	auto const marker = object_box->currentData(markerRole());
	QStringList courses;
	for (int i = 0; i < course_list->count(); ++i)
	{
		if (course_list->item(i)->checkState() == Qt::Checked)
			courses.push_back(course_list->item(i)->text());
	}
	
	auto data = map->courseData();
	CourseBulkReport report;
	if (marker.isValid())
	{
		auto const type = CourseItem::Type(marker.toInt());
		report = mode == Insert ? insertMarkerIntoCourses(data, type, courses, condition())
		                        : removeMarkerFromCourses(data, type, courses, condition());
	}
	else
	{
		report = mode == Insert ? insertIntoCourses(data, code, courses, condition())
		                        : removeFromCourses(data, code, courses, condition());
	}
	switch (report.error)
	{
	case CourseBulkReport::NoError:
		if (report.changed.empty())
			text = tr("None of the courses has such a place.");
		break;
	case CourseBulkReport::NoObject:
		text = tr("There is no course object to choose.");
		break;
	case CourseBulkReport::NoCourses:
		text = tr("No course is checked.");
		break;
	case CourseBulkReport::NoCondition:
		text = tr("The place is not said.");
		break;
	case CourseBulkReport::InvalidOrder:
		text = tr("The place cannot be named by the course object itself, nor by the same object twice.");
		break;
	}
	if (!text.isEmpty())
	{
		if (message)
			*message = text;
		return false;
	}
	
	changeCourseData(*map, std::move(data));
	auto const what = marker.isValid() ? object_box->currentText() : code;
	last_report = mode == Insert ? tr("%1 was put into %n course(s).", nullptr, int(report.changed.size())).arg(what)
	                             : tr("%1 was taken out of %n course(s).", nullptr, int(report.changed.size())).arg(what);
	if (!report.unchanged.isEmpty())
		last_report += QLatin1Char(' ') + tr("Not changed: %1.").arg(report.unchanged.join(QLatin1String(", ")));
	if (message)
		*message = last_report;
	return true;
}


void CourseBulkDialog::accept()
{
	QString message;
	if (apply(&message))
		QDialog::accept();
	else
		QMessageBox::warning(this, windowTitle(), message);
}



CourseRenumberDialog::CourseRenumberDialog(Map* map, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint)
, map(map)
{
	setWindowTitle(tr("Renumber all controls"));
	auto const first = std::max(1, firstControlCode(map->courseData()));
	
	old_edit = new QSpinBox();
	old_edit->setObjectName(QStringLiteral("course-renumber-old"));
	old_edit->setRange(1, 99999);
	old_edit->setValue(first);
	new_edit = new QSpinBox();
	new_edit->setObjectName(QStringLiteral("course-renumber-new"));
	new_edit->setRange(1, 99999);
	new_edit->setValue(first);
	gaps_check = new QCheckBox(tr("Close numbering gaps"));
	gaps_check->setObjectName(QStringLiteral("course-renumber-gaps"));
	
	auto* form = new QFormLayout();
	form->addRow(tr("Old first code:"), old_edit);
	form->addRow(tr("New first code:"), new_edit);
	form->addRow(QString{}, gaps_check);
	auto* note = new QLabel(tr("The controls whose code is a number at or above the old first code get new codes."));
	note->setWordWrap(true);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(note);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &CourseRenumberDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CourseRenumberDialog::reject);
}

CourseRenumberDialog::~CourseRenumberDialog() = default;


bool CourseRenumberDialog::apply(QString* message)
{
	QString text;
	auto data = map->courseData();
	auto const report = renumberControls(data, old_edit->value(), new_edit->value(), gaps_check->isChecked());
	if (!report.clash.isEmpty())
		text = tr("The code %1 belongs to a course object which is not renumbered.").arg(report.clash);
	else if (!report.done)
		text = tr("There is no control whose code is a number at or above the old first code.");
	else if (report.renumbered == 0)
		text = tr("The controls have these codes already.");
	if (!text.isEmpty())
	{
		if (message)
			*message = text;
		return false;
	}
	
	changeCourseData(*map, std::move(data));
	last_report = tr("%n control(s) got a new code.", nullptr, report.renumbered);
	if (message)
		*message = last_report;
	return true;
}


void CourseRenumberDialog::accept()
{
	QString message;
	if (apply(&message))
		QDialog::accept();
	else
		QMessageBox::warning(this, windowTitle(), message);
}


}  // namespace OpenOrienteering
