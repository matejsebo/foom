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

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

#include <QApplication>
#include <QPixmap>
#include <QColor>
#include <QPen>
#include <QPalette>
#include <QPainterPath>
#include <QPainter>
#include <QMessageBox>
#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLocale>
#include <QBrush>
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




ElevationResizeDialog::ElevationResizeDialog(const QRectF& extent, QWidget* parent)
: QDialog(parent)
{
	// the design notes, "Grid commands".
	setWindowTitle(tr("Crop elevation grid"));
	auto const makeEdit = [](double value) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(2);
		edit->setRange(-100000000.0, 100000000.0);
		edit->setSuffix(QStringLiteral(" m"));
		edit->setValue(value);
		return edit;
	};
	west_edit = makeEdit(extent.left());
	south_edit = makeEdit(extent.top());
	east_edit = makeEdit(extent.right());
	north_edit = makeEdit(extent.bottom());
	auto* layout = new QFormLayout();
	layout->addRow(tr("West:"), west_edit);
	layout->addRow(tr("South:"), south_edit);
	layout->addRow(tr("East:"), east_edit);
	layout->addRow(tr("North:"), north_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

ElevationResizeDialog::~ElevationResizeDialog() = default;

QRectF ElevationResizeDialog::box() const
{
	return QRectF(QPointF(west_edit->value(), south_edit->value()), QPointF(east_edit->value(), north_edit->value())).normalized();
}



LandformDialog::LandformDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Extract features"));
	LandformParams const defaults;
	auto const makeEdit = [](double value) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(1);
		edit->setRange(0.1, 10.0);
		edit->setSingleStep(0.1);
		edit->setSuffix(QStringLiteral(" m"));
		edit->setValue(value);
		return edit;
	};
	knolls_check = new QCheckBox(tr("Knolls (symbol 109)"));
	knolls_check->setChecked(defaults.knolls);
	height_edit = makeEdit(defaults.min_height);
	depressions_check = new QCheckBox(tr("Small depressions (symbol 111)"));
	depressions_check->setChecked(defaults.depressions);
	depth_edit = makeEdit(defaults.min_depth);
	connect(knolls_check, &QCheckBox::toggled, height_edit, &QWidget::setEnabled);
	connect(depressions_check, &QCheckBox::toggled, depth_edit, &QWidget::setEnabled);
	cliffs_check = new QCheckBox(tr("Cliffs from the slope gradient (symbol 10080)"));
	cliffs_check->setChecked(defaults.cliffs);
	threshold_edit = new QDoubleSpinBox();
	threshold_edit->setDecimals(0);
	threshold_edit->setRange(1.0, 89.0);
	threshold_edit->setSuffix(QStringLiteral(" °"));
	threshold_edit->setValue(defaults.cliff_threshold);
	auto const makeCount = [](int value) {
		auto* edit = new QSpinBox();
		edit->setRange(1, 1000);
		edit->setSuffix(QStringLiteral(" px"));
		edit->setValue(value);
		return edit;
	};
	area_edit = makeCount(defaults.cliff_min_area);
	length_edit = makeCount(defaults.cliff_min_length);
	for (auto* widget : std::initializer_list<QWidget*>{ threshold_edit, area_edit, length_edit })
	{
		widget->setEnabled(defaults.cliffs);
		connect(cliffs_check, &QCheckBox::toggled, widget, &QWidget::setEnabled);
	}
	trees_check = new QCheckBox(tr("Trees (symbol 418)"));
	trees_check->setChecked(defaults.trees);
	tree_height_edit = new QSpinBox();
	tree_height_edit->setRange(1, 50);
	tree_height_edit->setSuffix(QStringLiteral(" m"));
	tree_height_edit->setValue(int(defaults.tree_min_height));
	tree_height_edit->setEnabled(defaults.trees);
	connect(trees_check, &QCheckBox::toggled, tree_height_edit, &QWidget::setEnabled);
	
	auto* layout = new QFormLayout();
	layout->addRow(knolls_check);
	layout->addRow(tr("Minimum height:"), height_edit);
	layout->addRow(depressions_check);
	layout->addRow(tr("Minimum depth:"), depth_edit);
	layout->addRow(cliffs_check);
	layout->addRow(tr("Slope threshold:"), threshold_edit);
	layout->addRow(tr("Minimum area:"), area_edit);
	layout->addRow(tr("Minimum length:"), length_edit);
	layout->addRow(trees_check);
	layout->addRow(tr("Minimum tree height:"), tree_height_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

LandformDialog::~LandformDialog() = default;

LandformParams LandformDialog::params() const
{
	LandformParams params;
	params.knolls = knolls_check->isChecked();
	params.min_height = height_edit->value();
	params.depressions = depressions_check->isChecked();
	params.min_depth = depth_edit->value();
	params.cliffs = cliffs_check->isChecked();
	params.cliff_threshold = threshold_edit->value();
	params.cliff_min_area = area_edit->value();
	params.cliff_min_length = length_edit->value();
	params.trees = trees_check->isChecked();
	params.tree_min_height = tree_height_edit->value();
	return params;
}

PointCloudDialog::PointCloudDialog(const QStringList& paths, QWidget* parent)
: QDialog(parent)
, paths(paths)
{
	setWindowTitle(tr("Import point cloud"));
	QStringList names;
	for (auto const& path : paths)
		names.push_back(QFileInfo(path).fileName());
	auto* files_label = new QLabel(names.join(QLatin1String(", ")));
	files_label->setWordWrap(true);
	auto* analyse_button = new QPushButton(tr("Analyse"));
	report_label = new QLabel();
	report_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	GriddingParams const defaults;
	cell_edit = new QDoubleSpinBox();
	cell_edit->setDecimals(2);
	cell_edit->setRange(0.1, 100.0);
	cell_edit->setSingleStep(0.5);
	cell_edit->setSuffix(QStringLiteral(" m"));
	cell_edit->setValue(defaults.cell);
	dtm_check = new QCheckBox(tr("Terrain model (DTM): ground and water, last returns"));
	dtm_check->setChecked(defaults.dtm);
	dsm_check = new QCheckBox(tr("Surface model (DSM): ground, vegetation, buildings and water, first returns"));
	dsm_check->setChecked(defaults.dsm);
	connect(analyse_button, &QPushButton::clicked, this, &PointCloudDialog::analyse);
	
	// The products: the design notes, "Vegetation height and vegetation base map".
	height_check = new QCheckBox(tr("Vegetation height (surface minus terrain model, coloured)"));
	VegetationParams const vegetation;
	auto const makeEdit = [](double value, double minimum, double maximum, int decimals) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(decimals);
		edit->setRange(minimum, maximum);
		edit->setSingleStep(decimals == 1 ? 0.1 : 0.05);
		edit->setValue(value);
		return edit;
	};
	auto const makeKernel = [this](int value) {
		auto* edit = new QSpinBox();
		edit->setRange(1, 25);
		edit->setSingleStep(2);
		edit->setSuffix(tr(" cells square"));
		edit->setValue(value);
		return edit;
	};
	auto const makeRow = [](const std::vector<QWidget*>& widgets) {
		auto* row = new QHBoxLayout();
		for (auto* widget : widgets)
			row->addWidget(widget);
		row->addStretch(1);
		return row;
	};
	undergrowth_edits = {{ makeEdit(vegetation.undergrowth_from, 0.0, 10.0, 1), makeEdit(vegetation.undergrowth_to, 0.0, 10.0, 1),
	                       makeEdit(vegetation.undergrowth_thresholds[0], -1.0, 1.0, 2), makeEdit(vegetation.undergrowth_thresholds[1], -1.0, 1.0, 2) }};
	vegetation_edits = {{ makeEdit(vegetation.vegetation_from, 0.0, 10.0, 1), makeEdit(vegetation.vegetation_to, 0.0, 10.0, 1),
	                      makeEdit(vegetation.vegetation_thresholds[0], -1.0, 1.0, 2), makeEdit(vegetation.vegetation_thresholds[1], -1.0, 1.0, 2),
	                      makeEdit(vegetation.vegetation_thresholds[2], -1.0, 1.0, 2) }};
	undergrowth_kernel_edit = makeKernel(vegetation.undergrowth_kernel);
	vegetation_kernel_edit = makeKernel(vegetation.vegetation_kernel);
	no_data_check = new QCheckBox(tr("Show cells without returns in red"));
	no_data_check->setChecked(vegetation.no_data_red);
	vegetation_box = new QGroupBox(tr("Vegetation base map"));
	vegetation_box->setCheckable(true);
	vegetation_box->setChecked(false);
	auto* vegetation_layout = new QFormLayout(vegetation_box);
	vegetation_layout->addRow(tr("Undergrowth from:"), makeRow({ undergrowth_edits[0], new QLabel(tr("to")), undergrowth_edits[1], new QLabel(tr("m above ground")) }));
	vegetation_layout->addRow(tr("Kernel:"), undergrowth_kernel_edit);
	vegetation_layout->addRow(tr("Thresholds:"), makeRow({ undergrowth_edits[2], undergrowth_edits[3] }));
	vegetation_layout->addRow(tr("Vegetation from:"), makeRow({ vegetation_edits[0], new QLabel(tr("to")), vegetation_edits[1], new QLabel(tr("m above ground")) }));
	vegetation_layout->addRow(tr("Kernel:"), vegetation_kernel_edit);
	vegetation_layout->addRow(tr("Thresholds:"), makeRow({ vegetation_edits[2], vegetation_edits[3], vegetation_edits[4] }));
	vegetation_layout->addRow(no_data_check);
	
	// the design notes, "Feature map, intensity and classification maps".
	PointCloudProducts const product_defaults;
	feature_edits = {{ makeEdit(product_defaults.feature_from, 0.0, 10.0, 1), makeEdit(product_defaults.feature_to, 0.0, 10.0, 1) }};
	feature_box = new QGroupBox(tr("Feature map"));
	feature_box->setCheckable(true);
	feature_box->setChecked(false);
	auto* feature_layout = new QFormLayout(feature_box);
	feature_layout->addRow(tr("Highest return from:"), makeRow({ feature_edits[0], new QLabel(tr("to")), feature_edits[1], new QLabel(tr("m above ground")) }));
	intensity_check = new QCheckBox(tr("Intensity map (first returns)"));
	intensity_check->setChecked(true);
	classification_check = new QCheckBox(tr("Classification map (colour by class)"));
	classification_check->setChecked(false);
	
	auto* layout = new QFormLayout();
	layout->addRow(tr("Files:"), files_label);
	layout->addRow(analyse_button, report_label);
	layout->addRow(tr("Cell size:"), cell_edit);
	layout->addRow(dtm_check);
	layout->addRow(dsm_check);
	layout->addRow(height_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(vegetation_box);
	main_layout->addWidget(feature_box);
	main_layout->addWidget(intensity_check);
	main_layout->addWidget(classification_check);
	auto* buttons = makeButtons(this);
	main_layout->addWidget(buttons);
	auto const update = [this, buttons]() {
		buttons->button(QDialogButtonBox::Ok)->setEnabled(dtm_check->isChecked() || dsm_check->isChecked() || height_check->isChecked() || vegetation_box->isChecked()
		                                                  || feature_box->isChecked() || intensity_check->isChecked() || classification_check->isChecked());
	};
	connect(dtm_check, &QCheckBox::toggled, this, update);
	connect(dsm_check, &QCheckBox::toggled, this, update);
	connect(height_check, &QCheckBox::toggled, this, update);
	connect(vegetation_box, &QGroupBox::toggled, this, update);
	connect(feature_box, &QGroupBox::toggled, this, update);
	connect(intensity_check, &QCheckBox::toggled, this, update);
	connect(classification_check, &QCheckBox::toggled, this, update);
}

PointCloudDialog::~PointCloudDialog() = default;

void PointCloudDialog::analyse()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	auto const info = PointCloud::analyse(paths);
	QApplication::restoreOverrideCursor();
	if (!info.error.isEmpty())
	{
		report_label->setText(info.error);
		return;
	}
	QStringList classes;
	for (int c = 0; c < int(info.per_class.size()); ++c)
	{
		if (info.per_class[std::size_t(c)] > 0)
			classes.push_back(QStringLiteral("%1: %2").arg(c).arg(info.per_class[std::size_t(c)]));
	}
	auto const spacing = info.density > 0 ? 1.0 / std::sqrt(info.density) : 0.0;
	report_label->setText(tr("%1 points\nEasting %2 to %3, northing %4 to %5\n%6 points per square metre (spacing %7 m)\nHeight %8 to %9 m\nPoints per class: %10\nSystem: %11")
	                      .arg(info.count)
	                      .arg(info.extent.left(), 0, 'f', 2).arg(info.extent.right(), 0, 'f', 2)
	                      .arg(info.extent.top(), 0, 'f', 2).arg(info.extent.bottom(), 0, 'f', 2)
	                      .arg(info.density, 0, 'f', 2).arg(spacing, 0, 'f', 2)
	                      .arg(info.z_min, 0, 'f', 2).arg(info.z_max, 0, 'f', 2)
	                      .arg(classes.join(QLatin1String(", ")),
	                           info.crs_spec.isEmpty() ? tr("none, taken as the map's") : info.crs_spec.left(60)));
}

