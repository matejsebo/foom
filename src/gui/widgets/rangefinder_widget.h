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


#ifndef FOOM_RANGEFINDER_WIDGET_H
#define FOOM_RANGEFINDER_WIDGET_H

#include <functional>

#include <QObject>
#include <QString>
#include <QWidget>

#include "core/map_coord.h"
#include "core/rangefinder.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace OpenOrienteering {

class Map;
class RangefinderSource;


/**
 * The contents of the rangefinder dock: the reference, the fields of a
 * reading which is entered by hand, and what follows from them.
 *
 * The widget keeps the survey. It draws nothing on the map and adds no
 * object itself: changed() says that the reference or the target moved,
 * and the function given to setAddFunction() draws a target.
 *
 * The fields carry object names which begin with "rangefinder-".
 *
 * See the design notes, "Rangefinder dock: detail".
 */
class RangefinderWidget : public QWidget
{
Q_OBJECT
public:
	/**
	 * Creates the widget for a map. The mobile form has a button to close it.
	 */
	RangefinderWidget(Map* map, bool mobile, QWidget* parent = nullptr);
	~RangefinderWidget() override;
	
	const RangefinderSurvey& survey() const { return current_survey; }
	
	/** The reading which stands in the fields. A distance of 0 is no distance. */
	RangefinderReading reading() const;
	
	/** Gives the target of the reading which stands in the fields; false when there is none. */
	bool previewTarget(MapCoordF* target) const;
	
	void setReference(const MapCoordF& position);
	
	/**
	 * Sets the function which draws a target when Add is pressed.
	 *
	 * When it returns false, the reading is not taken. Without a function
	 * Add does nothing.
	 */
	void setAddFunction(std::function<bool (const MapCoordF&)> function);
	
	/** Sets the action behind the button which lets the reference be clicked on the map. */
	void setReferenceClickAction(QAction* action);
	
	/** The source which reads the device. */
	RangefinderSource* source() const { return device_source; }
	
	/**
	 * Takes a reading from the device: puts what it has into the fields
	 * and, with Draw automatically, adds it as Add does.
	 */
	void deviceReading(const OpenOrienteering::RangefinderReading& reading);
	
signals:
	/** The reference or the target of the fields changed. */
	void changed();
	/** The path which is drawn is to be ended at its last vertex. */
	void finishRequested();
	/** The reference is to be taken from the GPS position. */
	void referenceFromGpsRequested();
	/** The button to close the mobile form was pressed. */
	void closeClicked();
	
private:
	void updateDisplay();
	void storeChoices();
	void addClicked();
	void connectClicked(bool checked);
	void updateDeviceState();
	void updateCounts();
	
	Map* map;
	RangefinderSurvey current_survey;
	std::function<bool (const MapCoordF&)> add_function;
	
	QLabel* reference_label;
	QToolButton* reference_click_button;
	QPushButton* reference_gps_button;
	QPushButton* reference_previous_button;
	QDoubleSpinBox* distance_box;
	QComboBox* distance_kind_box;
	QDoubleSpinBox* inclination_box;
	QDoubleSpinBox* azimuth_box;
	QCheckBox* magnetic_check;
	QComboBox* sight_box;
	QComboBox* traverse_box;
	QLabel* height_label;
	QLabel* target_label;
	QPushButton* add_button;
	QPushButton* finish_button;
	RangefinderSource* device_source;
	QComboBox* port_box;
	QComboBox* baud_box;
	QPushButton* connect_button;
	QLabel* state_label;
	QCheckBox* auto_check;
	QLabel* counts_label;
};


}  // namespace OpenOrienteering

#endif
