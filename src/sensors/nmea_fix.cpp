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


#include "nmea_fix.h"

#include <algorithm>
#include <cmath>

#include <QList>


namespace OpenOrienteering {

namespace {

/// Both unknown, or the same.
bool sameValue(double a, double b)
{
	return (std::isnan(a) && std::isnan(b)) || a == b;
}

double numberOf(const QByteArray& field)
{
	auto ok = false;
	auto const value = field.trimmed().toDouble(&ok);
	return ok ? value : qQNaN();
}

/// Reads hhmmss with an optional fraction of the second.
QTime timeOf(const QByteArray& field)
{
	auto const text = field.trimmed();
	if (text.size() < 6)
		return {};
	auto ok_h = false, ok_m = false, ok_s = false;
	auto const h = text.mid(0, 2).toInt(&ok_h);
	auto const m = text.mid(2, 2).toInt(&ok_m);
	auto const s = text.mid(4).toDouble(&ok_s);
	if (!ok_h || !ok_m || !ok_s || s < 0 || s >= 60)
		return {};
	auto const whole = int(std::floor(s));
	auto const msec = std::min(999, qRound((s - whole) * 1000));
	return QTime(h, m, whole, msec);
}

}  // namespace



double FixInfo::horizontalError() const
{
	if (!std::isnan(sd_latitude) && !std::isnan(sd_longitude))
		return std::max(sd_latitude, sd_longitude);
	if (!std::isnan(sd_latitude))
		return sd_latitude;
	if (!std::isnan(sd_longitude))
		return sd_longitude;
	if (from_sentences)
		return qQNaN();
	return accuracy;
}

// static
const char* FixInfo::propertyName()
{
	return "foomFixInfo";
}

bool operator==(const FixInfo& lhs, const FixInfo& rhs)
{
	return lhs.fix_class == rhs.fix_class
	       && lhs.from_sentences == rhs.from_sentences
	       && lhs.satellites == rhs.satellites
	       && sameValue(lhs.hdop, rhs.hdop)
	       && sameValue(lhs.altitude, rhs.altitude)
	       && sameValue(lhs.geoid_separation, rhs.geoid_separation)
	       && sameValue(lhs.sd_latitude, rhs.sd_latitude)
	       && sameValue(lhs.sd_longitude, rhs.sd_longitude)
	       && sameValue(lhs.accuracy, rhs.accuracy)
	       && lhs.utc == rhs.utc;
}


FixInfo::Class fixClassOfGgaQuality(const QByteArray& field)
{
	auto ok = false;
	auto const quality = field.trimmed().toInt(&ok);
	if (!ok)
		return FixInfo::Unknown;
	switch (quality)
	{
	case 0:
		return FixInfo::Invalid;
	case 1:
	case 3:  // precise positioning service
		return FixInfo::Autonomous;
	case 2:
		return FixInfo::Differential;
	case 4:
		return FixInfo::RtkFixed;
	case 5:
		return FixInfo::RtkFloat;
	case 6:  // dead reckoning
	case 7:  // typed in
	case 8:  // simulator
		return FixInfo::Invalid;
	default:
		return FixInfo::Unknown;
	}
}



NmeaFixParser::Result NmeaFixParser::feed(const QByteArray& data)
{
	auto const line = data.trimmed();
	if (line.size() < 6 || line.at(0) != '$')
		return NotASentence;
	
	// The checksum is the exclusive or of the characters between $ and *.
	auto const star = line.lastIndexOf('*');
	auto checksum_ok = false;
	if (star > 0 && line.size() - star == 3)
	{
		auto ok = false;
		auto const given = line.mid(star + 1).toInt(&ok, 16);
		auto computed = 0;
		for (auto i = 1; i < star; ++i)
			computed ^= static_cast<unsigned char>(line.at(i));
		checksum_ok = ok && given == computed;
	}
	if (!checksum_ok)
	{
		++bad_checksums;
		return BadChecksum;
	}
	
	auto const fields = line.mid(1, star - 1).split(',');
	auto const field = [&fields](int index) {
		return index < fields.size() ? fields.at(index) : QByteArray{};
	};
	auto const id = field(0);
	if (id.size() != 5)
		return Other;
	
	if (id.endsWith("GGA"))
	{
		FixInfo fix;
		fix.from_sentences = true;
		fix.utc = timeOf(field(1));
		fix.fix_class = fixClassOfGgaQuality(field(6));
		auto ok = false;
		auto const satellites = field(7).trimmed().toInt(&ok);
		if (ok && satellites >= 0)
			fix.satellites = satellites;
		fix.hdop = numberOf(field(8));
		fix.altitude = numberOf(field(9));
		fix.geoid_separation = numberOf(field(11));
		if (fix.utc.isValid() && fix.utc == gst_time)
		{
			fix.sd_latitude = gst_latitude;
			fix.sd_longitude = gst_longitude;
		}
		current = fix;
		return Gga;
	}
	
	if (id.endsWith("GST"))
	{
		gst_time = timeOf(field(1));
		gst_latitude = numberOf(field(6));
		gst_longitude = numberOf(field(7));
		if (gst_time.isValid() && gst_time == current.utc)
		{
			current.sd_latitude = gst_latitude;
			current.sd_longitude = gst_longitude;
		}
		return Gst;
	}
	
	return Other;
}


}  // namespace OpenOrienteering
