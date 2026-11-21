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


#include "lengthen_line_text_dialog.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QString>
#include <QVBoxLayout>


namespace OpenOrienteering {

LengthenLineTextDialog::LengthenLineTextDialog(const ops::LengthenLineTextParams& initial, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Lengthen line text"));

	distance_radio = new QRadioButton(tr("By distance:"));
	distance_edit = new QDoubleSpinBox();
	distance_edit->setDecimals(1);
	distance_edit->setRange(0.1, 1000.0);
	distance_edit->setSingleStep(1.0);
	distance_edit->setSuffix(QStringLiteral(" mm"));
	distance_edit->setValue(initial.distance);

	percent_radio = new QRadioButton(tr("By percentage:"));
	percent_edit = new QDoubleSpinBox();
	percent_edit->setDecimals(0);
	percent_edit->setRange(1.0, 1000.0);
	percent_edit->setSingleStep(5.0);
	percent_edit->setSuffix(QStringLiteral(" %"));
	percent_edit->setValue(initial.percent);

	distance_radio->setChecked(!initial.by_percent);
	percent_radio->setChecked(initial.by_percent);
	auto const update_enabled = [this]() {
		distance_edit->setEnabled(distance_radio->isChecked());
		percent_edit->setEnabled(percent_radio->isChecked());
	};
	update_enabled();
	connect(distance_radio, &QAbstractButton::toggled, this, update_enabled);
	connect(percent_radio, &QAbstractButton::toggled, this, update_enabled);

	auto* form = new QFormLayout();
	form->addRow(distance_radio, distance_edit);
	form->addRow(percent_radio, percent_edit);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

LengthenLineTextDialog::~LengthenLineTextDialog() = default;


ops::LengthenLineTextParams LengthenLineTextDialog::params() const
{
	ops::LengthenLineTextParams params;
	params.by_percent = percent_radio->isChecked();
	params.distance = distance_edit->value();
	params.percent = percent_edit->value();
	return params;
}


}  // namespace OpenOrienteering
