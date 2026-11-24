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

#include "elevation_wizard_dialog.h"
#include "gui/map/elevation_dialogs.h"

#include <functional>
#include <initializer_list>
#include <utility>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWizardPage>

#include "elevation/point_cloud.h"
#include "elevation/raster_products.h"

namespace OpenOrienteering {

/**
 * A wizard page whose initialization and validation are the wizard's.
 */
class ElevationWizardDialog::Page_ : public QWizardPage
{
public:
	Page_(const QString& title, const QString& subtitle, QWidget* content, const ElevationWizardDialog& wizard)
	: wizard(wizard)
	{
		setTitle(title);
		setSubTitle(subtitle);
		auto* layout = new QVBoxLayout(this);
		layout->addWidget(content);
		layout->addStretch();
	}

	void initializePage() override
	{
		if (init)
			init();
	}

	bool validatePage() override
	{
		return !validate || validate();
	}

	int nextId() const override
	{
		return wizard.nextId();
	}

	std::function<void ()> init;
	std::function<bool ()> validate;

private:
	const ElevationWizardDialog& wizard;
};


namespace {

QComboBox* intervalCombo(std::initializer_list<double> values, double current)
{
	auto* combo = new QComboBox();
	for (auto value : values)
	{
		combo->addItem(QString::number(value), value);
		if (value == current)
			combo->setCurrentIndex(combo->count() - 1);
	}
	return combo;
}

double comboValue(const QComboBox* combo)
{
	return combo->currentData().toDouble();
}

QDoubleSpinBox* spin(double minimum, double maximum, int decimals, double value, const QString& suffix = {})
{
	auto* edit = new QDoubleSpinBox();
	edit->setRange(minimum, maximum);
	edit->setDecimals(decimals);
	edit->setValue(value);
	if (!suffix.isEmpty())
		edit->setSuffix(suffix);
	return edit;
}

QSpinBox* intSpin(int minimum, int maximum, int value, const QString& suffix = {})
{
	auto* edit = new QSpinBox();
	edit->setRange(minimum, maximum);
	edit->setValue(value);
	if (!suffix.isEmpty())
		edit->setSuffix(suffix);
	return edit;
}

template <class T>
T* named(T* widget, const char* name)
{
	widget->setObjectName(QString::fromLatin1(name));
	return widget;
}

}  // namespace



ElevationWizardDialog::ElevationWizardDialog(const QString& map_path, QWidget* parent)
: QWizard(parent)
, map_path(map_path)
{
	// the design notes, "Wizard".
	setWindowTitle(tr("Elevation wizard"));
	setOption(QWizard::NoBackButtonOnStartPage, true);

	auto add = [this](int id, const QString& title, const QString& subtitle, QWidget* content) {
		auto* page = new Page_(title, subtitle, content, *this);
		setPage(id, page);
		return page;
	};
	auto* source = add(SourcePage, tr("Source"), tr("Elevation grids or point clouds."), makeSourcePage());
	source->validate = [this]() { return validateSource(); };
	auto* settings = add(SettingsPage, tr("Settings"), tr("The units of the grids, or the cell size and the models of the point clouds."), makeSettingsPage());
	settings->init = [this]() { initializeSettings(); };
	settings->validate = [this]() { return validateSettings(); };
	auto* products = add(ProductsPage, tr("Products"), tr("What the wizard makes; a page follows for each product's settings."), makeProductsPage());
	products->init = [this]() { initializeProducts(); };
	add(ContoursPage, tr("Contours"), tr("The contour sets, as Elevation > Contours."), makeContoursPage());
	add(HillShadePage, tr("Hill shading"), tr("As Elevation > Hill shading."), makeHillShadePage());
	add(SlopePage, tr("Slope gradient"), tr("As Elevation > Slope gradient; cliffs from the black pixels, as Elevation > Extract features."), makeSlopePage());
	add(HypsometricPage, tr("Hypsometric map"), tr("As Elevation > Hypsometric map."), makeHypsometricPage());
	auto* features = add(FeaturesPage, tr("Extract features"), tr("As Elevation > Extract features."), makeFeaturesPage());
	features->init = [this]() { initializeFeatures(); };
	add(CloudProductsPage, tr("Point-cloud products"), tr("The vegetation height, the vegetation base map and the feature map."), makeCloudProductsPage());
	auto* output = add(OutputPage, tr("Output"), tr("Finish writes the rasters and adds the objects to the current part in one undo step."), makeOutputPage());
	output->init = [this]() { initializeOutput(); };
	setStartId(SourcePage);
}

ElevationWizardDialog::~ElevationWizardDialog() = default;



QWidget* ElevationWizardDialog::makeSourcePage()
{
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);
	grid_radio = named(new QRadioButton(tr("Elevation grid (GeoTIFF, ESRI ASCII grid, XYZ, SRTM HGT, OCD DEM...); several are merged")), "grid_radio");
	cloud_radio = named(new QRadioButton(tr("Point cloud (LAS, LAZ)")), "cloud_radio");
	grid_radio->setChecked(true);
	cloud_radio->setEnabled(PointCloud::supported());
	auto* group = new QButtonGroup(widget);
	group->addButton(grid_radio);
	group->addButton(cloud_radio);
	connect(grid_radio, &QRadioButton::toggled, this, [this]() { file_list->clear(); updateEnabled(); });
	layout->addWidget(grid_radio);
	layout->addWidget(cloud_radio);
	file_list = named(new QListWidget(), "file_list");
	file_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	layout->addWidget(file_list);
	auto* buttons = new QHBoxLayout();
	auto* add_button = named(new QPushButton(tr("Add files...")), "add_button");
	remove_button = named(new QPushButton(tr("Remove")), "remove_button");
	connect(add_button, &QPushButton::clicked, this, &ElevationWizardDialog::addFiles);
	connect(remove_button, &QPushButton::clicked, this, &ElevationWizardDialog::removeFiles);
	buttons->addWidget(add_button);
	buttons->addWidget(remove_button);
	buttons->addStretch();
	layout->addLayout(buttons);
	return widget;
}

