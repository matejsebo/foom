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


#ifndef FOOM_GPS_TAIL_H
#define FOOM_GPS_TAIL_H

#include <cstddef>
#include <vector>

#include <QObject>
#include <QString>

#include "core/map_coord.h"

class QDoubleSpinBox;
class QFormLayout;
class QSettings;
class QWidget;

namespace OpenOrienteering {


/**
 * The last piece of the way of the GPS position, which is drawn behind
 * the position marker as a line which fades towards its old end.
 *
 * Positions and lengths are in millimetres on the map.
 *
 * See the design notes,
 * "Antenna height, averaged vertex and tail".
 */
class GpsTail
{
public:
	/** The greatest length of the tail. Initially 0: the tail holds nothing. */
	double maxLength() const { return max_length; }
	/** Sets the greatest length and cuts the tail to it. */
	void setMaxLength(double length);
	
	/**
	 * Appends a position and cuts the old end of the tail to the greatest length.
	 * A position which equals the last one adds nothing.
	 */
	void add(const MapCoordF& position);
	
	void clear();
	
	/** The positions, the oldest first. */
	const std::vector<MapCoordF>& points() const { return tail; }
	
	/** The length of the tail. */
	double length() const;
	
	/**
	 * The opacity with which the tail is drawn at one of its points:
	 * 1 at the newest one, falling evenly to 0 at the greatest length behind it.
	 */
	double opacityAt(std::size_t index) const;
	
private:
	void trim();
	
	std::vector<MapCoordF> tail;
	double max_length = 0;
};


/** The settings of the GPS display which are FOOM's. */
struct GpsDisplaySettings
{
	/** The height of the antenna above the ground, in metres. */
	double antenna_height = 0.0;
	/** The length of the tail on the ground, in metres; 0 for no tail. */
	double tail_length = 30.0;
	
	/** Reads the settings; a key which is absent or makes no sense gives the default. */
	static GpsDisplaySettings load(const QSettings& settings);
	/** Reads the settings of the application. */
	static GpsDisplaySettings load();
	
	void save(QSettings& settings) const;
	/** Writes the settings of the application. */
	void save() const;
};

bool operator==(const GpsDisplaySettings& lhs, const GpsDisplaySettings& rhs);
inline bool operator!=(const GpsDisplaySettings& lhs, const GpsDisplaySettings& rhs) { return !(lhs == rhs); }


/**
 * The rows of the sensors settings page for the antenna height and the tail.
 *
 * The widgets carry the object names antenna-height and tail-length.
 */
class GpsDisplayRows : public QObject
{
Q_OBJECT
public:
	/** Appends the rows to the layout. */
	GpsDisplayRows(QFormLayout* layout, QWidget* parent);
	~GpsDisplayRows() override;
	
	GpsDisplaySettings settings() const;
	void setSettings(const GpsDisplaySettings& settings);
	
private:
	QDoubleSpinBox* antenna_box;
	QDoubleSpinBox* tail_box;
};


}  // namespace OpenOrienteering

#endif
