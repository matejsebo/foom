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


#include "to_curve_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QString>
#include <QVBoxLayout>


namespace OpenOrienteering {


ToCurveDialog::ToCurveDialog(const ops::ToCurveParams& initial, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("To curve"));
	all_button = new QRadioButton(tr("Convert all segments (contours)"));
	straight_button = new QRadioButton(tr("Keep the straight parts (roads)"));
	(initial.keep_straight_parts ? straight_button : all_button)->setChecked(true);

	factor_edit = new QDoubleSpinBox();
	factor_edit->setDecimals(2);
	factor_edit->setRange(-0.5, 2.0);
	factor_edit->setSingleStep(0.05);
	factor_edit->setValue(initial.tangent_factor);
	ends_check = new QCheckBox(tr("Keep the angle at the start and end of open lines"));
	ends_check->setChecked(initial.keep_end_angles);
	corners_check = new QCheckBox(tr("Keep a corner where the direction turns by more than:"));
	corners_check->setChecked(initial.keep_corners);
	angle_edit = new QDoubleSpinBox();
	angle_edit->setDecimals(0);
	angle_edit->setRange(1.0, 179.0);
	angle_edit->setSuffix(QStringLiteral(" °"));
	angle_edit->setValue(initial.corner_angle_deg);
	angle_edit->setEnabled(initial.keep_corners);
	connect(corners_check, &QCheckBox::toggled, angle_edit, &QWidget::setEnabled);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* corner_row = new QHBoxLayout();
	corner_row->addWidget(corners_check);
	corner_row->addWidget(angle_edit);
	corner_row->addStretch();
	auto* layout = new QFormLayout();
	layout->addRow(all_button);
	layout->addRow(straight_button);
	layout->addRow(tr("Tangent length factor:"), factor_edit);
	layout->addRow(ends_check);
	layout->addRow(corner_row);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

ToCurveDialog::~ToCurveDialog() = default;

ops::ToCurveParams ToCurveDialog::params() const
{
	ops::ToCurveParams params;
	params.keep_straight_parts = straight_button->isChecked();
	params.tangent_factor = factor_edit->value();
	params.keep_end_angles = ends_check->isChecked();
	params.keep_corners = corners_check->isChecked();
	params.corner_angle_deg = angle_edit->value();
	return params;
}


}  // namespace OpenOrienteering