QWidget* ElevationWizardDialog::makeSettingsPage()
{
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);

	grid_box = new QGroupBox(tr("Units"));
	auto* grid_layout = new QVBoxLayout(grid_box);
	feet_check = named(new QCheckBox(tr("Heights in feet: convert to metres")), "feet_check");
	millimetres_check = named(new QCheckBox(tr("Heights in millimetres: convert to metres")), "millimetres_check");
	shift_check = named(new QCheckBox(tr("Shift values below sea level")), "shift_check");
	grid_layout->addWidget(feet_check);
	grid_layout->addWidget(millimetres_check);
	grid_layout->addWidget(shift_check);
	layout->addWidget(grid_box);

	GriddingParams const gridding;
	cloud_box = new QGroupBox(tr("Gridding"));
	auto* cloud_layout = new QFormLayout(cloud_box);
	auto* analyse_button = named(new QPushButton(tr("Analyse")), "analyse_button");
	connect(analyse_button, &QPushButton::clicked, this, &ElevationWizardDialog::initializeSettings);
	cloud_layout->addRow(analyse_button);
	cell_edit = named(spin(0.1, 100.0, 2, gridding.cell, tr(" m")), "cell_edit");
	cloud_layout->addRow(tr("Cell size:"), cell_edit);
	dtm_check = named(new QCheckBox(tr("Terrain model (DTM), from:")), "dtm_check");
	dtm_check->setChecked(gridding.dtm);
	dtm_points = named(new PointChoiceWidget(gridding.dtm_points), "dtm_points");
	dsm_check = named(new QCheckBox(tr("Surface model (DSM), from:")), "dsm_check");
	dsm_check->setChecked(gridding.dsm);
	dsm_points = named(new PointChoiceWidget(gridding.dsm_points), "dsm_points");
	connect(dtm_check, &QCheckBox::toggled, dtm_points, &QWidget::setEnabled);
	connect(dsm_check, &QCheckBox::toggled, dsm_points, &QWidget::setEnabled);
	cloud_layout->addRow(dtm_check);
	cloud_layout->addRow(dtm_points);
	cloud_layout->addRow(dsm_check);
	cloud_layout->addRow(dsm_points);
	layout->addWidget(cloud_box);

	report_label = named(new QLabel(), "report_label");
	report_label->setWordWrap(true);
	report_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(report_label);
	return widget;
}

QWidget* ElevationWizardDialog::makeProductsPage()
{
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);
	auto check = [layout](const QString& text, const char* name) {
		auto* box = named(new QCheckBox(text), name);
		layout->addWidget(box);
		return box;
	};
	contours_check = check(tr("Contours"), "contours_check");
	hillshade_check = check(tr("Hill shading"), "hillshade_check");
	slope_check = check(tr("Slope gradient"), "slope_check");
	hypsometric_check = check(tr("Hypsometric map"), "hypsometric_check");
	features_check = check(tr("Extract features: knolls, small depressions, trees"), "features_check");
	vegetation_height_check = check(tr("Vegetation height"), "vegetation_height_check");
	vegetation_map_check = check(tr("Vegetation base map"), "vegetation_map_check");
	feature_map_check = check(tr("Feature map"), "feature_map_check");
	intensity_check = check(tr("Intensity map"), "intensity_check");
	classification_check = check(tr("Classification map"), "classification_check");
	return widget;
}

