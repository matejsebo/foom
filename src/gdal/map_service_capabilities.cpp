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


#include "map_service_capabilities.h"

#include <cmath>

#include <QtGlobal>
#include <QCoreApplication>
#include <QLatin1String>
#include <QPointF>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

namespace OpenOrienteering {

namespace {

/// Appends the values which are not yet in the list.
void appendUnique(QStringList& list, const QStringList& values)
{
	for (auto const& value : values)
		if (!list.contains(value))
			list.append(value);
}

QRectF readBoundingBox(const QXmlStreamAttributes& attributes)
{
	auto const minx = attributes.value(QLatin1String("minx")).toDouble();
	auto const miny = attributes.value(QLatin1String("miny")).toDouble();
	auto const maxx = attributes.value(QLatin1String("maxx")).toDouble();
	auto const maxy = attributes.value(QLatin1String("maxy")).toDouble();
	return QRectF(QPointF(minx, miny), QPointF(maxx, maxy));
}

/// WMS 1.1.1 ScaleHint: the diagonal of a pixel in map units, at 0.28 mm per pixel.
double scaleFromHint(double hint)
{
	return hint / std::sqrt(2.0) / 0.00028;
}


/// Parses a WMS Layer element and its children recursively.
void parseWmsLayer(QXmlStreamReader& xml, const ServiceLayer& inherited, std::vector<ServiceLayer>& out)
{
	ServiceLayer layer = inherited;
	layer.id.clear();
	layer.title.clear();
	layer.min_scale = 0;
	layer.max_scale = 0;

	std::vector<ServiceLayer> children;
	while (xml.readNextStartElement())
	{
		auto const name = xml.name();
		if (name == QLatin1String("Name"))
			layer.id = xml.readElementText();
		else if (name == QLatin1String("Title"))
			layer.title = xml.readElementText();
		else if (name == QLatin1String("CRS") || name == QLatin1String("SRS"))
			appendUnique(layer.systems, xml.readElementText().simplified().split(QLatin1Char(' '), QString::SkipEmptyParts));
		else if (name == QLatin1String("BoundingBox"))
		{
			auto const attributes = xml.attributes();
			auto crs = attributes.value(QLatin1String("CRS")).toString();
			if (crs.isEmpty())
				crs = attributes.value(QLatin1String("SRS")).toString();
			if (!crs.isEmpty())
				layer.bboxes.insert(crs, readBoundingBox(attributes));
			xml.skipCurrentElement();
		}
		else if (name == QLatin1String("MinScaleDenominator"))
			layer.min_scale = xml.readElementText().toDouble();
		else if (name == QLatin1String("MaxScaleDenominator"))
			layer.max_scale = xml.readElementText().toDouble();
		else if (name == QLatin1String("ScaleHint"))
		{
			auto const attributes = xml.attributes();
			if (attributes.hasAttribute(QLatin1String("min")))
				layer.min_scale = scaleFromHint(attributes.value(QLatin1String("min")).toDouble());
			if (attributes.hasAttribute(QLatin1String("max")))
				layer.max_scale = scaleFromHint(attributes.value(QLatin1String("max")).toDouble());
			xml.skipCurrentElement();
		}
		else if (name == QLatin1String("Layer"))
		{
			// Children inherit what this layer has collected so far.
			parseWmsLayer(xml, layer, children);
		}
		else
			xml.skipCurrentElement();
	}

	if (!layer.id.isEmpty())
		out.push_back(layer);
	// Late-declared systems of the parent apply to the children too.
	for (auto& child : children)
	{
		appendUnique(child.systems, layer.systems);
		for (auto it = layer.bboxes.constBegin(); it != layer.bboxes.constEnd(); ++it)
			if (!child.bboxes.contains(it.key()))
				child.bboxes.insert(it.key(), it.value());
		out.push_back(child);
	}
}

void parseWms(QXmlStreamReader& xml, ServiceCapabilities& result)
{
	result.kind = MapService::Wms;
	result.version = xml.attributes().value(QLatin1String("version")).toString();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Service"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("Title"))
					result.title = xml.readElementText();
				else
					xml.skipCurrentElement();
			}
		}
		else if (xml.name() == QLatin1String("Capability"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("Request"))
				{
					while (xml.readNextStartElement())
					{
						if (xml.name() == QLatin1String("GetMap"))
						{
							while (xml.readNextStartElement())
							{
								if (xml.name() == QLatin1String("Format"))
									result.formats.append(xml.readElementText());
								else
									xml.skipCurrentElement();
							}
						}
						else
							xml.skipCurrentElement();
					}
				}
				else if (xml.name() == QLatin1String("Layer"))
					parseWmsLayer(xml, ServiceLayer{}, result.layers);
				else
					xml.skipCurrentElement();
			}
		}
		else
			xml.skipCurrentElement();
	}
}

