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

#include "selection_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QString>
#include <QTime>
#include <QVBoxLayout>


namespace OpenOrienteering {

namespace {

void finish(QDialog* dialog, QFormLayout* form)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

}  // namespace



SelectByDateDialog::SelectByDateDialog(QWidget* parent)
: QDialog(parent)
{
	// the design notes, "Selection queries".
	setWindowTitle(tr("Select objects by date"));
	created_button = new QRadioButton(tr("Created"));
	modified_button = new QRadioButton(tr("Last modified"));
	modified_button->setChecked(true);
	auto const now = QDateTime::currentDateTime();
	from_edit = new QDateTimeEdit(QDateTime(now.date(), QTime(0, 0)));
	from_edit->setCalendarPopup(true);
	to_edit = new QDateTimeEdit(now);
	to_edit->setCalendarPopup(true);
	auto* form = new QFormLayout();
	form->addRow(created_button);
	form->addRow(modified_button);
	form->addRow(tr("From:"), from_edit);
	form->addRow(tr("To:"), to_edit);
	finish(this, form);
}

SelectByDateDialog::~SelectByDateDialog() = default;

DateKind SelectByDateDialog::kind() const { return created_button->isChecked() ? DateKind::Created : DateKind::Modified; }
qint64 SelectByDateDialog::from() const { return from_edit->dateTime().toSecsSinceEpoch(); }
qint64 SelectByDateDialog::to() const { return to_edit->dateTime().toSecsSinceEpoch(); }



SelectByPropertyDialog::SelectByPropertyDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Select objects by property"));
	property_combo = new QComboBox();
	property_combo->addItem(tr("Length (mm)"), int(PropertyQuery::Length));
	property_combo->addItem(tr("Area (mm²)"), int(PropertyQuery::Area));
	property_combo->addItem(tr("Number of vertices"), int(PropertyQuery::Vertices));
	property_combo->addItem(tr("Rotation (°)"), int(PropertyQuery::Rotation));
	property_combo->addItem(tr("Text length"), int(PropertyQuery::TextLength));
	compare_combo = new QComboBox();
	compare_combo->addItem(QStringLiteral("="), int(PropertyQuery::Equal));
	compare_combo->addItem(QStringLiteral("<"), int(PropertyQuery::Less));
	compare_combo->addItem(QStringLiteral(">"), int(PropertyQuery::Greater));
	compare_combo->addItem(QStringLiteral("<="), int(PropertyQuery::LessEqual));
	compare_combo->addItem(QStringLiteral(">="), int(PropertyQuery::GreaterEqual));
	compare_combo->addItem(QStringLiteral("<>"), int(PropertyQuery::NotEqual));
	compare_combo->setCurrentIndex(1);
	value_edit = new QDoubleSpinBox();
	value_edit->setDecimals(3);
	value_edit->setRange(-1000000.0, 1000000.0);
	value_edit->setValue(1.0);
	symbols_check = new QCheckBox(tr("Only the selected symbols"));
	auto* form = new QFormLayout();
	form->addRow(tr("Property:"), property_combo);
	form->addRow(tr("Operator:"), compare_combo);
	form->addRow(tr("Value:"), value_edit);
	form->addRow(symbols_check);
	finish(this, form);
}

SelectByPropertyDialog::~SelectByPropertyDialog() = default;

PropertyQuery SelectByPropertyDialog::query() const
{
	PropertyQuery query;
	query.property = PropertyQuery::Property(property_combo->currentData().toInt());
	query.compare = PropertyQuery::Compare(compare_combo->currentData().toInt());
	query.value = value_edit->value();
	return query;
}

bool SelectByPropertyDialog::onlySelectedSymbols() const { return symbols_check->isChecked(); }


}  // namespace OpenOrienteering