ElevationWizardDialog::ContourSet ElevationWizardDialog::makeContourSet(const QString& title, bool is_smoothed)
{
	// The lists and defaults of the contour dialog, the design notes, "Contours".
	ContourSet set;
	set.group = named(new QGroupBox(title), is_smoothed ? "smoothed_group" : "unsmoothed_group");
	set.group->setCheckable(true);
	set.group->setChecked(!is_smoothed);
	set.interval = intervalCombo({ 0.1, 0.25, 0.5, 1.0, 1.25, 2.0, 2.5, 3.0, 4.0, 5.0 }, is_smoothed ? 2.5 : 1.0);
	set.main_interval = intervalCombo({ 1.0, 2.0, 2.5, 5.0, 10.0, 20.0, 25.0 }, 5.0);
	set.index_interval = intervalCombo({ 5.0, 10.0, 12.5, 25.0, 50.0, 100.0, 200.0 }, 25.0);
	set.depression_check = new QCheckBox(tr("Separate depression symbols"));
	set.curves_check = new QCheckBox(tr("Convert to curves"));
	set.curves_check->setChecked(is_smoothed);
	auto* layout = new QFormLayout(set.group);
	layout->addRow(is_smoothed ? tr("Form line interval:") : tr("Smallest interval:"), set.interval);
	layout->addRow(tr("Main interval:"), set.main_interval);
	layout->addRow(tr("Index interval:"), set.index_interval);
	layout->addRow(set.depression_check);
	if (is_smoothed)
		layout->addRow(set.curves_check);
	else
		set.curves_check->setVisible(false);
	return set;
}

QWidget* ElevationWizardDialog::makeContoursPage()
{
	auto* widget = new QWidget();
	auto* layout = new QHBoxLayout(widget);
	unsmoothed = makeContourSet(tr("Unsmoothed set"), false);
	smoothed = makeContourSet(tr("Smoothed set"), true);
	layout->addWidget(unsmoothed.group);
	layout->addWidget(smoothed.group);
	return widget;
}

QWidget* ElevationWizardDialog::makeHillShadePage()
{
	HillShadeParams const defaults;
	auto* widget = new QWidget();
	auto* layout = new QFormLayout(widget);
	method_combo = named(new QComboBox(), "method_combo");
	method_combo->addItem(tr("Slope shading"), int(HillShadeParams::SlopeShading));
	method_combo->addItem(tr("Oblique light"), int(HillShadeParams::Oblique));
	method_combo->addItem(tr("Slope shading and oblique light"), int(HillShadeParams::Combined));
	method_combo->setCurrentIndex(method_combo->findData(int(defaults.method)));
	layout->addRow(tr("Method:"), method_combo);
	multi_check = named(new QCheckBox(tr("Multi-directional")), "multi_check");
	multi_check->setChecked(defaults.multi_directional);
	layout->addRow(multi_check);
	azimuth_edit = spin(0.0, 360.0, 1, defaults.azimuth_deg, tr(" °"));
	layout->addRow(tr("Azimuth:"), azimuth_edit);
	elevation_edit = spin(1.0, 90.0, 1, defaults.elevation_deg, tr(" °"));
	layout->addRow(tr("Sun elevation:"), elevation_edit);
	exaggeration_edit = spin(1.0, 10.0, 1, defaults.exaggeration);
	layout->addRow(tr("Exaggeration:"), exaggeration_edit);
	hill_cell_edit = spin(0.0, 1000.0, 2, defaults.cell_size, tr(" m"));
	hill_cell_edit->setSpecialValueText(tr("The grid's"));
	layout->addRow(tr("Cell size:"), hill_cell_edit);
	// foom: as Elevation > Hill shading (seen missing on 2026-11-22)
	bicubic_check = named(new QCheckBox(tr("Bicubic resampling")), "bicubic_check");
	bicubic_check->setChecked(defaults.resampling == HillShadeParams::Bicubic);
	layout->addRow(bicubic_check);
	hill_corners_check = named(new QCheckBox(tr("Gradient on cell corners (legacy mode)")), "hill_corners_check");
	hill_corners_check->setChecked(defaults.gradient == GradientMethod::Corners);
	layout->addRow(hill_corners_check);
	return widget;
}

