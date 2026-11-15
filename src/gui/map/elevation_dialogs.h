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


#ifndef OPENORIENTEERING_ELEVATION_DIALOGS_H
#define OPENORIENTEERING_ELEVATION_DIALOGS_H

#include <vector>

#include <QDialog>
#include <QObject>

#include "elevation/contours.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/raster_products.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QRadioButton;
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


}  // namespace OpenOrienteering

#endif
