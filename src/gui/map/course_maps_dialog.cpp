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
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

#include "core/extents.h"
#include "core/map.h"
#include "course/course_data.h"

namespace OpenOrienteering {

CourseMapsDialog::CourseMapsDialog(const Map* map, const QString& folder, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Export course maps"));
	
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
	auto* folder_layout = new QHBoxLayout();
	folder_layout->addWidget(folder_edit, 1);
	folder_layout->addWidget(browse_button);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Files:"), format_box);
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
	form->addRow(tr("Folder:"), folder_layout);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	ok_button = buttons->button(QDialogButtonBox::Ok);
	
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("A file is written for every course which is checked; for a course with a map exchange one for every map, for a relay as chosen below.")));
	qobject_cast<QLabel*>(layout->itemAt(0)->widget())->setWordWrap(true);
	layout->addWidget(course_list, 1);
	layout->addLayout(form);
	layout->addWidget(buttons);
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(browse_button, &QPushButton::clicked, this, &CourseMapsDialog::browse);
	connect(course_list, &QListWidget::itemChanged, this, &CourseMapsDialog::updateButtons);
	connect(folder_edit, &QLineEdit::textChanged, this, &CourseMapsDialog::updateButtons);
	connect(format_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseMapsDialog::updateEnabled);
	updateEnabled();
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
	return result;
}


void CourseMapsDialog::browse()
{
	auto const folder = QFileDialog::getExistingDirectory(this, windowTitle(), folder_edit->text());
	if (!folder.isEmpty())
		folder_edit->setText(QDir::toNativeSeparators(folder));
}


void CourseMapsDialog::updateEnabled()
{
	auto const pdf = format_box->currentIndex() == 0;
	canvases_check->setEnabled(pdf && has_canvases);
	routes_check->setEnabled(pdf && has_routes);
	extent_box->setEnabled(pdf && !canvases_check->isChecked());
}


void CourseMapsDialog::updateButtons()
{
	auto const chosen = options();
	ok_button->setEnabled(!chosen.courses.isEmpty() && !chosen.folder.isEmpty());
}


}  // namespace OpenOrienteering
