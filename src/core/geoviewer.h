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

#ifndef FOOM_GEOVIEWER_H
#define FOOM_GEOVIEWER_H

#include <QString>
#include <QUrl>

namespace OpenOrienteering {

class Georeferencing;
class MapCoordF;


/**
 * The geoviewer: a web viewer opened at a place of the map, looking in a
 * direction.
 *
 * See the design notes, "Online map and geoviewer".
 */

/** A place on the earth and a direction from true north in degrees. */
struct GeoviewerTarget
{
	double latitude = 0.0;
	double longitude = 0.0;
	double direction = 0.0;
};

/** The default pattern: Google Street View at the place. */
QString geoviewerDefaultPattern();

/** The pattern of the settings (Geoviewer/url), the default when there is none. */
QString geoviewerPattern();

/** Keeps the pattern in the settings; an empty one removes it. */
void setGeoviewerPattern(const QString& pattern);

/**
 * The pattern with {latitude} and {longitude} as degrees with 6 decimals and
 * {direction} as whole degrees from 0 to 359.
 */
QUrl geoviewerUrl(const QString& pattern, const GeoviewerTarget& target);

/**
 * The place of the map point from, and the direction towards the map point to
 * by the initial great-circle bearing (0 when they are the same).
 * Returns false for a georeferencing which is not geospatial.
 */
bool geoviewerTarget(const Georeferencing& georef, const MapCoordF& from, const MapCoordF& to, GeoviewerTarget& target);


}  // namespace OpenOrienteering

#endif
