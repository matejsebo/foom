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


#include "field_copy_dialog.h"

#include <Qt>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>


namespace OpenOrienteering {

FieldCopyDialog::FieldCopyDialog(const QRectF& visible_area, const QRectF& selection_area, QWidget* parent)
: QDialog(parent)
, visible_area(visible_area)
, selection_area(selection_area)
{
	setWindowTitle(tr("Create field copy"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	whole_button = new QRadioButton(tr("The whole map"));
	whole_button->setObjectName(QStringLiteral("extent-whole"));
	whole_button->setChecked(true);
	visible_button = new QRadioButton(tr("The visible area"));
	visible_button->setObjectName(QStringLiteral("extent-visible"));
	visible_button->setEnabled(visible_area.isValid());
	selection_button = new QRadioButton(tr("The area of the selected objects"));
	selection_button->setObjectName(QStringLiteral("extent-selection"));
	selection_button->setEnabled(selection_area.isValid());
	auto* extent_layout = new QVBoxLayout();
	extent_layout->addWidget(whole_button);
	extent_layout->addWidget(visible_button);
	extent_layout->addWidget(selection_button);
	
	templates_check = new QCheckBox(tr("Include the templates as images"));
	templates_check->setObjectName(QStringLiteral("templates"));
	dpi_spin = new QSpinBox();
	dpi_spin->setObjectName(QStringLiteral("dpi"));
	dpi_spin->setRange(50, 1200);
	dpi_spin->setValue(200);
	dpi_spin->setSuffix(tr(" dpi"));
	dpi_spin->setEnabled(false);
	auto* templates_layout = new QHBoxLayout();
	templates_layout->addWidget(templates_check);
	templates_layout->addWidget(dpi_spin);
	templates_layout->addStretch(1);
	
	review_check = new QCheckBox(tr("Review copy: only what is drawn in its sketch part comes back"));
	review_check->setObjectName(QStringLiteral("review"));
	
	auto* form = new QFormLayout();
	form->addRow(tr("Objects in:"), extent_layout);
	form->addRow(tr("Templates:"), templates_layout);
	form->addRow(QString{}, review_check);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout();
	layout->addLayout(form);
	layout->addWidget(buttons);
	setLayout(layout);
	
	connect(templates_check, &QCheckBox::toggled, dpi_spin, &QSpinBox::setEnabled);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

FieldCopyDialog::~FieldCopyDialog() = default;

FieldCopyOptions FieldCopyDialog::options() const
{
	FieldCopyOptions result;
	if (visible_button->isChecked())
		result.extent = visible_area;
	else if (selection_button->isChecked())
		result.extent = selection_area;
	if (templates_check->isChecked())
		result.template_dpi = dpi_spin->value();
	result.review = review_check->isChecked();
	return result;
}


}  // namespace OpenOrienteering
