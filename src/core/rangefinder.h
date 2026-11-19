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


#ifndef FOOM_RANGEFINDER_H
#define FOOM_RANGEFINDER_H

#include <QtGlobal>
#include <QByteArray>
#include <QString>

#include "core/map_coord.h"

class QSettings;

namespace OpenOrienteering {

class Georeferencing;


/**
 * One shot of a rangefinder: distances in metres, angles in degrees.
 *
 * Every value may be unknown (NaN). The inclination is positive upwards,
 * the azimuth runs clockwise from north.
 *
 * See the design notes, "Rangefinder dock: detail".
 */
struct RangefinderReading
{
	double horizontal_distance = qQNaN();
	double slope_distance = qQNaN();
	double inclination = qQNaN();
	double azimuth = qQNaN();
	
	/** The horizontal distance which was given, or else the one which follows from slope distance and inclination. */
	double horizontalDistance() const;
	/** The height of the target above the instrument; unknown without an inclination. */
	double heightDifference() const;
	/** Returns true when the reading has an azimuth and a horizontal distance. */
	bool isComplete() const;
};


/**
 * Reads the lines which a rangefinder sends: the sentence $PLTIT,HV with
 * horizontal distance, azimuth, inclination and slope distance.
 *
 * A sentence of another vendor becomes a further case of feed() which
 * fills the same reading.
 *
 * See the design notes, "Readings from a device".
 */
class RangefinderSentenceParser
{
public:
	enum Result
	{
		NoLine,   ///< The line was empty.
		Reading,  ///< The line gave a reading.
		Other     ///< Another sentence, a wrong checksum, or no sentence.
	};
	
	/** Takes one line, with or without its line end. */
	Result feed(const QByteArray& line);
	
	/** The reading of the last line which gave one. */
	const RangefinderReading& reading() const { return last_reading; }
	
	int readingCount() const { return readings; }
	int otherCount() const { return others; }
	
private:
	RangefinderReading last_reading;
	int readings = 0;
	int others = 0;
};


/**
 * A survey with a rangefinder: the reference point, how the readings are
 * to be understood, and the target of the reading before.
 *
 * Positions are map coordinates.
 */
class RangefinderSurvey
{
public:
	enum Sight
	{
		ForwardSight,  ///< The reading was taken at the reference, looking at the target.
		RearSight      ///< The reading was taken at the target, looking back at the reference.
	};
	
	enum Traverse
	{
		LinearTraverse,  ///< Every target becomes the next reference.
		RadialTraverse   ///< All readings share the reference.
	};
	
	Sight sight = ForwardSight;
	Traverse traverse = LinearTraverse;
	/** The azimuth is from magnetic north, which is the map's up direction; otherwise from true north. */
	bool azimuth_is_magnetic = true;
	
	/**
	 * The angle of an azimuth from the map's up direction, clockwise, in degrees.
	 *
	 * The map's up direction is magnetic north by the map's georeferencing,
	 * so an azimuth from true north has the declination taken off.
	 */
	static double mapBearing(double azimuth, bool magnetic, const Georeferencing& georeferencing);
	
	bool hasReference() const { return has_reference; }
	const MapCoordF& reference() const { return reference_point; }
	void setReference(const MapCoordF& position);
	void clearReference();
	
	bool hasPreviousTarget() const { return has_previous_target; }
	const MapCoordF& previousTarget() const { return previous_target; }
	/** Makes the previous target the reference. Returns false when there is none. */
	bool usePreviousTargetAsReference();
	
	/**
	 * Gives the target of a reading.
	 *
	 * Returns false without a reference and for a reading which is not complete.
	 */
	bool target(const RangefinderReading& reading, const Georeferencing& georeferencing, MapCoordF* target) const;
	
	/** The height of the target above the reference: the reading's, with the sign changed for a rear sight. */
	double heightDifference(const RangefinderReading& reading) const;
	
	/**
	 * Takes a reading: gives its target, remembers it as the previous
	 * target and, in a linear traverse, makes it the reference.
	 */
	bool take(const RangefinderReading& reading, const Georeferencing& georeferencing, MapCoordF* target = nullptr);
	
private:
	MapCoordF reference_point;
	MapCoordF previous_target;
	bool has_reference = false;
	bool has_previous_target = false;
};


/**
 * The choices of the rangefinder dock which are kept between sessions.
 */
struct RangefinderSettings
{
	RangefinderSurvey::Sight sight = RangefinderSurvey::ForwardSight;
	RangefinderSurvey::Traverse traverse = RangefinderSurvey::LinearTraverse;
	bool azimuth_is_magnetic = true;
	/** The distance which is entered is the slope distance. */
	bool slope_distance = false;
	/** The port of the device: a serial port, or host:port. */
	QString port;
	/** The speed of the serial port. */
	int baud = 4800;
	/** A reading from the device is drawn at once. */
	bool draw_automatically = true;
	
	/** Reads the settings; keys which are absent or make no sense give the defaults. */
	static RangefinderSettings load(const QSettings& settings);
	/** Reads the settings of the application. */
	static RangefinderSettings load();
	
	void save(QSettings& settings) const;
	/** Writes the settings of the application. */
	void save() const;
};


}  // namespace OpenOrienteering

#endif
