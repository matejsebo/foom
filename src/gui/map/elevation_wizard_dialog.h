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

#ifndef OPENORIENTEERING_ELEVATION_WIZARD_DIALOG_H
#define OPENORIENTEERING_ELEVATION_WIZARD_DIALOG_H

#include <array>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QWizard>

#include "elevation/elevation_wizard.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The elevation wizard.
 *
 * Its pages fill an ElevationWizardJob; runElevationWizard() makes the
 * products. See the design notes, "Wizard".
 */
class ElevationWizardDialog : public QWizard
{
Q_OBJECT
public:
	enum Page
	{
		SourcePage,
		SettingsPage,
		ProductsPage,
		ContoursPage,
		HillShadePage,
		SlopePage,
		HypsometricPage,
		FeaturesPage,
		CloudProductsPage,
		OutputPage
	};

	/// map_path: the map's file, or empty while it is unsaved; it tells where the files go.
	ElevationWizardDialog(const QString& map_path, QWidget* parent);
	~ElevationWizardDialog() override;

	void setSource(ElevationWizardJob::Source source);
	ElevationWizardJob::Source source() const;

	void setFiles(const QStringList& files);
	QStringList files() const;

	/// The job of the pages.
	ElevationWizardJob job() const;

	int nextId() const override;

private:
	struct ContourSet
	{
		QGroupBox* group;
		QComboBox* interval;
		QComboBox* main_interval;
		QComboBox* index_interval;
		QCheckBox* depression_check;
		QCheckBox* curves_check;
	};

	class Page_;
	friend class Page_;

	bool pageWanted(int id) const;
	void addFiles();
	void removeFiles();
	void initializeSettings();
	bool validateSource();
	bool validateSettings() const;
	void initializeProducts();
	void initializeFeatures();
	void initializeOutput();
	void updateEnabled();

	QWidget* makeSourcePage();
	QWidget* makeSettingsPage();
	QWidget* makeProductsPage();
	QWidget* makeContoursPage();
	ContourSet makeContourSet(const QString& title, bool smoothed);
	QWidget* makeHillShadePage();
	QWidget* makeSlopePage();
	QWidget* makeHypsometricPage();
	QWidget* makeFeaturesPage();
	QWidget* makeCloudProductsPage();
	QWidget* makeOutputPage();

	QString map_path;
	bool settings_ok = false;
	int products_source = -1;   ///< The source whose defaults the products page shows.

	// Source
	QRadioButton* grid_radio;
	QRadioButton* cloud_radio;
	QListWidget* file_list;
	QPushButton* remove_button;

	// Settings
	QGroupBox* grid_box;
	QCheckBox* feet_check;
	QCheckBox* millimetres_check;
	QCheckBox* shift_check;
	QGroupBox* cloud_box;
	QDoubleSpinBox* cell_edit;
	QCheckBox* dtm_check;
	QCheckBox* dsm_check;
	QLabel* report_label;

	// Products
	QCheckBox* contours_check;
	QCheckBox* hillshade_check;
	QCheckBox* slope_check;
	QCheckBox* hypsometric_check;
	QCheckBox* features_check;
	QCheckBox* vegetation_height_check;
	QCheckBox* vegetation_map_check;
	QCheckBox* feature_map_check;
	QCheckBox* intensity_check;
	QCheckBox* classification_check;

	// Contours
	ContourSet unsmoothed;
	ContourSet smoothed;

	// Hill shading
	QComboBox* method_combo;
	QCheckBox* multi_check;
	QDoubleSpinBox* azimuth_edit;
	QDoubleSpinBox* elevation_edit;
	QDoubleSpinBox* exaggeration_edit;
	QDoubleSpinBox* hill_cell_edit;
	QCheckBox* bicubic_check;       // foom
	QCheckBox* hill_corners_check;  // foom

	// Slope gradient
	QRadioButton* continuous_radio;
	QRadioButton* binary_radio;
	QDoubleSpinBox* threshold_edit;
	QDoubleSpinBox* slope_cell_edit;
	QCheckBox* slope_corners_check;  // foom
	QCheckBox* cliffs_check;
	QSpinBox* area_edit;
	QSpinBox* length_edit;

	// Hypsometric map
	QComboBox* ramp_combo;
	QDoubleSpinBox* interval_edit;
	QCheckBox* zero_check;

	// Features
	QCheckBox* knolls_check;
	QDoubleSpinBox* knoll_height_edit;
	QCheckBox* depressions_check;
	QDoubleSpinBox* depression_depth_edit;
	QCheckBox* trees_check;
	QSpinBox* tree_height_edit;

	// Point-cloud products
	QRadioButton* coloured_radio;
	QRadioButton* grey_radio;
	QComboBox* mapping_combo;
	QDoubleSpinBox* max_height_edit;
	std::array<QDoubleSpinBox*, 4> undergrowth_edits;   ///< from, to, two thresholds
	std::array<QDoubleSpinBox*, 5> vegetation_edits;    ///< from, to, three thresholds
	QSpinBox* undergrowth_kernel_edit;
	QSpinBox* vegetation_kernel_edit;
	QCheckBox* no_data_check;
	std::array<QDoubleSpinBox*, 2> feature_edits;

	// Output
	QLabel* folder_label;
	QCheckBox* attach_check;
	QLabel* summary_label;
};


}  // namespace OpenOrienteering

#endif
