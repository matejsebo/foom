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

#include "map_transform_dialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QString>
#include <QVBoxLayout>


namespace OpenOrienteering {

namespace {

QDoubleSpinBox* makeEdit(double value, double minimum, double maximum, const QString& suffix)
{
	auto* edit = new QDoubleSpinBox();
	edit->setDecimals(2);
	edit->setRange(minimum, maximum);
	edit->setSuffix(suffix);
	edit->setValue(value);
	return edit;
}

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



MapMoveDialog::MapMoveDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Move map"));
	east_edit = makeEdit(0.0, -1000000.0, 1000000.0, QStringLiteral(" m"));
	north_edit = makeEdit(0.0, -1000000.0, 1000000.0, QStringLiteral(" m"));
	carry_check = new QCheckBox(tr("Also move templates, layout objects, extents and bookmarks"));
	carry_check->setChecked(true);
	auto* form = new QFormLayout();
	form->addRow(tr("Easting:"), east_edit);
	form->addRow(tr("Northing:"), north_edit);
	form->addRow(carry_check);
	finish(this, form);
}

MapMoveDialog::~MapMoveDialog() = default;

double MapMoveDialog::east() const { return east_edit->value(); }
double MapMoveDialog::north() const { return north_edit->value(); }
bool MapMoveDialog::carry() const { return carry_check->isChecked(); }



MapStretchDialog::MapStretchDialog(bool mirror, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(mirror ? tr("Mirror map") : tr("Stretch / shrink map"));
	x_edit = makeEdit(100.0, 1.0, 10000.0, QStringLiteral(" %"));
	y_edit = makeEdit(100.0, 1.0, 10000.0, QStringLiteral(" %"));
	horizontal_check = new QCheckBox(tr("Mirror horizontally"));
	horizontal_check->setChecked(mirror);
	vertical_check = new QCheckBox(tr("Mirror vertically"));
	vertical_check->setChecked(mirror);
	carry_check = new QCheckBox(tr("Also transform templates, layout objects, extents and bookmarks"));
	carry_check->setChecked(true);
	auto* form = new QFormLayout();
	form->addRow(tr("Horizontal stretch / shrink:"), x_edit);
	form->addRow(tr("Vertical stretch / shrink:"), y_edit);
	form->addRow(horizontal_check);
	form->addRow(vertical_check);
	form->addRow(carry_check);
	finish(this, form);
}

MapStretchDialog::~MapStretchDialog() = default;

double MapStretchDialog::factorX() const { return x_edit->value() / 100.0; }
double MapStretchDialog::factorY() const { return y_edit->value() / 100.0; }
bool MapStretchDialog::mirrorHorizontally() const { return horizontal_check->isChecked(); }
bool MapStretchDialog::mirrorVertically() const { return vertical_check->isChecked(); }
bool MapStretchDialog::carry() const { return carry_check->isChecked(); }


}  // namespace OpenOrienteering
