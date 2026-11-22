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


#include "course_maps_dialog.h"

#include <Qt>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>

#include "core/extents.h"
#include "core/map.h"
#include "course/course_data.h"

namespace OpenOrienteering {

CourseMapsDialog::CourseMapsDialog(const Map* map, const QString& folder, QWidget* parent, Purpose purpose)
: QDialog(parent)
, purpose(purpose)
{
	setWindowTitle(purpose == Print ? tr("Print course maps") : tr("Export course maps"));
	
	course_list = new QListWidget();
	course_list->setObjectName(QStringLiteral("course-maps-courses"));
	for (auto const& course : map->courseData().courses)
	{
		auto* item = new QListWidgetItem(course.name, course_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
	}
	
	relay_box = new QComboBox();
	relay_box->setObjectName(QStringLiteral("course-maps-relay"));
	relay_box->addItem(tr("A map for every variant"));
	relay_box->addItem(tr("A map for every runner"));
	relay_box->setToolTip(tr("A map for every runner has his start number, and needs the teams of the class of the relay."));
	
	extent_box = new QComboBox();
	extent_box->setObjectName(QStringLiteral("course-maps-extent"));
	extent_box->addItem(map->hasPrinterConfig() ? tr("Print area") : tr("The drawn map"));
	for (auto const& extent : map->extents())
		extent_box->addItem(extent.name);
	
	// the design notes, "Course maps as map files"
	format_box = new QComboBox();
	format_box->setObjectName(QStringLiteral("course-maps-format"));
	format_box->addItem(tr("PDF files"), QString{});
	format_box->addItem(tr("FOOM map files (.foom)"), QStringLiteral("XML"));
	format_box->addItem(tr("OCD map files (.ocd)"), QStringLiteral("OCD2018"));
	format_box->setToolTip(tr("A map file is written for every part of a course, with the whole map; extent, canvases and routes are for PDF files."));
	
	// the design notes, "Several courses in one file"
	placing_box = new QComboBox();
	placing_box->setObjectName(QStringLiteral("course-maps-placing"));
	placing_box->setToolTip(tr("All maps can go into one file: as its pages, or for map files the map repeated beside or below each other; at their place all courses lie over one map."));
	
	// the design notes, "Course only, intensity, line width and the control descriptions"
	content_box = new QComboBox();
	content_box->setObjectName(QStringLiteral("course-maps-content"));
	content_box->addItem(tr("Map and course"), int(CourseMapContent::Map));
	content_box->addItem(tr("Course only, for pre-printed maps"), int(CourseMapContent::CourseOnly));
	content_box->addItem(tr("Control descriptions"), int(CourseMapContent::Descriptions));
	auto const percent = [](const char* name, int minimum, int maximum) {
		auto* spin = new QSpinBox();
		spin->setObjectName(QString::fromLatin1(name));
		spin->setRange(minimum, maximum);
		spin->setValue(100);
		spin->setSuffix(QStringLiteral(" %"));
		return spin;
	};
	intensity_spin = percent("course-maps-intensity", 10, 100);
	line_width_spin = percent("course-maps-line-width", 50, 200);
	auto const number = [](const char* name, double minimum, double maximum, double value, const QString& suffix) {
		auto* spin = new QDoubleSpinBox();
		spin->setObjectName(QString::fromLatin1(name));
		spin->setDecimals(1);
		spin->setRange(minimum, maximum);
		spin->setSingleStep(0.1);
		spin->setValue(value);
		spin->setSuffix(suffix);
		return spin;
	};
	stretch_x_spin = number("course-maps-stretch-x", 90.0, 110.0, 100.0, QStringLiteral(" %"));
	stretch_y_spin = number("course-maps-stretch-y", 90.0, 110.0, 100.0, QStringLiteral(" %"));
	offset_x_spin = number("course-maps-offset-x", -25.0, 25.0, 0.0, QStringLiteral(" mm"));
	offset_y_spin = number("course-maps-offset-y", -25.0, 25.0, 0.0, QStringLiteral(" mm"));
	stretch_x_spin->setToolTip(tr("Horizontal stretch about the bottom left corner of the page"));
	stretch_y_spin->setToolTip(tr("Vertical stretch about the bottom left corner of the page"));
	offset_x_spin->setToolTip(tr("To the right"));
	offset_y_spin->setToolTip(tr("Upwards"));
	mirror_check = new QCheckBox(tr("Mirror"));
	mirror_check->setObjectName(QStringLiteral("course-maps-mirror"));
	auto const pair = [](QWidget* first, QWidget* second) {
		auto* widget = new QWidget();
		auto* row = new QHBoxLayout(widget);
		row->setContentsMargins(0, 0, 0, 0);
		row->addWidget(first);
		row->addWidget(second);
		return widget;
	};
	
	// the design notes, "Canvases: the dialog"
	auto const& canvases = map->courseData().canvases;
	canvases_check = new QCheckBox(tr("By the canvases of the map"));
	canvases_check->setObjectName(QStringLiteral("course-maps-canvases"));
	canvases_check->setToolTip(tr("Every map has the extent, the scale and the hidden symbols of the canvas of its course; a course without a canvas has no map."));
	canvases_check->setChecked(canvases.active && !canvases.canvases.empty());
	connect(canvases_check, &QCheckBox::toggled, this, &CourseMapsDialog::updateEnabled);
	
	folder_edit = new QLineEdit(QDir::toNativeSeparators(folder));
	folder_edit->setObjectName(QStringLiteral("course-maps-folder"));
	auto* browse_button = new QPushButton(tr("Choose..."));
	auto* folder_widget = new QWidget();
	auto* folder_layout = new QHBoxLayout(folder_widget);
	folder_layout->setContentsMargins(0, 0, 0, 0);
	folder_layout->addWidget(folder_edit, 1);
	folder_layout->addWidget(browse_button);
	
	auto* form = new QFormLayout();
	auto* format_label = new QLabel(tr("Files:"));
	form->addRow(format_label, format_box);
	form->addRow(tr("Placing:"), placing_box);
	form->addRow(tr("Content:"), content_box);
	form->addRow(tr("Intensity:"), intensity_spin);
	form->addRow(tr("Line width:"), line_width_spin);
	form->addRow(tr("Stretch:"), pair(stretch_x_spin, stretch_y_spin));
	form->addRow(tr("Adjustment:"), pair(offset_x_spin, offset_y_spin));
	form->addRow(QString{}, mirror_check);
	form->addRow(tr("Relays:"), relay_box);
	form->addRow(tr("Extent:"), extent_box);
	form->addRow(QString{}, canvases_check);
	// the design notes, "Route analysis: the routes on the maps of the courses"
	routes_check = new QCheckBox(tr("With the routes of the route analysis"));
	routes_check->setObjectName(QStringLiteral("course-maps-routes"));
	routes_check->setToolTip(tr("The routes which the map keeps are painted over the map of their course."));
	has_routes = !map->courseData().routes.isEmpty();
	has_canvases = !canvases.canvases.empty();
	form->addRow(QString{}, routes_check);
	auto* folder_label = new QLabel(tr("Folder:"));
	form->addRow(folder_label, folder_widget);
	// the design notes, "Printing the course maps"
	if (purpose == Print)
	{
		for (auto* widget : { static_cast<QWidget*>(format_label), static_cast<QWidget*>(format_box), static_cast<QWidget*>(folder_label), folder_widget })
			widget->hide();
	}
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	ok_button = buttons->button(QDialogButtonBox::Ok);
	
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("A file is written for every course which is checked; for a course with a map exchange one for every map, for a relay as chosen below; or all of them go into one file, as chosen under Placing.")));
	qobject_cast<QLabel*>(layout->itemAt(0)->widget())->setWordWrap(true);
	layout->addWidget(course_list, 1);
	layout->addLayout(form);
	layout->addWidget(buttons);
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(browse_button, &QPushButton::clicked, this, &CourseMapsDialog::browse);
	connect(course_list, &QListWidget::itemChanged, this, &CourseMapsDialog::updateButtons);
	connect(folder_edit, &QLineEdit::textChanged, this, &CourseMapsDialog::updateButtons);
	connect(format_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseMapsDialog::fillPlacings);
	connect(placing_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseMapsDialog::updateEnabled);
	connect(content_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseMapsDialog::updateEnabled);
	fillPlacings();
	updateButtons();
}