GriddingParams PointCloudDialog::params() const
{
	GriddingParams params;
	params.cell = cell_edit->value();
	params.dtm = dtm_check->isChecked();
	params.dsm = dsm_check->isChecked();
	return params;
}

PointCloudProducts PointCloudDialog::products() const
{
	PointCloudProducts products;
	products.vegetation_height = height_check->isChecked();
	products.vegetation_map = vegetation_box->isChecked();
	auto& v = products.vegetation;
	v.undergrowth_from = undergrowth_edits[0]->value();
	v.undergrowth_to = undergrowth_edits[1]->value();
	v.undergrowth_thresholds = {{ undergrowth_edits[2]->value(), undergrowth_edits[3]->value() }};
	v.undergrowth_kernel = undergrowth_kernel_edit->value();
	v.vegetation_from = vegetation_edits[0]->value();
	v.vegetation_to = vegetation_edits[1]->value();
	v.vegetation_thresholds = {{ vegetation_edits[2]->value(), vegetation_edits[3]->value(), vegetation_edits[4]->value() }};
	v.vegetation_kernel = vegetation_kernel_edit->value();
	v.no_data_red = no_data_check->isChecked();
	products.feature_map = feature_box->isChecked();
	products.feature_from = feature_edits[0]->value();
	products.feature_to = feature_edits[1]->value();
	products.intensity_map = intensity_check->isChecked();
	products.classification_map = classification_check->isChecked();
	return products;
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


HypsometricDialog::HypsometricDialog(QWidget* parent)
: QDialog(parent)
{
	// the design notes, "Hypsometric map".
	setWindowTitle(tr("Hypsometric map"));
	HypsometricParams const defaults;
	ramp_combo = new QComboBox();
	ramp_combo->addItem(tr("Brown-yellow-green"), int(HypsometricParams::BrownYellowGreen));
	ramp_combo->addItem(tr("Brown-yellow"), int(HypsometricParams::BrownYellow));
	ramp_combo->addItem(tr("Red-yellow-green"), int(HypsometricParams::RedYellowGreen));
	ramp_combo->addItem(tr("Red-yellow-blue"), int(HypsometricParams::RedYellowBlue));
	ramp_combo->addItem(tr("Red-green-blue"), int(HypsometricParams::RedGreenBlue));
	ramp_combo->addItem(tr("Red-yellow"), int(HypsometricParams::RedYellow));
	ramp_combo->addItem(tr("Greyscale"), int(HypsometricParams::Greyscale));
	ramp_combo->addItem(tr("Greyscale interval"), int(HypsometricParams::GreyscaleInterval));
	ramp_combo->setCurrentIndex(ramp_combo->findData(int(defaults.ramp)));
	interval_edit = new QDoubleSpinBox();
	interval_edit->setDecimals(0);
	interval_edit->setRange(1.0, 10000.0);
	interval_edit->setSuffix(QStringLiteral(" m"));
	interval_edit->setValue(defaults.interval);
	auto const update = [this]() {
		interval_edit->setEnabled(ramp_combo->currentData().toInt() == int(HypsometricParams::GreyscaleInterval));
	};
	connect(ramp_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
	update();
	zero_check = new QCheckBox(tr("White at elevation 0"));
	zero_check->setChecked(defaults.white_at_zero);
	attach_check = new QCheckBox(tr("Attach as template"));
	attach_check->setChecked(true);
	
	auto* layout = new QFormLayout();
	layout->addRow(tr("Colours:"), ramp_combo);
	layout->addRow(tr("Interval:"), interval_edit);
	layout->addRow(zero_check);
	layout->addRow(attach_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(makeButtons(this));
}

HypsometricDialog::~HypsometricDialog() = default;

HypsometricParams HypsometricDialog::params() const
{
	HypsometricParams params;
	params.ramp = HypsometricParams::Ramp(ramp_combo->currentData().toInt());
	params.interval = interval_edit->value();
	params.white_at_zero = zero_check->isChecked();
	return params;
}

bool HypsometricDialog::attach() const { return attach_check->isChecked(); }


namespace {

/// The heights against the distance; the vertical scale is the horizontal one times the factor.
class ProfileChart : public QWidget
{
public:
	ProfileChart(const Profile& profile, QWidget* parent)
	: QWidget(parent)
	, profile(profile)
	{
		setMinimumSize(480, 200);
		setAutoFillBackground(true);
		QPalette palette;
		palette.setColor(QPalette::Window, Qt::white);
		setPalette(palette);
	}
	
	void setFactor(double value)
	{
		factor = value;
		update();
	}
	
protected:
	void paintEvent(QPaintEvent*) override
	{
		if (profile.samples.size() < 2 || !(profile.length > 0.0) || std::isnan(profile.minimum))
			return;
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		auto const margin = 40.0;
		auto const area = QRectF(rect()).adjusted(margin, 10.0, -10.0, -margin);
		auto const range = std::max(profile.maximum - profile.minimum, 1.0);
		// Metres per pixel along; the vertical scale exaggerates by the factor, at most what fits.
		auto const scale_x = area.width() / profile.length;
		auto const scale_y = std::min(scale_x * factor, area.height() / range);
		auto const base = profile.minimum;
		auto const point = [&](const ProfileSample& s) {
			return QPointF(area.left() + s.distance * scale_x, area.bottom() - (s.height - base) * scale_y);
		};
		painter.setPen(QPen(Qt::gray, 0));
		painter.drawLine(area.bottomLeft(), area.bottomRight());
		painter.drawLine(area.bottomLeft(), QPointF(area.left(), area.top()));
		painter.setPen(Qt::black);
		painter.drawText(QRectF(0.0, area.bottom() - 10.0, margin - 4.0, 20.0), Qt::AlignRight | Qt::AlignVCenter, QString::number(base, 'f', 0));
		painter.drawText(QRectF(area.right() - 80.0, area.bottom() + 4.0, 80.0, 20.0), Qt::AlignRight, QString::number(profile.length, 'f', 0) + QStringLiteral(" m"));
		QPainterPath line;
		auto open = false;
		for (auto const& s : profile.samples)
		{
			if (std::isnan(s.height))
			{
				open = false;
				continue;
			}
			if (open)
				line.lineTo(point(s));
			else
				line.moveTo(point(s));
			open = true;
		}
		painter.setPen(QPen(QColor(153, 102, 51), 2.0));
		painter.drawPath(line);
	}
	
private:
	const Profile& profile;
	double factor = 5.0;
};

}  // namespace



ProfileDialog::ProfileDialog(const Profile& profile, QWidget* parent)
: QDialog(parent)
, profile(profile)
{
	// the design notes, "Profile".
	setWindowTitle(tr("Profile"));
	auto* chart_widget = new ProfileChart(this->profile, this);
	chart = chart_widget;
	auto* factor_edit = new QDoubleSpinBox();
	factor_edit->setDecimals(1);
	factor_edit->setRange(1.0, 20.0);
	factor_edit->setValue(5.0);
	connect(factor_edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [chart_widget](double value) { chart_widget->setFactor(value); });
	auto const metres = [](double value) { return std::isnan(value) ? QStringLiteral("-") : QString::number(value, 'f', 1) + QStringLiteral(" m"); };
	auto* figures = new QLabel(tr("Length %1, start %2, end %3, lowest %4, highest %5, ascent %6, descent %7")
	                           .arg(metres(profile.length), metres(profile.start), metres(profile.end), metres(profile.minimum),
	                                metres(profile.maximum), metres(profile.ascent), metres(profile.descent)));
	figures->setWordWrap(true);
	auto* csv_button = new QPushButton(tr("Save CSV..."));
	auto* image_button = new QPushButton(tr("Save image..."));
	connect(csv_button, &QPushButton::clicked, this, &ProfileDialog::saveCsv);
	connect(image_button, &QPushButton::clicked, this, &ProfileDialog::saveImage);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* row = new QHBoxLayout();
	row->addWidget(new QLabel(tr("Vertical factor:")));
	row->addWidget(factor_edit);
	row->addStretch(1);
	row->addWidget(csv_button);
	row->addWidget(image_button);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(chart, 1);
	layout->addWidget(figures);
	layout->addLayout(row);
	layout->addWidget(buttons);
}

ProfileDialog::~ProfileDialog() = default;

void ProfileDialog::saveCsv()
{
	auto const path = QFileDialog::getSaveFileName(this, tr("Save CSV"), {}, tr("CSV") + QLatin1String(" (*.csv)"));
	if (path.isEmpty())
		return;
	QString error;
	if (!writeProfileCsv(profile, path, &error))
		QMessageBox::warning(this, tr("Save CSV"), error);
}

void ProfileDialog::saveImage()
{
	auto const path = QFileDialog::getSaveFileName(this, tr("Save image"), {}, tr("PNG") + QLatin1String(" (*.png)"));
	if (path.isEmpty())
		return;
	if (!chart->grab().save(path, "PNG"))
		QMessageBox::warning(this, tr("Save image"), tr("Cannot write %1").arg(path));
}



VegetationHeightDialog::VegetationHeightDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Vegetation height"));
	VegetationHeightOptions const defaults;
	coloured_radio = new QRadioButton(tr("Coloured classes"));
	grey_radio = new QRadioButton(tr("Greyscale"));
	coloured_radio->setChecked(!defaults.greyscale);
	grey_radio->setChecked(defaults.greyscale);

	class_table = new QTableWidget(0, 4);
	class_table->setHorizontalHeaderLabels({ tr("From (m)"), tr("To (m)"), tr("From colour"), tr("To colour") });
	class_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	class_table->verticalHeader()->hide();
	class_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	connect(class_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (item->column() < 2)
			return;
		QColor const colour(item->text().trimmed());
		QSignalBlocker blocker(class_table);
		item->setBackground(colour.isValid() ? QBrush(colour) : QBrush());
		item->setForeground(colour.isValid() && colour.lightness() < 128 ? QBrush(Qt::white) : QBrush(Qt::black));
	});
	auto* add_button = new QPushButton(tr("Add class"));
	auto* remove_button = new QPushButton(tr("Remove class"));
	auto* load_button = new QPushButton(tr("Load..."));
	auto* save_button = new QPushButton(tr("Save..."));
	auto* reset_button = new QPushButton(tr("Reset to default"));
	connect(add_button, &QPushButton::clicked, this, &VegetationHeightDialog::addClass);
	connect(remove_button, &QPushButton::clicked, this, &VegetationHeightDialog::removeClass);
	connect(load_button, &QPushButton::clicked, this, &VegetationHeightDialog::loadClasses);
	connect(save_button, &QPushButton::clicked, this, &VegetationHeightDialog::saveClasses);
	connect(reset_button, &QPushButton::clicked, this, [this]() { setClasses(defaultHeightClasses()); });
	auto* class_buttons = new QHBoxLayout();
	for (auto* button : { add_button, remove_button, load_button, save_button, reset_button })
	{
		class_buttons->addWidget(button);
		class_widgets.push_back(button);
	}
	class_widgets.push_back(class_table);

	mapping_combo = new QComboBox();
	mapping_combo->addItem(tr("Linear"), int(VegetationHeightOptions::Linear));
	mapping_combo->addItem(tr("Quadratic negative"), int(VegetationHeightOptions::QuadraticNegative));
	mapping_combo->addItem(tr("Quadratic positive"), int(VegetationHeightOptions::QuadraticPositive));
	mapping_combo->setCurrentIndex(mapping_combo->findData(int(defaults.mapping)));
	max_height_edit = new QDoubleSpinBox();
	max_height_edit->setDecimals(1);
	max_height_edit->setRange(0.5, 200.0);
	max_height_edit->setSuffix(QStringLiteral(" m"));
	max_height_edit->setValue(defaults.max_height);
	auto* grey_layout = new QFormLayout();
	grey_layout->addRow(tr("Mapping:"), mapping_combo);
	grey_layout->addRow(tr("Black at:"), max_height_edit);

	attach_check = new QCheckBox(tr("Attach as template"));
	attach_check->setChecked(true);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(coloured_radio);
	layout->addWidget(class_table);
	layout->addLayout(class_buttons);
	layout->addWidget(grey_radio);
	layout->addLayout(grey_layout);
	layout->addWidget(attach_check);
	layout->addWidget(makeButtons(this));

	setClasses(defaults.classes);
	connect(coloured_radio, &QRadioButton::toggled, this, &VegetationHeightDialog::updateEnabled);
	updateEnabled();
}

VegetationHeightDialog::~VegetationHeightDialog() = default;

void VegetationHeightDialog::updateEnabled()
{
	auto const coloured = coloured_radio->isChecked();
	for (auto* widget : class_widgets)
		widget->setEnabled(coloured);
	mapping_combo->setEnabled(!coloured);
	max_height_edit->setEnabled(!coloured);
}

void VegetationHeightDialog::setClasses(const std::vector<HeightClass>& classes)
{
	class_table->setRowCount(0);
	for (auto const& c : classes)
	{
		auto const row = class_table->rowCount();
		class_table->insertRow(row);
		class_table->setItem(row, 0, new QTableWidgetItem(QString::number(c.from, 'g', 6)));
		class_table->setItem(row, 1, new QTableWidgetItem(QString::number(c.to, 'g', 6)));
		class_table->setItem(row, 2, new QTableWidgetItem(QColor(c.from_color).name()));
		class_table->setItem(row, 3, new QTableWidgetItem(QColor(c.to_color).name()));
	}
}

bool VegetationHeightDialog::readClasses(std::vector<HeightClass>& classes, QString* error) const
{
	std::vector<HeightClass> result;
	for (int row = 0; row < class_table->rowCount(); ++row)
	{
		auto const text = [this, row](int col) {
			auto const* item = class_table->item(row, col);
			return item ? item->text().trimmed() : QString{};
		};
		bool ok_from = false, ok_to = false;
		HeightClass c;
		c.from = QLocale().toDouble(text(0), &ok_from);
		if (!ok_from)
			c.from = text(0).toDouble(&ok_from);
		c.to = QLocale().toDouble(text(1), &ok_to);
		if (!ok_to)
			c.to = text(1).toDouble(&ok_to);
		QColor const from_colour(text(2));
		QColor const to_colour(text(3));
		if (!ok_from || !ok_to || !from_colour.isValid() || !to_colour.isValid())
		{
			*error = tr("Row %1 needs two heights and two colours such as #ffd700.").arg(row + 1);
			return false;
		}
		c.from_color = from_colour.rgb();
		c.to_color = to_colour.rgb();
		if (!(c.from < c.to) || (!result.empty() && c.from < result.back().to))
		{
			*error = tr("Row %1: each class must begin below its end and not below the end of the one before.").arg(row + 1);
			return false;
		}
		result.push_back(c);
	}
	if (result.empty())
	{
		*error = tr("Add at least one class.");
		return false;
	}
	classes = std::move(result);
	return true;
}

void VegetationHeightDialog::addClass()
{
	std::vector<HeightClass> classes;
	QString error;
	auto const from = readClasses(classes, &error) ? classes.back().to : 0.0;
	auto const row = class_table->rowCount();
	class_table->insertRow(row);
	class_table->setItem(row, 0, new QTableWidgetItem(QString::number(from, 'g', 6)));
	class_table->setItem(row, 1, new QTableWidgetItem(QString::number(from + 10.0, 'g', 6)));
	class_table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("#ffffff")));
	class_table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("#ffffff")));
}

