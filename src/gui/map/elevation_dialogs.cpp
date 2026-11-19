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


#include "elevation_dialogs.h"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>


namespace OpenOrienteering {

namespace {

QComboBox* intervalCombo(std::initializer_list<double> values, double current)
{
	auto* combo = new QComboBox();
	combo->setEditable(true);
	for (auto value : values)
		combo->addItem(QString::number(value) + QStringLiteral(" m"), value);
	auto const index = combo->findData(current);
	if (index >= 0)
		combo->setCurrentIndex(index);
	else
		combo->setEditText(QString::number(current));
	return combo;
}

double comboValue(const QComboBox* combo, double fallback)
{
	auto text = combo->currentText();
	text.remove(QStringLiteral(" m"));
	bool ok = false;
	auto const value = text.toDouble(&ok);
	return (ok && value > 0.0) ? value : fallback;
}

QDialogButtonBox* makeButtons(QDialog* dialog)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	return buttons;
}

}  // namespace



ElevationOpenDialog::ElevationOpenDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Import elevation data"));
	kind_combo = new QComboBox();
	kind_combo->addItem(tr("Terrain (DTM)"), int(ElevationSet::Terrain));
	kind_combo->addItem(tr("Surface (DSM)"), int(ElevationSet::Surface));
	kind_combo->addItem(tr("Canopy height"), int(ElevationSet::Canopy));
	feet_check = new QCheckBox(tr("Heights are in feet; convert to metres"));
	millimetres_check = new QCheckBox(tr("Heights are in millimetres; convert to metres"));
	shift_check = new QCheckBox(tr("Shift values below sea level"));

	auto* layout = new QFormLayout();
	layout->addRow(tr("Grid:"), kind_combo);
	layout->addRow(feet_check);
	layout->addRow(millimetres_check);
	layout->addRow(shift_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

ElevationOpenDialog::~ElevationOpenDialog() = default;

ElevationSet::Kind ElevationOpenDialog::kind() const
{
	return ElevationSet::Kind(kind_combo->currentData().toInt());
}

ElevationModel::OpenOptions ElevationOpenDialog::options() const
{
	ElevationModel::OpenOptions options;
	options.feet_to_metres = feet_check->isChecked();
	options.millimetres_to_metres = millimetres_check->isChecked();
	options.shift_below_sea_level = shift_check->isChecked();
	return options;
}



ContourDialog::ContourDialog(double minimum, double maximum, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Contours"));
	unsmoothed = makeSet(tr("Unsmoothed contours"), false, true);
	smoothed = makeSet(tr("Smoothed contours"), true, false);

	minimum_edit = new QDoubleSpinBox();
	minimum_edit->setDecimals(1);
	minimum_edit->setRange(-20000.0, 20000.0);
	minimum_edit->setSuffix(QStringLiteral(" m"));
	minimum_edit->setValue(std::isnan(minimum) ? -20000.0 : minimum);
	maximum_edit = new QDoubleSpinBox();
	maximum_edit->setDecimals(1);
	maximum_edit->setRange(-20000.0, 20000.0);
	maximum_edit->setSuffix(QStringLiteral(" m"));
	maximum_edit->setValue(std::isnan(maximum) ? 20000.0 : maximum);
	auto* range_layout = new QFormLayout();
	range_layout->addRow(tr("Lowest level:"), minimum_edit);
	range_layout->addRow(tr("Highest level:"), maximum_edit);

	auto* main_layout = new QVBoxLayout(this);
	main_layout->addWidget(unsmoothed.group);
	main_layout->addWidget(smoothed.group);
	main_layout->addLayout(range_layout);
	main_layout->addWidget(makeButtons(this));
}

ContourDialog::~ContourDialog() = default;

ContourDialog::SetWidgets ContourDialog::makeSet(const QString& title, bool is_smoothed, bool enabled)
{
	SetWidgets widgets;
	widgets.group = new QGroupBox(title);
	widgets.group->setCheckable(true);
	widgets.group->setChecked(enabled);
	widgets.interval = intervalCombo({ 0.1, 0.25, 0.5, 1.0, 1.25, 2.0, 2.5, 3.0, 4.0, 5.0 }, is_smoothed ? 2.5 : 1.0);
	widgets.main_interval = intervalCombo({ 1.0, 2.0, 2.5, 5.0, 10.0, 20.0, 25.0 }, 5.0);
	widgets.index_interval = intervalCombo({ 5.0, 10.0, 12.5, 25.0, 50.0, 100.0, 200.0 }, 25.0);
	widgets.depression_check = new QCheckBox(tr("Separate depression symbols"));
	widgets.curves_check = new QCheckBox(tr("Convert to curves"));
	widgets.curves_check->setChecked(is_smoothed);
	widgets.curves_check->setVisible(is_smoothed);
	auto* layout = new QFormLayout(widgets.group);
	layout->addRow(is_smoothed ? tr("Form line interval:") : tr("Smallest interval:"), widgets.interval);
	layout->addRow(tr("Main interval:"), widgets.main_interval);
	layout->addRow(tr("Index interval:"), widgets.index_interval);
	layout->addRow(widgets.depression_check);
	if (is_smoothed)
		layout->addRow(widgets.curves_check);
	return widgets;
}

std::vector<ContourParams> ContourDialog::sets() const
{
	std::vector<ContourParams> result;
	for (auto const* widgets : { &unsmoothed, &smoothed })
	{
		if (!widgets->group->isChecked())
			continue;
		ContourParams params;
		params.smoothed = (widgets == &smoothed);
		params.interval = comboValue(widgets->interval, params.smoothed ? 2.5 : 1.0);
		params.main_interval = comboValue(widgets->main_interval, 5.0);
		params.index_interval = comboValue(widgets->index_interval, 25.0);
		params.depression_symbols = widgets->depression_check->isChecked();
		params.curves = params.smoothed && widgets->curves_check->isChecked();
		params.minimum = minimum_edit->value();
		params.maximum = maximum_edit->value();
		result.push_back(params);
	}
	return result;
}



HillShadeDialog::HillShadeDialog(double cell_size, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Hill shading"));
	HillShadeParams const defaults;
	method_combo = new QComboBox();
	method_combo->addItem(tr("Slope shading"), int(HillShadeParams::SlopeShading));
	method_combo->addItem(tr("Oblique light"), int(HillShadeParams::Oblique));
	method_combo->addItem(tr("Oblique light and slope shading"), int(HillShadeParams::Combined));
	method_combo->setCurrentIndex(method_combo->findData(int(defaults.method)));
	multi_check = new QCheckBox(tr("Multi-directional light"));
	multi_check->setChecked(defaults.multi_directional);
	azimuth_edit = new QDoubleSpinBox();
	azimuth_edit->setDecimals(0);
	azimuth_edit->setRange(0.0, 360.0);
	azimuth_edit->setSuffix(QStringLiteral(" °"));
	azimuth_edit->setValue(defaults.azimuth_deg);
	elevation_edit = new QDoubleSpinBox();
	elevation_edit->setDecimals(0);
	elevation_edit->setRange(1.0, 90.0);
	elevation_edit->setSuffix(QStringLiteral(" °"));
	elevation_edit->setValue(defaults.elevation_deg);
	exaggeration_edit = new QDoubleSpinBox();
	exaggeration_edit->setDecimals(1);
	exaggeration_edit->setRange(1.0, 10.0);
	exaggeration_edit->setValue(defaults.exaggeration);
	cell_edit = new QDoubleSpinBox();
	cell_edit->setDecimals(2);
	cell_edit->setRange(0.01, 1000.0);
	cell_edit->setSuffix(QStringLiteral(" m"));
	cell_edit->setValue(cell_size);
	attach_check = new QCheckBox(tr("Attach as template"));
	attach_check->setChecked(true);

	auto update_enabled = [this]() {
		auto const directional = method_combo->currentData().toInt() != int(HillShadeParams::SlopeShading);
		multi_check->setEnabled(directional);
		azimuth_edit->setEnabled(directional && !multi_check->isChecked());
		elevation_edit->setEnabled(directional);
	};
	connect(method_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update_enabled);
	connect(multi_check, &QCheckBox::toggled, this, update_enabled);
	update_enabled();

	auto* layout = new QFormLayout();
	layout->addRow(tr("Method:"), method_combo);
	layout->addRow(multi_check);
	layout->addRow(tr("Light azimuth:"), azimuth_edit);
	layout->addRow(tr("Sun elevation:"), elevation_edit);
	layout->addRow(tr("Vertical exaggeration:"), exaggeration_edit);
	layout->addRow(tr("Cell size:"), cell_edit);
	layout->addRow(attach_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

HillShadeDialog::~HillShadeDialog() = default;

HillShadeParams HillShadeDialog::params() const
{
	HillShadeParams params;
	params.method = HillShadeParams::Method(method_combo->currentData().toInt());
	params.multi_directional = multi_check->isChecked();
	params.azimuth_deg = azimuth_edit->value();
	params.elevation_deg = elevation_edit->value();
	params.exaggeration = exaggeration_edit->value();
	params.cell_size = cell_edit->value();
	return params;
}

bool HillShadeDialog::attach() const
{
	return attach_check->isChecked();
}



SlopeDialog::SlopeDialog(double cell_size, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Slope gradient"));
	SlopeParams const defaults;
	continuous_button = new QRadioButton(tr("Continuous: grey up to the threshold, black above"));
	binary_button = new QRadioButton(tr("Black and white: white below the threshold, black above"));
	(defaults.binary ? binary_button : continuous_button)->setChecked(true);
	threshold_edit = new QDoubleSpinBox();
	threshold_edit->setDecimals(0);
	threshold_edit->setRange(1.0, 90.0);
	threshold_edit->setSuffix(QStringLiteral(" °"));
	threshold_edit->setValue(defaults.threshold_deg);
	cell_edit = new QDoubleSpinBox();
	cell_edit->setDecimals(2);
	cell_edit->setRange(0.01, 1000.0);
	cell_edit->setSuffix(QStringLiteral(" m"));
	cell_edit->setValue(cell_size);
	attach_check = new QCheckBox(tr("Attach as template"));
	attach_check->setChecked(true);

	auto* layout = new QFormLayout();
	layout->addRow(continuous_button);
	layout->addRow(binary_button);
	layout->addRow(tr("Threshold:"), threshold_edit);
	layout->addRow(tr("Cell size:"), cell_edit);
	layout->addRow(attach_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

SlopeDialog::~SlopeDialog() = default;

SlopeParams SlopeDialog::params() const
{
	SlopeParams params;
	params.binary = binary_button->isChecked();
	params.threshold_deg = threshold_edit->value();
	params.cell_size = cell_edit->value();
	return params;
}

bool SlopeDialog::attach() const
{
	return attach_check->isChecked();
}


}  // namespace OpenOrienteering
