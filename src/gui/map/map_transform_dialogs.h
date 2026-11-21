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

#ifndef OPENORIENTEERING_MAP_TRANSFORM_DIALOGS_H
#define OPENORIENTEERING_MAP_TRANSFORM_DIALOGS_H

#include <memory>
#include <vector>

#include <QDialog>
#include <QObject>
#include <QPointF>
#include <QString>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QWidget;

namespace OpenOrienteering {

class CRSSelector;
class Georeferencing;
class Map;


/**
 * The map-move dialog: metres east and north.
 *
 * See the design notes, "Move, stretch and mirror".
 */
class MapMoveDialog : public QDialog
{
Q_OBJECT
public:
	explicit MapMoveDialog(QWidget* parent);
	~MapMoveDialog() override;
	
	double east() const;
	double north() const;
	bool carry() const;
	
private:
	QDoubleSpinBox* east_edit;
	QDoubleSpinBox* north_edit;
	QCheckBox* carry_check;
};


/**
 * The map-stretch and map-mirror dialog: percentages and mirrors.
 */
class MapStretchDialog : public QDialog
{
Q_OBJECT
public:
	MapStretchDialog(bool mirror, QWidget* parent);
	~MapStretchDialog() override;
	
	double factorX() const;
	double factorY() const;
	bool mirrorHorizontally() const;
	bool mirrorVertically() const;
	bool carry() const;
	
private:
	QDoubleSpinBox* x_edit;
	QDoubleSpinBox* y_edit;
	QCheckBox* horizontal_check;
	QCheckBox* vertical_check;
	QCheckBox* carry_check;
};


/**
 * The map-reproject dialog: the target system, chosen as in the georeferencing dialog.
 */
class ReprojectDialog : public QDialog
{
Q_OBJECT
public:
	ReprojectDialog(const Georeferencing& georef, QWidget* parent);
	~ReprojectDialog() override;
	
	QString crsId() const;
	QString crsSpec() const;
	std::vector<QString> crsParameters() const;
	
private:
	CRSSelector* selector;
};


/**
 * The map-rotate-magnetic dialog: the declination, with the online lookup, and the resulting rotation.
 */
class MagneticNorthDialog : public QDialog
{
Q_OBJECT
public:
	MagneticNorthDialog(const Georeferencing& georef, QWidget* parent);
	~MagneticNorthDialog() override;
	
	double declination() const;
	bool keepSelectedSymbols() const;
	bool keepLayout() const;
	
private:
	void updateAngles();
	void lookUpDeclination();
	
	std::unique_ptr<Georeferencing> georef;
	QDoubleSpinBox* declination_edit;
	QPushButton* lookup_button;
	QLabel* lookup_label;
	QLabel* angle_label;
	QLabel* rotate_label;
	QCheckBox* symbols_check;
	QCheckBox* layout_check;
	QNetworkAccessManager* network = nullptr;
};


/**
 * The map-center dialog: the map's offset and the new one.
 */
class CenterMapDialog : public QDialog
{
Q_OBJECT
public:
	CenterMapDialog(const Map& map, QWidget* parent);
	~CenterMapDialog() override;
	
	QPointF offset() const;
	
private:
	QDoubleSpinBox* east_edit;
	QDoubleSpinBox* north_edit;
};


}  // namespace OpenOrienteering

#endif