void VegetationHeightDialog::removeClass()
{
	auto const row = class_table->currentRow();
	if (row >= 0)
		class_table->removeRow(row);
}

void VegetationHeightDialog::loadClasses()
{
	auto const path = QFileDialog::getOpenFileName(this, tr("Load classes"), {}, tr("Height classes") + QLatin1String(" (*.txt);;") + tr("All files") + QLatin1String(" (*.*)"));
	if (path.isEmpty())
		return;
	std::vector<HeightClass> classes;
	QString error;
	if (!loadHeightClasses(path, classes, &error))
	{
		QMessageBox::warning(this, tr("Load classes"), error);
		return;
	}
	setClasses(classes);
}

void VegetationHeightDialog::saveClasses()
{
	std::vector<HeightClass> classes;
	QString error;
	if (!readClasses(classes, &error))
	{
		QMessageBox::warning(this, tr("Save classes"), error);
		return;
	}
	auto const path = QFileDialog::getSaveFileName(this, tr("Save classes"), {}, tr("Height classes") + QLatin1String(" (*.txt)"));
	if (path.isEmpty())
		return;
	if (!saveHeightClasses(path, classes, &error))
		QMessageBox::warning(this, tr("Save classes"), error);
}

void VegetationHeightDialog::done(int result)
{
	if (result == QDialog::Accepted && coloured_radio->isChecked())
	{
		std::vector<HeightClass> classes;
		QString error;
		if (!readClasses(classes, &error))
		{
			QMessageBox::warning(this, windowTitle(), error);
			return;
		}
	}
	QDialog::done(result);
}

VegetationHeightOptions VegetationHeightDialog::options() const
{
	VegetationHeightOptions options;
	options.greyscale = grey_radio->isChecked();
	options.mapping = VegetationHeightOptions::GreyMapping(mapping_combo->currentData().toInt());
	options.max_height = max_height_edit->value();
	QString error;
	readClasses(options.classes, &error);   // keeps the defaults when the table is not valid
	return options;
}

bool VegetationHeightDialog::attach() const
{
	return attach_check->isChecked();
}


}  // namespace OpenOrienteering
