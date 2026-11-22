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


#ifndef OPENORIENTEERING_GNSS_FILE_H
#define OPENORIENTEERING_GNSS_FILE_H

#include <memory>
#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QDate>
#include <QString>
#include <QTime>

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;


/**
 * A position of a GNSS file.
 *
 * See the design notes, "NMEA and FRWD files".
 */
struct GnssFix
{
	QTime utc;                 ///< The time of the fix (UTC).
	QDate date;                ///< The day, when an RMC sentence gave one.
	double latitude = 0.0;     ///< Degrees, north positive.
	double longitude = 0.0;    ///< Degrees, east positive.
	double altitude = qQNaN(); ///< Metres above mean sea level.
	int quality = -1;          ///< The GGA quality, or -1 without GGA.
	int satellites = -1;       ///< Satellites in use, or -1.
	double hdop = qQNaN();

	/// The time as ISO 8601 in UTC, with the date when it is known.
	QString isoTime() const;
};


/** A named point of a GNSS file (NMEA: WPL). */
struct GnssWaypoint
{
	QString name;
	double latitude = 0.0;
	double longitude = 0.0;
};


/** What a GNSS file holds. */
struct GnssFileData
{
	std::vector<GnssFix> fixes;           ///< The valid fixes, in their order.
	std::vector<GnssWaypoint> waypoints;
	int skipped = 0;                      ///< Epochs without a valid position.
	int bad_checksums = 0;                ///< Sentences dropped for their checksum.
	int sentences = 0;                    ///< Sentences read, with a good checksum or none.
	QString error;                        ///< Why the file was not read; empty when it was.

	bool ok() const { return error.isEmpty(); }
};


/** Reads NMEA sentences, one per line. */
GnssFileData readNmea(const QByteArray& bytes);

/**
 * Reads a GNSS file: NMEA. FRWD files are refused with a message,
 * since their format is not known.
 */
GnssFileData readGnssFile(const QString& path);


/** How the data of a GNSS file becomes objects. */
enum class GnssImportKind
{
	Track,       ///< One line object through the fixes.
	Waypoints,   ///< A point object for every waypoint.
	Positions,   ///< A point object for every fix.
};


struct GnssObjects
{
	std::vector<std::unique_ptr<Object>> objects;
	QString error;

	GnssObjects();
	GnssObjects(GnssObjects&&);
	GnssObjects& operator=(GnssObjects&&);
	~GnssObjects();
};


/**
 * Makes the objects of the data, placed by the map's georeferencing.
 *
 * The symbol must be a line symbol for a track and a point symbol otherwise,
 * and the map must be georeferenced.
 */
GnssObjects gnssObjects(const Map& map, const GnssFileData& data, GnssImportKind kind, const Symbol* symbol);

/**
 * Adds the objects to the current part in one undo step and selects them.
 * Returns the number added.
 */
int addGnssObjects(Map& map, std::vector<std::unique_ptr<Object>>&& objects);


}  // namespace OpenOrienteering

#endif
