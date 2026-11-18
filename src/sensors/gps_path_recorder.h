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


#ifndef FOOM_GPS_PATH_RECORDER_H
#define FOOM_GPS_PATH_RECORDER_H

#include <QObject>
#include <QString>

#include "core/map_coord.h"

class QDoubleSpinBox;
class QFormLayout;
class QSettings;
class QWidget;

namespace OpenOrienteering {


/**
 * Decides which GPS positions become vertices of the path which is drawn.
 *
 * The first position is taken, and then every one which lies farther than
 * the minimum distance from the last one taken.
 *
 * See the design notes, "Recording into a symbol".
 */
class GpsPathRecorder
{
public:
	/**
	 * @param min_distance       The minimum distance on the ground, in metres.
	 * @param scale_denominator  The scale of the map.
	 */
	GpsPathRecorder(double min_distance, double scale_denominator);
	
	/** Returns true if the position is to become a vertex, and remembers it then. */
	bool accept(const MapCoordF& position);
	
	/** Remembers a vertex which was added whatever its distance. */
	void take(const MapCoordF& position);
	
	bool isPaused() const { return paused; }
	/** While paused, accept() takes nothing. */
	void setPaused(bool value) { paused = value; }
	
	/** The number of positions taken. */
	int count() const { return taken; }
	
private:
	MapCoordF last;
	double min_distance_mm;
	int taken = 0;
	bool paused = false;
};


/** The settings of the recording. */
struct GpsRecordSettings
{
	/** The minimum distance between two recorded vertices, in metres. */
	double min_distance = 2.0;
	
	/** Reads the settings; a key which is absent or makes no sense gives the default. */
	static GpsRecordSettings load(const QSettings& settings);
	/** Reads the settings of the application. */
	static GpsRecordSettings load();
	
	void save(QSettings& settings) const;
	/** Writes the settings of the application. */
	void save() const;
};


/**
 * The row of the sensors settings page for the recording.
 *
 * The widget carries the object name record-min-distance.
 */
class GpsRecordRows : public QObject
{
Q_OBJECT
public:
	/** Appends the row to the layout. */
	GpsRecordRows(QFormLayout* layout, QWidget* parent);
	~GpsRecordRows() override;
	
	GpsRecordSettings settings() const;
	void setSettings(const GpsRecordSettings& settings);
	
private:
	QDoubleSpinBox* distance_box;
};


}  // namespace OpenOrienteering

#endif
