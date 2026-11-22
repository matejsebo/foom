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


#include "crop_dialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>


namespace OpenOrienteering {

namespace {

QDoubleSpinBox* coordinateEdit(double value)
{
	auto* edit = new QDoubleSpinBox();
	edit->setRange(-1000000.0, 1000000.0);
	edit->setDecimals(2);
	edit->setSuffix(QStringLiteral(" mm"));
	edit->setValue(value);
	return edit;
}

}  // namespace


CropDialog::CropDialog(bool have_shapes, const QRectF& rect, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Crop"));
	
	auto* objects_box = new QGroupBox(tr("Objects to crop"));
	all_button = new QRadioButton(tr("All objects"));
	symbols_button = new QRadioButton(tr("Objects of the symbols selected in the symbol pane"));
	all_button->setChecked(true);
	auto* objects_layout = new QVBoxLayout(objects_box);
	objects_layout->addWidget(all_button);
	objects_layout->addWidget(symbols_button);
	
	auto* shape_box = new QGroupBox(tr("Cropping shape"));
	shapes_button = new QRadioButton(tr("The selected lines and areas"));
	rect_button = new QRadioButton(tr("A rectangle"));
	shapes_button->setEnabled(have_shapes);
	(have_shapes ? shapes_button : rect_button)->setChecked(true);
	left_edit = coordinateEdit(rect.left());
	top_edit = coordinateEdit(rect.top());
	right_edit = coordinateEdit(rect.right());
	bottom_edit = coordinateEdit(rect.bottom());
	auto* rect_form = new QFormLayout();
	rect_form->addRow(tr("Left:"), left_edit);
	rect_form->addRow(tr("Top:"), top_edit);
	rect_form->addRow(tr("Right:"), right_edit);
	rect_form->addRow(tr("Bottom:"), bottom_edit);
	auto* shape_layout = new QVBoxLayout(shape_box);
	shape_layout->addWidget(shapes_button);
	shape_layout->addWidget(rect_button);
	shape_layout->addLayout(rect_form);
	
	delete_check = new QCheckBox(tr("Delete the cropping objects"));
	hole_check = new QCheckBox(tr("Crop a hole"));
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(shapes_button, &QAbstractButton::toggled, this, &CropDialog::updateWidgets);
	
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(objects_box);
	layout->addWidget(shape_box);
	layout->addWidget(delete_check);
	layout->addWidget(hole_check);
	layout->addWidget(buttons);
	updateWidgets();
}

CropDialog::~CropDialog() = default;


bool CropDialog::selectedSymbolsOnly() const
{
	return symbols_button->isChecked();
}

bool CropDialog::useRectangle() const
{
	return rect_button->isChecked();
}

QRectF CropDialog::rectangle() const
{
	return QRectF(QPointF(left_edit->value(), top_edit->value()), QPointF(right_edit->value(), bottom_edit->value())).normalized();
}

bool CropDialog::hole() const
{
	return hole_check->isChecked();
}

bool CropDialog::deleteShapes() const
{
	return delete_check->isEnabled() && delete_check->isChecked();
}

void CropDialog::updateWidgets()
{
	auto const rect = rect_button->isChecked();
	for (auto* edit : { left_edit, top_edit, right_edit, bottom_edit })
		edit->setEnabled(rect);
	delete_check->setEnabled(!rect);
}


}  // namespace OpenOrienteering