QWidget* ElevationWizardDialog::makeSlopePage()
{
	SlopeParams const defaults;
	LandformParams const landforms;
	auto* widget = new QWidget();
	auto* layout = new QFormLayout(widget);
	continuous_radio = named(new QRadioButton(tr("Continuous")), "continuous_radio");
	binary_radio = named(new QRadioButton(tr("Black and white")), "binary_radio");
	(defaults.binary ? binary_radio : continuous_radio)->setChecked(true);
	auto* group = new QButtonGroup(widget);
	group->addButton(continuous_radio);
	group->addButton(binary_radio);
	layout->addRow(continuous_radio);
	layout->addRow(binary_radio);
	threshold_edit = spin(1.0, 89.0, 1, defaults.threshold_deg, tr(" °"));
	layout->addRow(tr("Threshold:"), threshold_edit);
	slope_cell_edit = spin(0.0, 1000.0, 2, defaults.cell_size, tr(" m"));
	slope_cell_edit->setSpecialValueText(tr("The grid's"));
	layout->addRow(tr("Cell size:"), slope_cell_edit);
	// foom: as Elevation > Slope gradient
	slope_corners_check = named(new QCheckBox(tr("Gradient on cell corners (legacy mode)")), "slope_corners_check");
	slope_corners_check->setChecked(defaults.gradient == GradientMethod::Corners);
	layout->addRow(slope_corners_check);
	cliffs_check = named(new QCheckBox(tr("Extract cliffs from black pixels")), "cliffs_check");
	cliffs_check->setChecked(landforms.cliffs);
	layout->addRow(cliffs_check);
	area_edit = intSpin(1, 1000, landforms.cliff_min_area, tr(" pixels"));
	layout->addRow(tr("Minimum area:"), area_edit);
	length_edit = intSpin(1, 1000, landforms.cliff_min_length, tr(" pixels"));
	layout->addRow(tr("Minimum length:"), length_edit);
	connect(cliffs_check, &QCheckBox::toggled, this, &ElevationWizardDialog::updateEnabled);
	return widget;
}

QWidget* ElevationWizardDialog::makeHypsometricPage()
{
	HypsometricParams const defaults;
	auto* widget = new QWidget();
	auto* layout = new QFormLayout(widget);
	ramp_combo = named(new QComboBox(), "ramp_combo");
	// The order of the hypsometric dialog.
	for (auto const& item : std::initializer_list<std::pair<HypsometricParams::Ramp, QString>> {
	         { HypsometricParams::BrownYellowGreen, tr("Brown-yellow-green") },
	         { HypsometricParams::Greyscale, tr("Greyscale") },
	         { HypsometricParams::RedGreenBlue, tr("Red-green-blue") },
	         { HypsometricParams::RedYellowGreen, tr("Red-yellow-green") },
	         { HypsometricParams::RedYellowBlue, tr("Red-yellow-blue") },
	         { HypsometricParams::BrownYellow, tr("Brown-yellow") },
	         { HypsometricParams::RedYellow, tr("Red-yellow") },
	         { HypsometricParams::GreyscaleInterval, tr("Greyscale interval") } })
		ramp_combo->addItem(item.second, int(item.first));
	ramp_combo->setCurrentIndex(ramp_combo->findData(int(defaults.ramp)));
	layout->addRow(tr("Ramp:"), ramp_combo);
	interval_edit = spin(1.0, 10000.0, 1, defaults.interval, tr(" m"));
	layout->addRow(tr("Interval:"), interval_edit);
	zero_check = new QCheckBox(tr("White at elevation 0"));
	zero_check->setChecked(defaults.white_at_zero);
	layout->addRow(zero_check);
	connect(ramp_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ElevationWizardDialog::updateEnabled);
	return widget;
}

QWidget* ElevationWizardDialog::makeFeaturesPage()
{
	LandformParams const defaults;
	auto* widget = new QWidget();
	auto* layout = new QFormLayout(widget);
	knolls_check = named(new QCheckBox(tr("Knolls (symbol 109)")), "knolls_check");
	knolls_check->setChecked(defaults.knolls);
	knoll_height_edit = spin(0.1, 100.0, 1, defaults.min_height, tr(" m"));
	layout->addRow(knolls_check);
	layout->addRow(tr("Minimum height:"), knoll_height_edit);
	depressions_check = named(new QCheckBox(tr("Small depressions (symbol 111)")), "depressions_check");
	depressions_check->setChecked(defaults.depressions);
	depression_depth_edit = spin(0.1, 100.0, 1, defaults.min_depth, tr(" m"));
	layout->addRow(depressions_check);
	layout->addRow(tr("Minimum depth:"), depression_depth_edit);
	trees_check = named(new QCheckBox(tr("Trees (symbol 418), from the surface model")), "trees_check");
	trees_check->setChecked(defaults.trees);
	tree_height_edit = intSpin(1, 100, int(defaults.tree_min_height), tr(" m"));
	layout->addRow(trees_check);
	layout->addRow(tr("Minimum tree height:"), tree_height_edit);
	for (auto* box : { knolls_check, depressions_check, trees_check })
		connect(box, &QCheckBox::toggled, this, &ElevationWizardDialog::updateEnabled);
	return widget;
}

