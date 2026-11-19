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


#include "rangefinder.h"

#include <cmath>

#include <QtMath>
#include <QLatin1String>
#include <QSettings>
#include <QString>
#include <QVariant>

#include "core/georeferencing.h"


namespace OpenOrienteering {

namespace {

QString sightKey() { return QStringLiteral("Rangefinder/sight"); }
QString traverseKey() { return QStringLiteral("Rangefinder/traverse"); }
QString magneticKey() { return QStringLiteral("Rangefinder/azimuth_magnetic"); }
QString slopeKey() { return QStringLiteral("Rangefinder/slope_distance"); }

}  // namespace



double RangefinderReading::horizontalDistance() const
{
	if (!qIsNaN(horizontal_distance))
		return horizontal_distance;
	if (!qIsNaN(slope_distance) && !qIsNaN(inclination))
		return slope_distance * std::cos(qDegreesToRadians(inclination));
	return qQNaN();
}

double RangefinderReading::heightDifference() const
{
	if (qIsNaN(inclination))
		return qQNaN();
	if (!qIsNaN(horizontal_distance))
		return horizontal_distance * std::tan(qDegreesToRadians(inclination));
	if (!qIsNaN(slope_distance))
		return slope_distance * std::sin(qDegreesToRadians(inclination));
	return qQNaN();
}

bool RangefinderReading::isComplete() const
{
	return !qIsNaN(azimuth) && !qIsNaN(horizontalDistance());
}



// static
double RangefinderSurvey::mapBearing(double azimuth, bool magnetic, const Georeferencing& georeferencing)
{
	return magnetic ? azimuth : azimuth - georeferencing.getDeclination();
}

void RangefinderSurvey::setReference(const MapCoordF& position)
{
	reference_point = position;
	has_reference = true;
}

void RangefinderSurvey::clearReference()
{
	has_reference = false;
}

bool RangefinderSurvey::usePreviousTargetAsReference()
{
	if (!has_previous_target)
		return false;
	setReference(previous_target);
	return true;
}

bool RangefinderSurvey::target(const RangefinderReading& reading, const Georeferencing& georeferencing, MapCoordF* target) const
{
	if (!has_reference || !reading.isComplete())
		return false;
	auto const scale = double(georeferencing.getScaleDenominator());
	if (scale <= 0)
		return false;
	
	auto const bearing = qDegreesToRadians(mapBearing(reading.azimuth, azimuth_is_magnetic, georeferencing));
	auto length = reading.horizontalDistance() * 1000.0 / scale;  // mm on the map
	if (sight == RearSight)
		length = -length;
	// The map's y axis points down.
	if (target)
		*target = MapCoordF(reference_point.x() + length * std::sin(bearing), reference_point.y() - length * std::cos(bearing));
	return true;
}

double RangefinderSurvey::heightDifference(const RangefinderReading& reading) const
{
	auto const difference = reading.heightDifference();
	return sight == RearSight ? -difference : difference;
}

bool RangefinderSurvey::take(const RangefinderReading& reading, const Georeferencing& georeferencing, MapCoordF* target)
{
	MapCoordF result;
	if (!this->target(reading, georeferencing, &result))
		return false;
	previous_target = result;
	has_previous_target = true;
	if (traverse == LinearTraverse)
		setReference(result);
	if (target)
		*target = result;
	return true;
}



// static
RangefinderSettings RangefinderSettings::load(const QSettings& settings)
{
	RangefinderSettings result;
	if (settings.value(sightKey()).toString() == QLatin1String("rear"))
		result.sight = RangefinderSurvey::RearSight;
	if (settings.value(traverseKey()).toString() == QLatin1String("radial"))
		result.traverse = RangefinderSurvey::RadialTraverse;
	result.azimuth_is_magnetic = settings.value(magneticKey(), true).toBool();
	result.slope_distance = settings.value(slopeKey(), false).toBool();
	return result;
}

// static
RangefinderSettings RangefinderSettings::load()
{
	QSettings settings;
	return load(settings);
}

void RangefinderSettings::save(QSettings& settings) const
{
	settings.setValue(sightKey(), sight == RangefinderSurvey::RearSight ? QStringLiteral("rear") : QStringLiteral("forward"));
	settings.setValue(traverseKey(), traverse == RangefinderSurvey::RadialTraverse ? QStringLiteral("radial") : QStringLiteral("linear"));
	settings.setValue(magneticKey(), azimuth_is_magnetic);
	settings.setValue(slopeKey(), slope_distance);
}

void RangefinderSettings::save() const
{
	QSettings settings;
	save(settings);
}


}  // namespace OpenOrienteering
