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

#include "geoviewer.h"

#include <cmath>

#include <QtMath>
#include <QLatin1String>
#include <QSettings>
#include <QVariant>

#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map_coord.h"


namespace OpenOrienteering {

namespace {

const QLatin1String settings_key("Geoviewer/url");

}  // namespace


QString geoviewerDefaultPattern()
{
	// Google Street View at the place, looking in the direction.
	return QStringLiteral("https://maps.google.com/maps?q=&layer=c&cbll={latitude},{longitude}&cbp=11,{direction},0,0,0");
}

QString geoviewerPattern()
{
	auto const pattern = QSettings().value(settings_key).toString();
	return pattern.isEmpty() ? geoviewerDefaultPattern() : pattern;
}

void setGeoviewerPattern(const QString& pattern)
{
	QSettings settings;
	if (pattern.isEmpty() || pattern == geoviewerDefaultPattern())
		settings.remove(settings_key);
	else
		settings.setValue(settings_key, pattern);
}

QUrl geoviewerUrl(const QString& pattern, const GeoviewerTarget& target)
{
	auto direction = std::fmod(std::round(target.direction), 360.0);
	if (direction < 0)
		direction += 360.0;
	auto text = pattern;
	text.replace(QLatin1String("{latitude}"), QString::number(target.latitude, 'f', 6));
	text.replace(QLatin1String("{longitude}"), QString::number(target.longitude, 'f', 6));
	text.replace(QLatin1String("{direction}"), QString::number(int(direction)));
	return QUrl(text);
}

bool geoviewerTarget(const Georeferencing& georef, const MapCoordF& from, const MapCoordF& to, GeoviewerTarget& target)
{
	if (georef.getState() != Georeferencing::Geospatial)
		return false;
	auto ok = false;
	auto const start = georef.toGeographicCoords(from, &ok);
	if (!ok)
		return false;
	target.latitude = start.latitude();
	target.longitude = start.longitude();
	target.direction = 0.0;
	if (from == to)
		return true;
	auto const end = georef.toGeographicCoords(to, &ok);
	if (!ok)
		return false;
	// The initial bearing of the great circle from start to end.
	auto const phi1 = qDegreesToRadians(start.latitude());
	auto const phi2 = qDegreesToRadians(end.latitude());
	auto const dlambda = qDegreesToRadians(end.longitude() - start.longitude());
	auto const y = std::sin(dlambda) * std::cos(phi2);
	auto const x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dlambda);
	if (x == 0 && y == 0)
		return true;
	auto bearing = qRadiansToDegrees(std::atan2(y, x));
	if (bearing < 0)
		bearing += 360.0;
	target.direction = bearing;
	return true;
}


}  // namespace OpenOrienteering