QWidget* ElevationWizardDialog::makeCloudProductsPage()
{
	VegetationHeightOptions const height;
	PointCloudProducts const products;
	auto const& vegetation = products.vegetation;
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);

	auto* height_box = named(new QGroupBox(tr("Vegetation height")), "height_box");
	auto* height_layout = new QFormLayout(height_box);
	coloured_radio = named(new QRadioButton(tr("Coloured classes (four)")), "coloured_radio");
	grey_radio = named(new QRadioButton(tr("Greyscale")), "grey_radio");
	(height.greyscale ? grey_radio : coloured_radio)->setChecked(true);
	auto* group = new QButtonGroup(widget);
	group->addButton(coloured_radio);
	group->addButton(grey_radio);
	height_layout->addRow(coloured_radio);
	height_layout->addRow(grey_radio);
	mapping_combo = new QComboBox();
	mapping_combo->addItem(tr("Linear"), int(VegetationHeightOptions::Linear));
	mapping_combo->addItem(tr("Quadratic negative"), int(VegetationHeightOptions::QuadraticNegative));
	mapping_combo->addItem(tr("Quadratic positive"), int(VegetationHeightOptions::QuadraticPositive));
	mapping_combo->setCurrentIndex(mapping_combo->findData(int(height.mapping)));
	height_layout->addRow(tr("Grey mapping:"), mapping_combo);
	max_height_edit = spin(0.1, 1000.0, 1, height.max_height, tr(" m"));
	height_layout->addRow(tr("Black at:"), max_height_edit);
	connect(grey_radio, &QRadioButton::toggled, this, &ElevationWizardDialog::updateEnabled);
	layout->addWidget(height_box);

	auto* base_box = named(new QGroupBox(tr("Vegetation base map")), "base_box");
	auto* base_layout = new QFormLayout(base_box);
	undergrowth_edits = {{ spin(0.0, 100.0, 2, vegetation.undergrowth_from, tr(" m")), spin(0.0, 100.0, 2, vegetation.undergrowth_to, tr(" m")),
	                       spin(-1.0, 1.0, 2, vegetation.undergrowth_thresholds[0]), spin(-1.0, 1.0, 2, vegetation.undergrowth_thresholds[1]) }};
	vegetation_edits = {{ spin(0.0, 100.0, 2, vegetation.vegetation_from, tr(" m")), spin(0.0, 100.0, 2, vegetation.vegetation_to, tr(" m")),
	                      spin(-1.0, 1.0, 2, vegetation.vegetation_thresholds[0]), spin(-1.0, 1.0, 2, vegetation.vegetation_thresholds[1]),
	                      spin(-1.0, 1.0, 2, vegetation.vegetation_thresholds[2]) }};
	undergrowth_kernel_edit = intSpin(1, 51, vegetation.undergrowth_kernel, tr(" cells"));
	vegetation_kernel_edit = intSpin(1, 51, vegetation.vegetation_kernel, tr(" cells"));
	auto row = [](std::initializer_list<QWidget*> widgets) {
		auto* layout = new QHBoxLayout();
		for (auto* w : widgets)
			layout->addWidget(w);
		return layout;
	};
	base_layout->addRow(tr("Undergrowth from, to:"), row({ undergrowth_edits[0], undergrowth_edits[1] }));
	base_layout->addRow(tr("Undergrowth kernel:"), undergrowth_kernel_edit);
	base_layout->addRow(tr("Undergrowth thresholds:"), row({ undergrowth_edits[2], undergrowth_edits[3] }));
	base_layout->addRow(tr("Vegetation from, to:"), row({ vegetation_edits[0], vegetation_edits[1] }));
	base_layout->addRow(tr("Vegetation kernel:"), vegetation_kernel_edit);
	base_layout->addRow(tr("Vegetation thresholds:"), row({ vegetation_edits[2], vegetation_edits[3], vegetation_edits[4] }));
	no_data_check = new QCheckBox(tr("Show area with no data in red"));
	no_data_check->setChecked(vegetation.no_data_red);
	base_layout->addRow(no_data_check);
	layout->addWidget(base_box);

	auto* feature_box = named(new QGroupBox(tr("Feature map")), "feature_box");
	auto* feature_layout = new QFormLayout(feature_box);
	feature_edits = {{ spin(0.0, 100.0, 2, products.feature_from, tr(" m")), spin(0.0, 100.0, 2, products.feature_to, tr(" m")) }};
	feature_layout->addRow(tr("Height band from, to:"), row({ feature_edits[0], feature_edits[1] }));
	layout->addWidget(feature_box);
	return widget;
}

QWidget* ElevationWizardDialog::makeOutputPage()
{
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);
	folder_label = named(new QLabel(), "folder_label");
	folder_label->setWordWrap(true);
	layout->addWidget(folder_label);
	attach_check = named(new QCheckBox(tr("Attach rasters as templates")), "attach_check");
	attach_check->setChecked(true);
	layout->addWidget(attach_check);
	summary_label = named(new QLabel(), "summary_label");
	summary_label->setWordWrap(true);
	layout->addWidget(summary_label);
	return widget;
}



