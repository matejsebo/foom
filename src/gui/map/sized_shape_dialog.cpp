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


#include "sized_shape_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>


namespace OpenOrienteering {


SizedShapeDialog::SizedShapeDialog(const ops::SizedShapeParams& initial, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Shape of a given size"));
	kind_combo = new QComboBox();
	kind_combo->addItem(tr("Circle"), int(ops::SizedShapeParams::Circle));
	kind_combo->addItem(tr("Ellipse"), int(ops::SizedShapeParams::Ellipse));
	kind_combo->addItem(tr("Line"), int(ops::SizedShapeParams::Line));
	kind_combo->addItem(tr("Rectangle"), int(ops::SizedShapeParams::Rectangle));
	kind_combo->setCurrentIndex(kind_combo->findData(int(initial.kind)));
	unit_combo = new QComboBox();
	unit_combo->addItem(tr("mm on the map"));
	unit_combo->addItem(tr("m on the ground"));
	unit_combo->setCurrentIndex(initial.metres ? 1 : 0);

	auto const sizeEdit = [](double value) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(2);
		edit->setRange(0.01, 100000.0);
		edit->setValue(value);
		return edit;
	};
	width_label = new QLabel();
	width_edit = sizeEdit(initial.width);
	height_label = new QLabel();
	height_edit = sizeEdit(initial.height);
	angle_edit = new QDoubleSpinBox();
	angle_edit->setDecimals(1);
	angle_edit->setRange(-360.0, 360.0);
	angle_edit->setSuffix(QStringLiteral(" °"));
	angle_edit->setValue(initial.angle_deg);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QFormLayout();
	layout->addRow(tr("Shape:"), kind_combo);
	layout->addRow(tr("Unit:"), unit_combo);
	layout->addRow(width_label, width_edit);
	layout->addRow(height_label, height_edit);
	layout->addRow(tr("Angle:"), angle_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);

	connect(kind_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SizedShapeDialog::kindChanged);
	kindChanged();
}

SizedShapeDialog::~SizedShapeDialog() = default;

void SizedShapeDialog::kindChanged()
{
	auto const kind = ops::SizedShapeParams::Kind(kind_combo->currentData().toInt());
	switch (kind)
	{
	case ops::SizedShapeParams::Circle:
		width_label->setText(tr("Radius:"));
		break;
	case ops::SizedShapeParams::Ellipse:
		width_label->setText(tr("First axis:"));
		height_label->setText(tr("Second axis:"));
		break;
	case ops::SizedShapeParams::Line:
		width_label->setText(tr("Length:"));
		break;
	case ops::SizedShapeParams::Rectangle:
		width_label->setText(tr("Width:"));
		height_label->setText(tr("Height:"));
		break;
	}
	auto const two_sizes = kind == ops::SizedShapeParams::Ellipse || kind == ops::SizedShapeParams::Rectangle;
	height_label->setVisible(two_sizes);
	height_edit->setVisible(two_sizes);
	angle_edit->setEnabled(kind != ops::SizedShapeParams::Circle);
}

ops::SizedShapeParams SizedShapeDialog::params() const
{
	ops::SizedShapeParams params;
	params.kind = ops::SizedShapeParams::Kind(kind_combo->currentData().toInt());
	params.metres = unit_combo->currentIndex() == 1;
	params.width = width_edit->value();
	params.height = height_edit->value();
	params.angle_deg = angle_edit->value();
	return params;
}


}  // namespace OpenOrienteering
