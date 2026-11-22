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


#ifndef FOOM_OCD_GUIDE_H
#define FOOM_OCD_GUIDE_H

#include <QtGlobal>
#include <QLatin1String>
#include <QString>

#include "core/guides.h"
#include "core/map_coord.h"
#include "fileformats/ocd_parameter_stream_reader.h"

namespace OpenOrienteering {

/// The OCD parameter string type of a guide.
constexpr int ocd_guide_string_type = 26;

/**
 * Parses an OCD type 26 parameter string: an empty first field, then
 * h (0 vertical, else horizontal) and c, the position as an OCD coordinate
 * (0.01 mm shifted left by 8 bits, y up). Returns false without a valid c.
 *
 * See the design notes, "OCD string type 26".
 */
inline bool parseOcdGuide(const QString& param_string, Guide& out)
{
	OcdParameterStreamReader parameters(param_string);
	auto horizontal = false;
	auto have_position = false;
	qint32 c = 0;
	while (parameters.readNext())
	{
		auto const value = parameters.value();
		switch (parameters.key())
		{
		case 'h':
			horizontal = value.toInt() != 0;
			break;
		case 'c':
			c = value.toInt(&have_position);
			break;
		default:
			break;
		}
	}
	if (!have_position)
		return false;
	auto const hundredths = c >> 8;  // arithmetic shift, as convertOcdPoint() does
	out.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
	out.position = horizontal ? -hundredths * 10 : hundredths * 10;
	return true;
}

/**
 * Returns the OCD type 26 parameter string of a guide, its position less
 * the area offset, rounded half up to 0.01 mm as object coordinates are.
 */
inline QString ocdGuideString(const Guide& guide, const MapCoord& area_offset)
{
	auto const horizontal = guide.orientation == Qt::Horizontal;
	// Native units, y up.
	qint64 value = horizontal ? -(qint64(guide.position) - area_offset.nativeY())
	                          : qint64(guide.position) - area_offset.nativeX();
	auto const hundredths = value < -5 ? (value - 4) / 10 : (value + 5) / 10;
	return QLatin1String(horizontal ? "\th1\tc" : "\th0\tc") + QString::number(hundredths * 256);
}


}  // namespace OpenOrienteering

#endif