void ElevationWizardDialog::setSource(ElevationWizardJob::Source source)
{
	(source == ElevationWizardJob::PointCloud ? cloud_radio : grid_radio)->setChecked(true);
}

ElevationWizardJob::Source ElevationWizardDialog::source() const
{
	return cloud_radio->isChecked() ? ElevationWizardJob::PointCloud : ElevationWizardJob::Grid;
}

void ElevationWizardDialog::setFiles(const QStringList& files)
{
	file_list->clear();
	file_list->addItems(files);
	updateEnabled();
}

QStringList ElevationWizardDialog::files() const
{
	QStringList result;
	for (int i = 0; i < file_list->count(); ++i)
		result.push_back(file_list->item(i)->text());
	return result;
}

void ElevationWizardDialog::addFiles()
{
	auto const start_dir = map_path.isEmpty() ? QString{} : QFileInfo(map_path).absolutePath();
	QString filter;
	if (source() == ElevationWizardJob::PointCloud)
		filter = tr("Point clouds") + QLatin1String(" (*.las *.laz);;");
	else
		filter = tr("Elevation grids") + QLatin1String(" (*.tif *.tiff *.asc *.xyz *.hgt *.ocdDem *.img *.vrt *.dem);;");
	filter += tr("All files") + QLatin1String(" (*.*)");
	auto const paths = QFileDialog::getOpenFileNames(this, tr("Elevation wizard"), start_dir, filter);
	auto current = files();
	for (auto const& path : paths)
	{
		if (!current.contains(path))
			current.push_back(path);
	}
	setFiles(current);
}

void ElevationWizardDialog::removeFiles()
{
	qDeleteAll(file_list->selectedItems());
	updateEnabled();
}

bool ElevationWizardDialog::validateSource()
{
	return file_list->count() > 0;
}

void ElevationWizardDialog::initializeSettings()
{
	auto const cloud = source() == ElevationWizardJob::PointCloud;
	grid_box->setVisible(!cloud);
	cloud_box->setVisible(cloud);
	settings_ok = false;
	auto const paths = files();
	if (paths.isEmpty())
		return;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (cloud)
	{
		auto const info = PointCloud::analyse(paths);
		if (info.error.isEmpty())
		{
			settings_ok = true;
			report_label->setText(tr("%1 points\nEasting %2 to %3, northing %4 to %5\n%6 points per square metre\nHeight %7 to %8 m\nSystem: %9")
			                      .arg(info.count)
			                      .arg(info.extent.left(), 0, 'f', 2).arg(info.extent.right(), 0, 'f', 2)
			                      .arg(info.extent.top(), 0, 'f', 2).arg(info.extent.bottom(), 0, 'f', 2)
			                      .arg(info.density, 0, 'f', 2)
			                      .arg(info.z_min, 0, 'f', 2).arg(info.z_max, 0, 'f', 2)
			                      .arg(info.crs_spec.isEmpty() ? tr("none, taken as the map's") : info.crs_spec.left(60)));
		}
		else
		{
			report_label->setText(info.error);
		}
	}
	else
	{
		auto const grid = ElevationModel::open(paths.front());
		if (grid)
		{
			settings_ok = true;
			auto const info = grid->info();
			auto const extent = grid->extent();
			auto text = tr("%1: %2 by %3 cells of %4 m\nEasting %5 to %6, northing %7 to %8\nHeights %9 to %10 m")
			            .arg(QFileInfo(paths.front()).fileName())
			            .arg(grid->width()).arg(grid->height()).arg(grid->cellSize())
			            .arg(extent.left(), 0, 'f', 1).arg(extent.right(), 0, 'f', 1)
			            .arg(extent.top(), 0, 'f', 1).arg(extent.bottom(), 0, 'f', 1)
			            .arg(info.minimum, 0, 'f', 2).arg(info.maximum, 0, 'f', 2);
			if (paths.size() > 1)
				text += QLatin1Char('\n') + tr("%n more grid(s) will be merged after it.", nullptr, paths.size() - 1);
			report_label->setText(text);
		}
		else
		{
			report_label->setText(ElevationModel::lastError());
		}
	}
	QApplication::restoreOverrideCursor();
}

bool ElevationWizardDialog::validateSettings() const
{
	return settings_ok;
}

