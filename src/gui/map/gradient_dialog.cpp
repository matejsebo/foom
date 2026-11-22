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


#include "gradient_dialog.h"

#include <Qt>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QString>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/map_color.h"
#include "gui/widgets/color_dropdown.h"


namespace OpenOrienteering {

GradientDialog::GradientDialog(const Map& map, bool line_selected, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Gradient fill"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	auto* form = new QFormLayout();
	from_color = new ColorDropDown(&map, map.getNumColors() > 0 ? map.getColor(map.getNumColors() - 1) : nullptr);
	form->addRow(tr("From colour:"), from_color);
	to_color = new ColorDropDown(&map, map.getNumColors() > 0 ? map.getColor(0) : nullptr);
	form->addRow(tr("To colour:"), to_color);
	angle = new QDoubleSpinBox();
	angle->setRange(-360, 360);
	angle->setDecimals(1);
	angle->setSuffix(QString(QChar(0x00B0)));
	angle->setValue(0);
	form->addRow(tr("Angle:"), angle);
	use_line = new QCheckBox(tr("Use the selected line for the direction"));
	use_line->setEnabled(line_selected);
	use_line->setChecked(line_selected);
	form->addRow(use_line);
	connect(use_line, &QCheckBox::toggled, angle, &QWidget::setDisabled);
	angle->setDisabled(use_line->isChecked());
	keep_areas = new QCheckBox(tr("Keep the areas"));
	form->addRow(keep_areas);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout();
	layout->addLayout(form);
	layout->addWidget(buttons);
	setLayout(layout);
}

GradientDialog::~GradientDialog() = default;

ops::GradientParams GradientDialog::params() const
{
	ops::GradientParams result;
	if (auto const* color = from_color->color())
		result.from_color = static_cast<const QColor&>(*color);
	if (auto const* color = to_color->color())
		result.to_color = static_cast<const QColor&>(*color);
	result.angle = angle->value();
	result.use_line = use_line->isChecked();
	result.keep_areas = keep_areas->isChecked();
	return result;
}


}  // namespace OpenOrienteering
