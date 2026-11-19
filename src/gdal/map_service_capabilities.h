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


#ifndef OPENORIENTEERING_MAP_SERVICE_CAPABILITIES_H
#define OPENORIENTEERING_MAP_SERVICE_CAPABILITIES_H

#include <vector>

#include <QByteArray>
#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "gdal/map_service.h"

namespace OpenOrienteering {


/**
 * One layer offered by a WMS or WMTS server.
 */
struct ServiceLayer
{
	QString id;                       ///< WMS Name, WMTS Identifier
	QString title;
	QStringList systems;              ///< Coordinate systems, as "EPSG:nnnn"; WMS: inherited from parent layers
	QStringList formats;              ///< WMTS: the layer's formats; WMS layers use the GetMap formats
	QStringList matrix_sets;          ///< WMTS tile matrix sets
	double min_scale = 0;             ///< WMS MinScaleDenominator, 0 when none
	double max_scale = 0;             ///< WMS MaxScaleDenominator, 0 when none
	QHash<QString, QRectF> bboxes;    ///< WMS BoundingBox per system (left/top = min x/y)
};


/**
 * The parsed capabilities document of a WMS 1.1.1, WMS 1.3.0 or WMTS 1.0.0 server.
 */
struct ServiceCapabilities
{
	MapService::Kind kind = MapService::Wms;
	QString version;
	QString title;
	QStringList formats;                      ///< WMS GetMap formats
	std::vector<ServiceLayer> layers;
	QHash<QString, QString> matrix_set_systems;  ///< WMTS: tile matrix set id to "EPSG:nnnn"
	QString error;                            ///< Non-empty when the document could not be understood

	static ServiceCapabilities parse(const QByteArray& document);

	const ServiceLayer* layer(const QString& id) const;

	/** "EPSG:nnnn" from an URN such as urn:ogc:def:crs:EPSG:6.18.3:3857, or the input. */
	static QString normalizeCrs(const QString& crs);
};


}  // namespace OpenOrienteering

#endif