void parseWmts(QXmlStreamReader& xml, ServiceCapabilities& result)
{
	result.kind = MapService::Wmts;
	result.version = xml.attributes().value(QLatin1String("version")).toString();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("ServiceIdentification"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("Title"))
					result.title = xml.readElementText();
				else
					xml.skipCurrentElement();
			}
		}
		else if (xml.name() == QLatin1String("Contents"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("Layer"))
				{
					ServiceLayer layer;
					while (xml.readNextStartElement())
					{
						auto const name = xml.name();
						if (name == QLatin1String("Identifier"))
							layer.id = xml.readElementText();
						else if (name == QLatin1String("Title"))
							layer.title = xml.readElementText();
						else if (name == QLatin1String("Format"))
							layer.formats.append(xml.readElementText());
						else if (name == QLatin1String("TileMatrixSetLink"))
						{
							while (xml.readNextStartElement())
							{
								if (xml.name() == QLatin1String("TileMatrixSet"))
									layer.matrix_sets.append(xml.readElementText());
								else
									xml.skipCurrentElement();
							}
						}
						else
							xml.skipCurrentElement();
					}
					if (!layer.id.isEmpty())
						result.layers.push_back(layer);
				}
				else if (xml.name() == QLatin1String("TileMatrixSet"))
				{
					QString id;
					QString crs;
					while (xml.readNextStartElement())
					{
						if (xml.name() == QLatin1String("Identifier"))
							id = xml.readElementText();
						else if (xml.name() == QLatin1String("SupportedCRS"))
							crs = ServiceCapabilities::normalizeCrs(xml.readElementText());
						else
							xml.skipCurrentElement();
					}
					if (!id.isEmpty())
						result.matrix_set_systems.insert(id, crs);
				}
				else
					xml.skipCurrentElement();
			}
		}
		else
			xml.skipCurrentElement();
	}
	for (auto& layer : result.layers)
	{
		for (auto const& set : layer.matrix_sets)
		{
			auto const crs = result.matrix_set_systems.value(set);
			if (!crs.isEmpty() && !layer.systems.contains(crs))
				layer.systems.append(crs);
		}
	}
}

}  // namespace


// static
ServiceCapabilities ServiceCapabilities::parse(const QByteArray& document)
{
	ServiceCapabilities result;
	QXmlStreamReader xml(document);
	if (!xml.readNextStartElement())
	{
		result.error = QCoreApplication::translate("OpenOrienteering::MapServiceDialog", "Not an XML document: %1").arg(xml.errorString());
		return result;
	}

	auto const root = xml.name();
	if (root == QLatin1String("WMS_Capabilities") || root == QLatin1String("WMT_MS_Capabilities"))
		parseWms(xml, result);
	else if (root == QLatin1String("Capabilities"))
		parseWmts(xml, result);
	else if (root == QLatin1String("ServiceExceptionReport") || root == QLatin1String("ExceptionReport"))
		result.error = QCoreApplication::translate("OpenOrienteering::MapServiceDialog", "The server reported an error: %1").arg(QString::fromUtf8(document).simplified().left(300));
	else
		result.error = QCoreApplication::translate("OpenOrienteering::MapServiceDialog", "Not a WMS or WMTS capabilities document (root element %1).").arg(root.toString());

	if (xml.hasError() && result.error.isEmpty())
		result.error = xml.errorString();
	return result;
}

const ServiceLayer* ServiceCapabilities::layer(const QString& id) const
{
	for (auto const& candidate : layers)
		if (candidate.id == id)
			return &candidate;
	return nullptr;
}

// static
QString ServiceCapabilities::normalizeCrs(const QString& crs)
{
	// urn:ogc:def:crs:EPSG:6.18.3:3857, urn:ogc:def:crs:EPSG::3857, http://www.opengis.net/def/crs/EPSG/0/3857
	auto const trimmed = crs.trimmed();
	if (trimmed.startsWith(QLatin1String("urn:"), Qt::CaseInsensitive))
	{
		auto const parts = trimmed.split(QLatin1Char(':'));
		if (parts.size() >= 7)
			return parts[4].toUpper() + QLatin1Char(':') + parts.last();
	}
	else if (trimmed.contains(QLatin1String("/def/crs/")))
	{
		auto const parts = trimmed.split(QLatin1Char('/'));
		if (parts.size() >= 3)
			return parts[parts.size() - 3].toUpper() + QLatin1Char(':') + parts.last();
	}
	return trimmed;
}


}  // namespace OpenOrienteering
