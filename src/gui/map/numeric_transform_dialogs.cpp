/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "numeric_transform_dialogs.h"

#include <QtGlobal>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QString>
#include <QWidget>

#include "core/ops/transform.h"


namespace OpenOrienteering {

namespace {

QDoubleSpinBox* makeSpinBox(double value, double minimum, double maximum, int decimals, const QString& suffix)
{
	auto* box = new QDoubleSpinBox();
	box->setRange(minimum, maximum);
	box->setDecimals(decimals);
	box->setSuffix(suffix);
	box->setValue(value);
	return box;
}

QDialogButtonBox* makeButtons(QDialog& dialog)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	return buttons;
}

}  // namespace


bool NumericTransformDialogs::askMoveBy(QWidget* parent, ops::MoveParams& params)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(tr("Move by"));
	auto* right = makeSpinBox(params.dx, -100000.0, 100000.0, 3, tr(" mm"));
	auto* up = makeSpinBox(-params.dy, -100000.0, 100000.0, 3, tr(" mm"));
	auto* keep = new QCheckBox(tr("Keep the original"));
	keep->setChecked(params.duplicate);
	auto* layout = new QFormLayout(&dialog);
	layout->addRow(tr("Right:"), right);
	layout->addRow(tr("Up:"), up);
	layout->addRow(keep);
	layout->addRow(makeButtons(dialog));
	if (dialog.exec() != QDialog::Accepted)
		return false;
	
	params.dx = right->value();
	params.dy = -up->value();  // map y grows downwards
	params.duplicate = keep->isChecked();
	return true;
}

bool NumericTransformDialogs::askRotateBy(QWidget* parent, ops::RotateParams& params)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(tr("Rotate by"));
	auto* angle = makeSpinBox(params.angle, -360.0, 360.0, 2, tr("\u00b0"));
	auto* common = new QRadioButton(tr("About the common centre"));
	auto* each = new QRadioButton(tr("Each object about its own centre"));
	(params.each_object ? each : common)->setChecked(true);
	auto* keep = new QCheckBox(tr("Keep the original"));
	keep->setChecked(params.duplicate);
	auto* layout = new QFormLayout(&dialog);
	layout->addRow(tr("Angle (counter-clockwise):"), angle);
	layout->addRow(common);
	layout->addRow(each);
	layout->addRow(keep);
	layout->addRow(makeButtons(dialog));
	if (dialog.exec() != QDialog::Accepted)
		return false;
	
	params.angle = angle->value();
	params.each_object = each->isChecked();
	params.duplicate = keep->isChecked();
	return true;
}

bool NumericTransformDialogs::askStretch(QWidget* parent, ops::StretchParams& params)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(tr("Stretch"));
	auto* horizontal = makeSpinBox(params.factor_x, -1000.0, 1000.0, 3, QString{});
	auto* vertical = makeSpinBox(params.factor_y, -1000.0, 1000.0, 3, QString{});
	auto* lock = new QCheckBox(tr("Lock ratio"));
	lock->setChecked(qFuzzyCompare(params.factor_x, params.factor_y));
	QObject::connect(horizontal, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [lock, vertical](double value) {
		if (lock->isChecked())
			vertical->setValue(value);
	});
	QObject::connect(lock, &QCheckBox::toggled, [horizontal, vertical](bool checked) {
		if (checked)
			vertical->setValue(horizontal->value());
	});
	auto* keep = new QCheckBox(tr("Keep the original"));
	keep->setChecked(params.duplicate);
	auto* layout = new QFormLayout(&dialog);
	layout->addRow(tr("Horizontal factor:"), horizontal);
	layout->addRow(tr("Vertical factor:"), vertical);
	layout->addRow(lock);
	layout->addRow(keep);
	layout->addRow(makeButtons(dialog));
	if (dialog.exec() != QDialog::Accepted)
		return false;
	
	params.factor_x = horizontal->value();
	params.factor_y = vertical->value();
	params.duplicate = keep->isChecked();
	return true;
}


}  // namespace OpenOrienteering
