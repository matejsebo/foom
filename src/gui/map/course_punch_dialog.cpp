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


#include "course_punch_dialog.h"

#include <cstddef>
#include <utility>

#include <Qt>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include "core/map.h"
#include "course/course_data.h"
#include "course/course_punch.h"
#include "course/course_storage.h"

namespace OpenOrienteering {

CoursePunchDialog::CoursePunchDialog(Map* map, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowMaximizeButtonHint)
, map(map)
{
	setWindowTitle(tr("Punching unit IDs"));
	auto const& data = map->courseData();
	
	table = new QTableWidget();
	table->setObjectName(QStringLiteral("course-punch-table"));
	table->setColumnCount(2);
	table->setHorizontalHeaderLabels({ tr("Control code"), tr("Punching unit IDs") });
	table->verticalHeader()->setVisible(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	auto const order = punchControlOrder(data);
	table->setRowCount(int(order.size()));
	for (int row = 0; row < int(order.size()); ++row)
	{
		auto const& object = data.objects[order[std::size_t(row)]];
		auto* code_item = new QTableWidgetItem(object.code);
		code_item->setFlags(code_item->flags() & ~Qt::ItemIsEditable);
		code_item->setData(Qt::UserRole, object.code);
		table->setItem(row, 0, code_item);
		auto const text = joinPunchIds(object.punch_ids);
		auto* ids_item = new QTableWidgetItem(text);
		ids_item->setData(Qt::UserRole, text);
		table->setItem(row, 1, ids_item);
	}
	if (table->rowCount() > 0)
		table->setCurrentCell(0, 1);
	
	list = new QListWidget();
	list->setObjectName(QStringLiteral("course-punch-list"));
	for (auto id : offeredPunchIds())
		list->addItem(QString::number(id));
	list->setMaximumWidth(list->sizeHintForColumn(0) + 48);
	list->setToolTip(tr("A double click adds the number to the punching unit IDs of the chosen control."));
	connect(list, &QListWidget::itemDoubleClicked, this, &CoursePunchDialog::addListedId);
	auto* list_layout = new QVBoxLayout();
	list_layout->addWidget(new QLabel(tr("Punching unit IDs:")));
	list_layout->addWidget(list);
	
	system_box = new QComboBox();
	system_box->setObjectName(QStringLiteral("course-punch-system"));
	system_box->addItem(QStringLiteral("SPORTident"), int(PunchSystem::SportIdent));
	system_box->addItem(QStringLiteral("Emit"), int(PunchSystem::Emit));
	system_box->addItem(tr("Other"), int(PunchSystem::Other));
	auto* system_form = new QFormLayout();
	system_form->addRow(tr("Punching system:"), system_box);
	
	only_radio = new QRadioButton(tr("Only add to controls without a punching unit ID"));
	only_radio->setObjectName(QStringLiteral("course-punch-only"));
	replace_radio = new QRadioButton(tr("Add and replace punching unit IDs which are already assigned"));
	replace_radio->setObjectName(QStringLiteral("course-punch-replace"));
	keep_radio = new QRadioButton(tr("Add but do not replace punching unit IDs which are already assigned"));
	keep_radio->setObjectName(QStringLiteral("course-punch-keep"));
	keep_radio->setChecked(true);
	auto* add_button = new QPushButton(tr("Add"));
	add_button->setObjectName(QStringLiteral("course-punch-add"));
	add_button->setAutoDefault(false);
	connect(add_button, &QPushButton::clicked, this, &CoursePunchDialog::addDefaults);
	auto* defaults_box = new QGroupBox(tr("Add default punching unit IDs"));
	auto* defaults_layout = new QVBoxLayout(defaults_box);
	defaults_layout->addWidget(only_radio);
	defaults_layout->addWidget(replace_radio);
	defaults_layout->addWidget(keep_radio);
	defaults_layout->addWidget(add_button, 0, Qt::AlignRight);
	auto* note = new QLabel(tr("The default ID of a control is its code, when the code is a number."));
	note->setWordWrap(true);
	
	auto* right_layout = new QVBoxLayout();
	right_layout->addLayout(system_form);
	right_layout->addWidget(defaults_box);
	right_layout->addWidget(note);
	right_layout->addStretch(1);
	
	auto* columns = new QHBoxLayout();
	columns->addWidget(table, 2);
	columns->addLayout(list_layout);
	columns->addLayout(right_layout, 2);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(columns, 1);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &CoursePunchDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CoursePunchDialog::reject);
	resize(760, 520);
}

CoursePunchDialog::~CoursePunchDialog() = default;


bool CoursePunchDialog::idsOfRow(int row, QStringList* ids) const
{
	auto const* ids_item = table->item(row, 1);
	auto const text = ids_item->text();
	if (text == ids_item->data(Qt::UserRole).toString())
	{
		if (auto const* object = map->courseData().findObject(table->item(row, 0)->data(Qt::UserRole).toString()))
		{
			*ids = object->punch_ids;
			return true;
		}
	}
	return parsePunchIds(text, ids);
}


void CoursePunchDialog::addListedId(QListWidgetItem* item)
{
	auto const row = table->currentRow();
	if (!item || row < 0)
		return;
	auto* ids_item = table->item(row, 1);
	QStringList ids;
	if (idsOfRow(row, &ids))
		ids_item->setText(joinPunchIds(addPunchId(ids, item->text())));
	else
		ids_item->setText(ids_item->text() + QLatin1Char(';') + item->text());
}


void CoursePunchDialog::addDefaults()
{
	auto mode = PunchDefaults::AddKeep;
	if (only_radio->isChecked())
		mode = PunchDefaults::OnlyWithout;
	else if (replace_radio->isChecked())
		mode = PunchDefaults::Replace;
	auto const system = PunchSystem(system_box->currentData().toInt());
	
	for (int row = 0; row < table->rowCount(); ++row)
	{
		auto const* object = map->courseData().findObject(table->item(row, 0)->data(Qt::UserRole).toString());
		if (!object)
			continue;
		auto* ids_item = table->item(row, 1);
		QStringList ids;
		if (!idsOfRow(row, &ids))
		{
			// What cannot be read is left for the user, unless it is replaced.
			auto const id = defaultPunchId(*object, system);
			if (mode == PunchDefaults::Replace && !id.isEmpty())
				ids_item->setText(id);
			continue;
		}
		auto const result = withDefaultPunchId(*object, ids, mode, system);
		if (result != ids)
			ids_item->setText(joinPunchIds(result));
	}
}


bool CoursePunchDialog::apply(QString* message)
{
	auto data = map->courseData();
	int changed = 0;
	for (int row = 0; row < table->rowCount(); ++row)
	{
		auto const code = table->item(row, 0)->data(Qt::UserRole).toString();
		QStringList ids;
		if (!idsOfRow(row, &ids))
		{
			table->setCurrentCell(row, 1);
			auto const text = tr("The punching unit IDs of the control %1 are not numbers: %2").arg(code, table->item(row, 1)->text());
			if (message)
				*message = text;
			return false;
		}
		for (auto& object : data.objects)
		{
			if (object.code == code && object.kind == CourseObject::Control && object.punch_ids != ids)
			{
				object.punch_ids = ids;
				++changed;
			}
		}
	}
	
	if (changed > 0)
	{
		changeCourseData(*map, std::move(data));
		for (int row = 0; row < table->rowCount(); ++row)
		{
			auto* ids_item = table->item(row, 1);
			if (auto const* object = map->courseData().findObject(table->item(row, 0)->data(Qt::UserRole).toString()))
				ids_item->setText(joinPunchIds(object->punch_ids));
			ids_item->setData(Qt::UserRole, ids_item->text());
		}
		last_report = tr("%n control(s) got other punching unit IDs.", nullptr, changed);
	}
	else
	{
		last_report = tr("No control got other punching unit IDs.");
	}
	if (message)
		*message = last_report;
	return true;
}


void CoursePunchDialog::accept()
{
	QString message;
	if (apply(&message))
		QDialog::accept();
	else
		QMessageBox::warning(this, windowTitle(), message);
}


}  // namespace OpenOrienteering