CourseMapsDialog::~CourseMapsDialog() = default;


CourseMapExportOptions CourseMapsDialog::options() const
{
	CourseMapExportOptions result;
	result.folder = QDir::fromNativeSeparators(folder_edit->text().trimmed());
	for (int i = 0; i < course_list->count(); ++i)
	{
		if (course_list->item(i)->checkState() == Qt::Checked)
			result.courses.push_back(course_list->item(i)->text());
	}
	result.relay_maps = relay_box->currentIndex() == 1 ? RelayMaps::Runners : RelayMaps::Variants;
	if (extent_box->currentIndex() > 0)
		result.extent = extent_box->currentText();
	result.map_format = format_box->currentData().toString();
	result.use_canvases = canvases_check->isEnabled() && canvases_check->isChecked();
	result.with_routes = routes_check->isEnabled() && routes_check->isChecked();
	if (placing_box->currentIndex() >= 0)
		result.placing = CourseMapPlacing(placing_box->currentData().toInt());
	if (content_box->isEnabled())
	{
		result.content = CourseMapContent(content_box->currentData().toInt());
		result.intensity = intensity_spin->value();
		result.line_width = line_width_spin->value();
	}
	if (result.content == CourseMapContent::CourseOnly)
	{
		result.stretch_x = stretch_x_spin->value();
		result.stretch_y = stretch_y_spin->value();
		result.mirror = mirror_check->isChecked();
		result.offset_x = offset_x_spin->value();
		result.offset_y = offset_y_spin->value();
	}
	return result;
}