void ElevationWizardDialog::initializeProducts()
{
	auto const current = int(source());
	if (products_source != current)
	{
		// The ticks for the source: the design notes, "Wizard".
		products_source = current;
		auto const job = ElevationWizardJob::defaults(source());
		contours_check->setChecked(job.contours);
		hillshade_check->setChecked(job.hill_shading);
		slope_check->setChecked(job.slope);
		hypsometric_check->setChecked(job.hypsometric);
		features_check->setChecked(job.features);
		vegetation_height_check->setChecked(job.vegetation_height);
		vegetation_map_check->setChecked(job.cloud.vegetation_map);
		feature_map_check->setChecked(job.cloud.feature_map);
		intensity_check->setChecked(job.cloud.intensity_map);
		classification_check->setChecked(job.cloud.classification_map);
	}
	updateEnabled();
}

void ElevationWizardDialog::initializeFeatures()
{
	updateEnabled();
}

void ElevationWizardDialog::initializeOutput()
{
	auto const paths = files();
	auto const base = map_path.isEmpty() ? (paths.isEmpty() ? QString{} : paths.front()) : map_path;
	folder_label->setText(tr("The files are written to %1.").arg(QDir::toNativeSeparators(QFileInfo(base).absolutePath())));
	auto const job = this->job();
	QStringList items;
	if (job.contours)
		items.push_back(tr("%n contour set(s)", nullptr, int(job.contour_sets.size())));
	if (job.hill_shading)
		items.push_back(tr("hill shading"));
	if (job.slope)
		items.push_back(tr("slope gradient"));
	if (job.hypsometric)
		items.push_back(tr("hypsometric map"));
	auto const landforms = job.effectiveLandforms();
	if (landforms.knolls || landforms.depressions || landforms.trees)
		items.push_back(tr("features"));
	if (landforms.cliffs)
		items.push_back(tr("cliffs"));
	if (job.vegetation_height)
		items.push_back(tr("vegetation height"));
	if (job.cloud.vegetation_map)
		items.push_back(tr("vegetation base map"));
	if (job.cloud.feature_map)
		items.push_back(tr("feature map"));
	if (job.cloud.intensity_map)
		items.push_back(tr("intensity map"));
	if (job.cloud.classification_map)
		items.push_back(tr("classification map"));
	summary_label->setText(items.isEmpty() ? tr("Only the elevation data are imported.")
	                                       : tr("Made: %1.").arg(items.join(QLatin1String(", "))));
}

void ElevationWizardDialog::updateEnabled()
{
	auto const cloud = source() == ElevationWizardJob::PointCloud;
	remove_button->setEnabled(file_list->count() > 0);
	for (auto* box : { vegetation_height_check, vegetation_map_check, feature_map_check, intensity_check, classification_check })
		box->setEnabled(cloud);
	trees_check->setEnabled(cloud);
	tree_height_edit->setEnabled(cloud && trees_check->isChecked());
	knoll_height_edit->setEnabled(knolls_check->isChecked());
	depression_depth_edit->setEnabled(depressions_check->isChecked());
	area_edit->setEnabled(cliffs_check->isChecked());
	length_edit->setEnabled(cliffs_check->isChecked());
	interval_edit->setEnabled(ramp_combo->currentData().toInt() == int(HypsometricParams::GreyscaleInterval));
	mapping_combo->setEnabled(grey_radio->isChecked());
	max_height_edit->setEnabled(grey_radio->isChecked());
	if (auto* box = findChild<QGroupBox*>(QStringLiteral("height_box")))
		box->setVisible(vegetation_height_check->isChecked());
	if (auto* box = findChild<QGroupBox*>(QStringLiteral("base_box")))
		box->setVisible(vegetation_map_check->isChecked());
	if (auto* box = findChild<QGroupBox*>(QStringLiteral("feature_box")))
		box->setVisible(feature_map_check->isChecked());
}



bool ElevationWizardDialog::pageWanted(int id) const
{
	auto const cloud = source() == ElevationWizardJob::PointCloud;
	switch (id)
	{
	case ContoursPage:
		return contours_check->isChecked();
	case HillShadePage:
		return hillshade_check->isChecked();
	case SlopePage:
		return slope_check->isChecked();
	case HypsometricPage:
		return hypsometric_check->isChecked();
	case FeaturesPage:
		return features_check->isChecked();
	case CloudProductsPage:
		return cloud && (vegetation_height_check->isChecked() || vegetation_map_check->isChecked() || feature_map_check->isChecked());
	default:
		return true;
	}
}

int ElevationWizardDialog::nextId() const
{
	auto const current = currentId();
	if (current < 0 || current >= OutputPage)
		return -1;
	for (int id = current + 1; id <= OutputPage; ++id)
	{
		if (pageWanted(id))
			return id;
	}
	return -1;
}



