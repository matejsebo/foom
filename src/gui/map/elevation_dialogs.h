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


#ifndef OPENORIENTEERING_ELEVATION_DIALOGS_H
#define OPENORIENTEERING_ELEVATION_DIALOGS_H

#include <array>
#include <vector>

#include <QDialog>
#include <QObject>

#include "elevation/contours.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "elevation/raster_products.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QRectF;
class QWidget;

namespace OpenOrienteering {


/**
 * Options of Elevation > Import elevation data (dem-open).
 */
class ElevationOpenDialog : public QDialog
{
Q_OBJECT
public:
	ElevationOpenDialog(QWidget* parent);
	~ElevationOpenDialog() override;

	ElevationSet::Kind kind() const;
	ElevationModel::OpenOptions options() const;

private:
	QComboBox* kind_combo;
	QCheckBox* feet_check;
	QCheckBox* millimetres_check;
	QCheckBox* shift_check;
};


/**
 * The dem-resize dialog: the bounds of the box, proposed as the grid's extent.
 */
class ElevationResizeDialog : public QDialog
{
Q_OBJECT
public:
	ElevationResizeDialog(const QRectF& extent, QWidget* parent);
	~ElevationResizeDialog() override;

	QRectF box() const;

private:
	QDoubleSpinBox* west_edit;
	QDoubleSpinBox* south_edit;
	QDoubleSpinBox* east_edit;
	QDoubleSpinBox* north_edit;
};


/**
 * The dem-contours dialog: an unsmoothed and a smoothed set.
 */
class ContourDialog : public QDialog
{
Q_OBJECT
public:
	ContourDialog(double minimum, double maximum, QWidget* parent);
	~ContourDialog() override;

	/// The enabled sets.
	std::vector<ContourParams> sets() const;

private:
	struct SetWidgets
	{
		QGroupBox* group;
		QComboBox* interval;
		QComboBox* main_interval;
		QComboBox* index_interval;
		QCheckBox* depression_check;
		QCheckBox* curves_check;
	};

	SetWidgets makeSet(const QString& title, bool smoothed, bool enabled);

	SetWidgets unsmoothed;
	SetWidgets smoothed;
	QDoubleSpinBox* minimum_edit;
	QDoubleSpinBox* maximum_edit;
};


/**
 * The dem-features dialog: knolls, small depressions, cliffs and trees.
 */
class LandformDialog : public QDialog
{
Q_OBJECT
public:
	explicit LandformDialog(QWidget* parent);
	~LandformDialog() override;
	
	LandformParams params() const;
	
private:
	QCheckBox* knolls_check;
	QDoubleSpinBox* height_edit;
	QCheckBox* depressions_check;
	QDoubleSpinBox* depth_edit;
	QCheckBox* cliffs_check;  // the design notes, "Cliffs from the slope gradient"
	QDoubleSpinBox* threshold_edit;
	QSpinBox* area_edit;
	QSpinBox* length_edit;
	QCheckBox* trees_check;  // the design notes, "Trees"
	QSpinBox* tree_height_edit;
};


/**
 * The dem-point-cloud dialog: the files, their analysis, the cell size and the products.
 */
class PointCloudDialog : public QDialog
{
Q_OBJECT
public:
	PointCloudDialog(const QStringList& paths, QWidget* parent);
	~PointCloudDialog() override;
	
	GriddingParams params() const;
	PointCloudProducts products() const;
	
private:
	void analyse();
	
	QStringList paths;
	QLabel* report_label;
	QDoubleSpinBox* cell_edit;
	QCheckBox* dtm_check;
	QCheckBox* dsm_check;
	QCheckBox* height_check;
	QGroupBox* vegetation_box;
	std::array<QDoubleSpinBox*, 4> undergrowth_edits;   ///< from, to, two thresholds
	std::array<QDoubleSpinBox*, 5> vegetation_edits;    ///< from, to, three thresholds
	QSpinBox* undergrowth_kernel_edit;
	QSpinBox* vegetation_kernel_edit;
	QCheckBox* no_data_check;
};


/**
 * The dem-hillshade dialog.
 */
class HillShadeDialog : public QDialog
{
Q_OBJECT
public:
	HillShadeDialog(double cell_size, QWidget* parent);
	~HillShadeDialog() override;

	HillShadeParams params() const;
	bool attach() const;

private:
	QComboBox* method_combo;
	QCheckBox* multi_check;
	QDoubleSpinBox* azimuth_edit;
	QDoubleSpinBox* elevation_edit;
	QDoubleSpinBox* exaggeration_edit;
	QDoubleSpinBox* cell_edit;
	QCheckBox* attach_check;
};


/**
 * The dem-slope dialog.
 */
class SlopeDialog : public QDialog
{
Q_OBJECT
public:
	SlopeDialog(double cell_size, QWidget* parent);
	~SlopeDialog() override;

	SlopeParams params() const;
	bool attach() const;

private:
	QRadioButton* continuous_button;
	QRadioButton* binary_button;
	QDoubleSpinBox* threshold_edit;
	QDoubleSpinBox* cell_edit;
	QCheckBox* attach_check;
};


/**
 * The dem-hypsometric dialog: the ramps, the interval and white at elevation 0.
 */
class HypsometricDialog : public QDialog
{
Q_OBJECT
public:
	explicit HypsometricDialog(QWidget* parent);
	~HypsometricDialog() override;
	
	HypsometricParams params() const;
	bool attach() const;
	
private:
	QComboBox* ramp_combo;
	QDoubleSpinBox* interval_edit;
	QCheckBox* zero_check;
	QCheckBox* attach_check;
};


}  // namespace OpenOrienteering

#endif