void CourseMapsDialog::browse()
{
	auto const folder = QFileDialog::getExistingDirectory(this, windowTitle(), folder_edit->text());
	if (!folder.isEmpty())
		folder_edit->setText(QDir::toNativeSeparators(folder));
}


void CourseMapsDialog::fillPlacings()
{
	auto const kept = placing_box->currentIndex() >= 0 ? placing_box->currentData().toInt() : int(CourseMapPlacing::Separate);
	auto const pdf = format_box->currentIndex() == 0;
	{
		const QSignalBlocker blocker(placing_box);
		placing_box->clear();
		placing_box->addItem(tr("A file for every map"), int(CourseMapPlacing::Separate));
		if (pdf)
		{
			placing_box->addItem(tr("One file, a page for every map"), int(CourseMapPlacing::Pages));
			placing_box->addItem(tr("One file, all maps at their place on one page"), int(CourseMapPlacing::Original));
		}
		else
		{
			placing_box->addItem(tr("One file, the maps beside each other"), int(CourseMapPlacing::Beside));
			placing_box->addItem(tr("One file, the maps below each other"), int(CourseMapPlacing::Below));
			placing_box->addItem(tr("One file, all maps at their place"), int(CourseMapPlacing::Original));
		}
		auto const index = placing_box->findData(kept);
		placing_box->setCurrentIndex(index >= 0 ? index : 0);
	}
	updateEnabled();
}


void CourseMapsDialog::updateEnabled()
{
	auto const pdf = format_box->currentIndex() == 0;
	auto const separate = placing_box->currentData().toInt() == int(CourseMapPlacing::Separate);
	auto const content = pdf ? content_box->currentData().toInt() : int(CourseMapContent::Map);
	auto const descriptions = content == int(CourseMapContent::Descriptions);
	canvases_check->setEnabled(pdf && has_canvases && separate && !descriptions);
	routes_check->setEnabled(pdf && has_routes && !descriptions);
	extent_box->setEnabled(pdf && !descriptions && !(canvases_check->isEnabled() && canvases_check->isChecked()));
	content_box->setEnabled(pdf);
	intensity_spin->setEnabled(pdf);
	line_width_spin->setEnabled(pdf);
	auto const course_only = content == int(CourseMapContent::CourseOnly);
	for (auto* widget : { static_cast<QWidget*>(stretch_x_spin), static_cast<QWidget*>(stretch_y_spin), static_cast<QWidget*>(offset_x_spin),
	                      static_cast<QWidget*>(offset_y_spin), static_cast<QWidget*>(mirror_check) })
		widget->setEnabled(course_only);
}


void CourseMapsDialog::updateButtons()
{
	auto const chosen = options();
	ok_button->setEnabled(!chosen.courses.isEmpty() && (purpose == Print || !chosen.folder.isEmpty()));
}


}  // namespace OpenOrienteering
