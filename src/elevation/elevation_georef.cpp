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


#include "elevation_georef.h"

#include <QLatin1String>
#include <QPointF>

#include "core/map.h"
#include "elevation/elevation_model.h"


namespace OpenOrienteering {

Georeferencing elevationGeoreferencing(const Georeferencing& georef)
{
	Georeferencing result(georef);
	if (georef.getState() != Georeferencing::Geospatial)
		return result;
	auto const units = ElevationModel::linearUnits(georef.getProjectedCRSSpec());
	if (units.metric_crs.isEmpty())
		return result;

	// The same projection in metres: every projected coordinate is the map's times the length of its unit, so the
	// reference point and the scale factor are the map's with that factor, and the grivation is the map's.
	auto const reference = georef.getProjectedRefPoint();
	auto const grivation = georef.getGrivation();
	auto const combined = georef.getCombinedScaleFactor();
	if (!result.setProjectedCRS(QStringLiteral("Elevation"), units.metric_crs))
		return Georeferencing(georef);
	result.setProjectedRefPoint(QPointF(reference.x() * units.horizontal, reference.y() * units.horizontal), false, false);
	result.setGrivation(grivation);
	result.setCombinedScaleFactor(combined * units.horizontal);
	return result;
}

Georeferencing elevationGeoreferencing(const Map& map)
{
	return elevationGeoreferencing(map.getGeoreferencing());
}

QString elevationCrs(const Georeferencing& georef)
{
	auto const spec = georef.getProjectedCRSSpec();
	if (georef.getState() != Georeferencing::Geospatial)
		return spec;
	auto const units = ElevationModel::linearUnits(spec);
	return units.metric_crs.isEmpty() ? spec : units.metric_crs;
}

QString elevationCrs(const Map& map)
{
	return elevationCrs(map.getGeoreferencing());
}


}  // namespace OpenOrienteering
