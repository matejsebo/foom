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


#ifndef FOOM_NMEA_FIX_H
#define FOOM_NMEA_FIX_H

#include <QtGlobal>
#include <QByteArray>
#include <QMetaType>
#include <QTime>

namespace OpenOrienteering {


/**
 * The quality data of a position fix.
 *
 * A source which reads NMEA sentences fills this from GGA and GST and sets
 * it on itself as the dynamic property named by propertyName(). For other
 * sources, only the accuracy is known.
 *
 * See the design notes, "GNSS sources: detail".
 */
struct FixInfo
{
	/** The class of the fix, from worst to best. */
	enum Class
	{
		Unknown = -1,
		Invalid = 0,
		Autonomous,
		Differential,
		RtkFloat,
		RtkFixed,
	};
	
	Class fix_class = Unknown;
	/** True if the data was read from NMEA sentences. */
	bool from_sentences = false;
	/** Satellites in use, or -1. */
	int satellites = -1;
	double hdop = qQNaN();
	/** Antenna altitude above mean sea level, in metres. */
	double altitude = qQNaN();
	/** Geoid separation, in metres. */
	double geoid_separation = qQNaN();
	/** Standard deviation of the latitude (GST), in metres. */
	double sd_latitude = qQNaN();
	/** Standard deviation of the longitude (GST), in metres. */
	double sd_longitude = qQNaN();
	/** Horizontal accuracy as given by the position source, in metres. */
	double accuracy = qQNaN();
	/** Time of the fix (UTC), from GGA. */
	QTime utc;
	
	/**
	 * The horizontal error the quality gate compares with its limit, in metres.
	 *
	 * For a fix read from sentences, this is the larger GST deviation, and
	 * NaN without GST. Otherwise it is the accuracy.
	 */
	double horizontalError() const;
	
	/** The name of the dynamic property by which a source publishes its fix data. */
	static const char* propertyName();
};

bool operator==(const FixInfo& lhs, const FixInfo& rhs);
inline bool operator!=(const FixInfo& lhs, const FixInfo& rhs) { return !(lhs == rhs); }


/** Returns the fix class for the quality field (field 6) of a GGA sentence. */
FixInfo::Class fixClassOfGgaQuality(const QByteArray& field);


/**
 * Reads the quality data of fixes from a stream of NMEA sentences.
 *
 * Sentences other than GGA and GST are ignored. A GST belongs to the GGA
 * of the same time, in whatever order the two arrive.
 */
class NmeaFixParser
{
public:
	enum Result
	{
		NotASentence,
		BadChecksum,
		Other,
		Gga,
		Gst,
	};
	
	/** Reads a line. The line end may be part of the data. */
	Result feed(const QByteArray& data);
	
	/** The data of the latest fix. */
	const FixInfo& fix() const { return current; }
	
	/** The number of sentences refused for their checksum. */
	int badChecksums() const { return bad_checksums; }
	
private:
	FixInfo current;
	QTime gst_time;
	double gst_latitude = qQNaN();
	double gst_longitude = qQNaN();
	int bad_checksums = 0;
};


}  // namespace OpenOrienteering

Q_DECLARE_METATYPE(OpenOrienteering::FixInfo)

#endif
