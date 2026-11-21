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

#ifndef FOOM_ELEVATION_PROFILE_H
#define FOOM_ELEVATION_PROFILE_H

#include <limits>
#include <vector>

#include <QPointF>
#include <QString>

namespace OpenOrienteering {

class ElevationModel;


/**
 * The heights along a line, see the design notes, "Profile".
 */
struct ProfileSample
{
	double distance = 0.0;        ///< m along the line from its start.
	QPointF projected;
	double height = std::numeric_limits<double>::quiet_NaN();   ///< NaN over void or outside the grid.
};

struct Profile
{
	std::vector<ProfileSample> samples;
	double length = 0.0;          ///< m.
	double start = std::numeric_limits<double>::quiet_NaN();    ///< The first known height.
	double end = std::numeric_limits<double>::quiet_NaN();      ///< The last known height.
	double minimum = std::numeric_limits<double>::quiet_NaN();
	double maximum = std::numeric_limits<double>::quiet_NaN();
	double ascent = 0.0;          ///< m, changes below the hysteresis left out.
	double descent = 0.0;         ///< m, ditto.
};

/**
 * Samples the grid along the line (projected coordinates) every step metres, the
 * line's end included; a step of 0 takes the grid's cell size. Heights are
 * interpolated bilinearly. Ascent and descent count a change only once it reaches
 * the hysteresis from the last counted height.
 */
Profile elevationProfile(const ElevationModel& grid, const std::vector<QPointF>& line, double step = 0.0, double hysteresis = 1.0);

/// Writes distance;easting;northing;height per sample, two decimals, an empty height for NaN.
bool writeProfileCsv(const Profile& profile, const QString& path, QString* error = nullptr);


}  // namespace OpenOrienteering

#endif