ElevationWizardJob ElevationWizardDialog::job() const
{
	auto const cloud = source() == ElevationWizardJob::PointCloud;
	ElevationWizardJob job;
	job.source = source();
	job.files = files();
	job.open_options.feet_to_metres = feet_check->isChecked();
	job.open_options.millimetres_to_metres = millimetres_check->isChecked();
	job.open_options.shift_below_sea_level = shift_check->isChecked();
	job.gridding.cell = cell_edit->value();
	job.gridding.dtm = dtm_check->isChecked();
	job.gridding.dsm = dsm_check->isChecked();
	job.gridding.dtm_points = dtm_points->choice();
	job.gridding.dsm_points = dsm_points->choice();

	job.contours = contours_check->isChecked();
	job.contour_sets.clear();
	for (auto const* set : { &unsmoothed, &smoothed })
	{
		if (!set->group->isChecked())
			continue;
		ContourParams params;
		params.smoothed = (set == &smoothed);
		params.interval = comboValue(set->interval);
		params.main_interval = comboValue(set->main_interval);
		params.index_interval = comboValue(set->index_interval);
		params.depression_symbols = set->depression_check->isChecked();
		params.curves = params.smoothed && set->curves_check->isChecked();
		job.contour_sets.push_back(params);
	}

	job.hill_shading = hillshade_check->isChecked();
	job.hill_shade.method = HillShadeParams::Method(method_combo->currentData().toInt());
	job.hill_shade.multi_directional = multi_check->isChecked();
	job.hill_shade.azimuth_deg = azimuth_edit->value();
	job.hill_shade.elevation_deg = elevation_edit->value();
	job.hill_shade.exaggeration = exaggeration_edit->value();
	job.hill_shade.cell_size = hill_cell_edit->value();
	job.hill_shade.resampling = bicubic_check->isChecked() ? HillShadeParams::Bicubic : HillShadeParams::Bilinear;
	job.hill_shade.gradient = hill_corners_check->isChecked() ? GradientMethod::Corners : GradientMethod::Horn;

	job.slope = slope_check->isChecked();
	job.slope_params.binary = binary_radio->isChecked();
	job.slope_params.threshold_deg = threshold_edit->value();
	job.slope_params.cell_size = slope_cell_edit->value();
	job.slope_params.gradient = slope_corners_check->isChecked() ? GradientMethod::Corners : GradientMethod::Horn;
	job.landforms.cliffs = cliffs_check->isChecked();
	job.landforms.cliff_min_area = area_edit->value();
	job.landforms.cliff_min_length = length_edit->value();

	job.hypsometric = hypsometric_check->isChecked();
	job.hypsometric_params.ramp = HypsometricParams::Ramp(ramp_combo->currentData().toInt());
	job.hypsometric_params.interval = interval_edit->value();
	job.hypsometric_params.white_at_zero = zero_check->isChecked();

	job.features = features_check->isChecked();
	job.landforms.knolls = knolls_check->isChecked();
	job.landforms.min_height = knoll_height_edit->value();
	job.landforms.depressions = depressions_check->isChecked();
	job.landforms.min_depth = depression_depth_edit->value();
	job.landforms.trees = cloud && trees_check->isChecked();
	job.landforms.tree_min_height = tree_height_edit->value();

	job.vegetation_height = cloud && vegetation_height_check->isChecked();
	job.vegetation_height_options.greyscale = grey_radio->isChecked();
	job.vegetation_height_options.mapping = VegetationHeightOptions::GreyMapping(mapping_combo->currentData().toInt());
	job.vegetation_height_options.max_height = max_height_edit->value();
	job.cloud.vegetation_map = cloud && vegetation_map_check->isChecked();
	auto& vegetation = job.cloud.vegetation;
	vegetation.cell = job.gridding.cell;
	vegetation.undergrowth_from = undergrowth_edits[0]->value();
	vegetation.undergrowth_to = undergrowth_edits[1]->value();
	vegetation.undergrowth_thresholds = {{ undergrowth_edits[2]->value(), undergrowth_edits[3]->value() }};
	vegetation.undergrowth_kernel = undergrowth_kernel_edit->value();
	vegetation.vegetation_from = vegetation_edits[0]->value();
	vegetation.vegetation_to = vegetation_edits[1]->value();
	vegetation.vegetation_thresholds = {{ vegetation_edits[2]->value(), vegetation_edits[3]->value(), vegetation_edits[4]->value() }};
	vegetation.vegetation_kernel = vegetation_kernel_edit->value();
	vegetation.no_data_red = no_data_check->isChecked();
	job.cloud.feature_map = cloud && feature_map_check->isChecked();
	job.cloud.feature_from = feature_edits[0]->value();
	job.cloud.feature_to = feature_edits[1]->value();
	job.cloud.intensity_map = cloud && intensity_check->isChecked();
	job.cloud.classification_map = cloud && classification_check->isChecked();

	job.attach = attach_check->isChecked();
	return job;
}


}  // namespace OpenOrienteering
