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


#ifndef OPENORIENTEERING_OCD_DATETIME_H
#define OPENORIENTEERING_OCD_DATETIME_H

#include <cstring>

#include <Qt>
#include <QtGlobal>
#include <QDate>
#include <QDateTime>
#include <QTime>

// Ocd is a global namespace, as in ocd_types.h.
namespace Ocd
{

/**
 * Converts a Delphi TDateTime value to seconds since the epoch (UTC).
 *
 * OCD version 12 stores the creation and modification dates of objects as
 * Delphi TDateTime values: IEEE doubles counting days since 1899-12-30, in
 * local time (see the design notes,
 * "Object identity and timestamps"). The fraction of a second is dropped.
 *
 * @return The time, or 0 for values on or before the Delphi epoch.
 */
inline qint64 fromDelphiDateTime(double days)
{
	if (!(days > 0.0) || days > 1e7)  // also rejects NaN
		return 0;
	auto const msecs = qRound64(days * 86400000.0);
	auto const date = QDate(1899, 12, 30).addDays(msecs / 86400000);
	auto const time = QTime::fromMSecsSinceStartOfDay(int(msecs % 86400000));
	return QDateTime(date, time, Qt::LocalTime).toSecsSinceEpoch();
}

/**
 * Converts seconds since the epoch (UTC) to a Delphi TDateTime value.
 *
 * @return The value, or 0.0 for an unknown time (0).
 */
inline double toDelphiDateTime(qint64 seconds)
{
	if (seconds <= 0)
		return 0.0;
	auto const local = QDateTime::fromSecsSinceEpoch(seconds, Qt::LocalTime);
	return double(QDate(1899, 12, 30).daysTo(local.date())) + local.time().msecsSinceStartOfDay() / 86400000.0;
}

/**
 * Reads a Delphi TDateTime value from the 8 bytes of an object header field.
 */
inline double delphiDateTimeFromBits(quint64 bits)
{
	double value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

/**
 * Returns the 8 bytes of a Delphi TDateTime value for an object header field.
 */
inline quint64 delphiDateTimeToBits(double value)
{
	quint64 bits;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

}  // namespace Ocd

#endif
