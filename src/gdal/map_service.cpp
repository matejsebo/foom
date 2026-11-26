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


#include "map_service.h"

#include <algorithm>
#include <cmath>

#include <QtGlobal>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QLatin1String>
#include <QPointF>
#include <QSettings>
#include <QStringList>
#include <QStringRef>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "mapper_config.h"

namespace OpenOrienteering {

namespace {

constexpr double mercator_half = 20037508.342789244;
constexpr double metres_per_degree = 111319.49079327358;

namespace literal {
	static const QLatin1String service("service");
	static const QLatin1String kind("kind");
	static const QLatin1String url("url");
	static const QLatin1String version("version");
	static const QLatin1String layers("layers");
	static const QLatin1String styles("styles");
	static const QLatin1String format("format");
	static const QLatin1String crs("crs");
	static const QLatin1String bbox("bbox");
	static const QLatin1String resolution("resolution");
	static const QLatin1String matrix_set("matrix_set");
	static const QLatin1String min_zoom("min_zoom");
	static const QLatin1String max_zoom("max_zoom");
	static const QLatin1String attribution("attribution");
	static const QLatin1String bulk_download("bulk_download");
}

bool isGeographic(const QString& crs)
{
	return crs.compare(QLatin1String("EPSG:4326"), Qt::CaseInsensitive) == 0
	       || crs.compare(QLatin1String("CRS:84"), Qt::CaseInsensitive) == 0;
}

QString bboxToString(const QRectF& bbox)
{
	return QString::number(bbox.left(), 'g', 15) + QLatin1Char(',')
	       + QString::number(bbox.top(), 'g', 15) + QLatin1Char(',')
	       + QString::number(bbox.right(), 'g', 15) + QLatin1Char(',')
	       + QString::number(bbox.bottom(), 'g', 15);
}

QRectF bboxFromString(const QString& text)
{
	auto const parts = text.split(QLatin1Char(','));
	if (parts.size() != 4)
		return {};
	bool ok[4];
	auto const minx = parts[0].toDouble(&ok[0]);
	auto const miny = parts[1].toDouble(&ok[1]);
	auto const maxx = parts[2].toDouble(&ok[2]);
	auto const maxy = parts[3].toDouble(&ok[3]);
	if (!(ok[0] && ok[1] && ok[2] && ok[3]))
		return {};
	return QRectF(QPointF(minx, miny), QPointF(maxx, maxy));
}

void writeElement(QXmlStreamWriter& xml, const char* name, const QString& value)
{
	xml.writeTextElement(QString::fromLatin1(name), value);
}

}  // namespace


bool MapService::operator==(const MapService& other) const
{
	return kind == other.kind
	       && name == other.name
	       && url == other.url
	       && version == other.version
	       && layers == other.layers
	       && styles == other.styles
	       && format == other.format
	       && crs == other.crs
	       && bbox == other.bbox
	       && qFuzzyCompare(1.0 + resolution, 1.0 + other.resolution)
	       && matrix_set == other.matrix_set
	       && min_zoom == other.min_zoom
	       && max_zoom == other.max_zoom
	       && attribution == other.attribution
	       && bulk_download == other.bulk_download;
}


// static
QString MapService::kindName(Kind kind)
{
	switch (kind)
	{
	case Wms:  return QStringLiteral("wms");
	case Wmts: return QStringLiteral("wmts");
	case Xyz:  return QStringLiteral("xyz");
	}
	return QStringLiteral("xyz");
}

// static
MapService::Kind MapService::kindFromName(const QString& name, bool* ok)
{
	if (ok)
		*ok = true;
	if (name == QLatin1String("wms"))
		return Wms;
	if (name == QLatin1String("wmts"))
		return Wmts;
	if (name == QLatin1String("xyz"))
		return Xyz;
	if (ok)
		*ok = false;
	return Xyz;
}


void MapService::save(QXmlStreamWriter& xml) const
{
	xml.writeEmptyElement(literal::service);
	xml.writeAttribute(literal::kind, kindName(kind));
	xml.writeAttribute(literal::url, url);
	xml.writeAttribute(literal::version, version);
	xml.writeAttribute(literal::layers, layers);
	xml.writeAttribute(literal::styles, styles);
	xml.writeAttribute(literal::format, format);
	xml.writeAttribute(literal::crs, crs);
	if (!bbox.isNull())
		xml.writeAttribute(literal::bbox, bboxToString(bbox));
	if (resolution > 0)
		xml.writeAttribute(literal::resolution, QString::number(resolution, 'g', 12));
	xml.writeAttribute(literal::matrix_set, matrix_set);
	xml.writeAttribute(literal::min_zoom, QString::number(min_zoom));
	xml.writeAttribute(literal::max_zoom, QString::number(max_zoom));
	if (!attribution.isEmpty())
		xml.writeAttribute(literal::attribution, attribution);
	xml.writeAttribute(literal::bulk_download, QLatin1String(bulk_download ? "true" : "false"));
}

// static
MapService MapService::load(QXmlStreamReader& xml)
{
	Q_ASSERT(xml.name() == literal::service);

	MapService service;
	auto const attributes = xml.attributes();
	service.kind = kindFromName(attributes.value(literal::kind).toString());
	service.url = attributes.value(literal::url).toString();
	if (attributes.hasAttribute(literal::version))
		service.version = attributes.value(literal::version).toString();
	service.layers = attributes.value(literal::layers).toString();
	service.styles = attributes.value(literal::styles).toString();
	if (attributes.hasAttribute(literal::format))
		service.format = attributes.value(literal::format).toString();
	if (attributes.hasAttribute(literal::crs))
		service.crs = attributes.value(literal::crs).toString();
	service.bbox = bboxFromString(attributes.value(literal::bbox).toString());
	service.resolution = attributes.value(literal::resolution).toDouble();
	service.matrix_set = attributes.value(literal::matrix_set).toString();
	if (attributes.hasAttribute(literal::min_zoom))
		service.min_zoom = attributes.value(literal::min_zoom).toInt();
	if (attributes.hasAttribute(literal::max_zoom))
		service.max_zoom = attributes.value(literal::max_zoom).toInt();
	service.attribution = attributes.value(literal::attribution).toString();
	if (attributes.hasAttribute(literal::bulk_download))
		service.bulk_download = attributes.value(literal::bulk_download) == QLatin1String("true");
	xml.skipCurrentElement();
	return service;
}


QString MapService::effectiveCrs() const
{
	return kind == Xyz ? QStringLiteral("EPSG:3857") : crs;
}

QString MapService::normalizedXyzUrl() const
{
	auto result = url;
	result.replace(QLatin1String("{z}"), QLatin1String("${z}"));
	result.replace(QLatin1String("{x}"), QLatin1String("${x}"));
	result.replace(QLatin1String("{y}"), QLatin1String("${y}"));
	result.replace(QLatin1String("$${"), QLatin1String("${"));
	return result;
}

QByteArray MapService::gdalSource(const QString& cache_dir) const
{
	if (kind == Wmts)
	{
		QString connection = QStringLiteral("WMTS:") + url;
		if (!layers.isEmpty())
			connection += QStringLiteral(",layer=") + layers;
		if (!matrix_set.isEmpty())
			connection += QStringLiteral(",tilematrixset=") + matrix_set;
		return connection.toUtf8();
	}

	QString result;
	QXmlStreamWriter xml(&result);
	xml.writeStartElement(QStringLiteral("GDAL_WMS"));
	xml.writeStartElement(QStringLiteral("Service"));
	if (kind == Xyz)
	{
		xml.writeAttribute(QStringLiteral("name"), QStringLiteral("TMS"));
		writeElement(xml, "ServerUrl", normalizedXyzUrl());
		xml.writeEndElement();  // Service

		xml.writeStartElement(QStringLiteral("DataWindow"));
		writeElement(xml, "UpperLeftX", QString::number(-mercator_half, 'f', 9));
		writeElement(xml, "UpperLeftY", QString::number(mercator_half, 'f', 9));
		writeElement(xml, "LowerRightX", QString::number(mercator_half, 'f', 9));
		writeElement(xml, "LowerRightY", QString::number(-mercator_half, 'f', 9));
		writeElement(xml, "TileLevel", QString::number(qMax(0, max_zoom)));
		writeElement(xml, "TileCountX", QStringLiteral("1"));
		writeElement(xml, "TileCountY", QStringLiteral("1"));
		writeElement(xml, "YOrigin", QStringLiteral("top"));
		xml.writeEndElement();  // DataWindow

		writeElement(xml, "Projection", QStringLiteral("EPSG:3857"));
		writeElement(xml, "BlockSizeX", QStringLiteral("256"));
		writeElement(xml, "BlockSizeY", QStringLiteral("256"));
		writeElement(xml, "OverviewCount", QString::number(qBound(0, max_zoom - min_zoom, max_zoom)));
	}
	else
	{
		auto const geographic = isGeographic(crs);
		auto const v130 = !version.startsWith(QLatin1String("1.1"));
		xml.writeAttribute(QStringLiteral("name"), QStringLiteral("WMS"));
		writeElement(xml, "Version", version);
		writeElement(xml, "ServerUrl", url);
		writeElement(xml, v130 ? "CRS" : "SRS", crs);
		writeElement(xml, "ImageFormat", format);
		writeElement(xml, "Layers", layers);
		writeElement(xml, "Styles", styles);
		writeElement(xml, "Transparent", QStringLiteral("TRUE"));
		if (v130 && geographic)
			writeElement(xml, "BBoxOrder", QStringLiteral("yxYX"));
		xml.writeEndElement();  // Service

		auto window = bbox;
		if (window.isNull())
		{
			window = geographic ? QRectF(QPointF(-180, -90), QPointF(180, 90))
			                    : QRectF(QPointF(-mercator_half, -mercator_half), QPointF(mercator_half, mercator_half));
		}
		auto res = resolution > 0 ? resolution : (geographic ? 0.1 / metres_per_degree : 0.1);
		// Keep the sizes below 2^30 pixels
		constexpr double max_size = 1073741824.0;
		res = std::max(res, std::max(window.width(), window.height()) / max_size);
		auto const size_x = qMax(1, int(std::ceil(window.width() / res)));
		auto const size_y = qMax(1, int(std::ceil(window.height() / res)));

		xml.writeStartElement(QStringLiteral("DataWindow"));
		writeElement(xml, "UpperLeftX", QString::number(window.left(), 'g', 15));
		writeElement(xml, "UpperLeftY", QString::number(window.bottom(), 'g', 15));
		writeElement(xml, "LowerRightX", QString::number(window.right(), 'g', 15));
		writeElement(xml, "LowerRightY", QString::number(window.top(), 'g', 15));
		writeElement(xml, "SizeX", QString::number(size_x));
		writeElement(xml, "SizeY", QString::number(size_y));
		xml.writeEndElement();  // DataWindow

		writeElement(xml, "Projection", crs);
		writeElement(xml, "BlockSizeX", QStringLiteral("1024"));
		writeElement(xml, "BlockSizeY", QStringLiteral("1024"));
		auto const overviews = int(std::floor(std::log2(std::max(size_x, size_y) / 1024.0)));
		writeElement(xml, "OverviewCount", QString::number(qMax(0, overviews)));
	}

	writeElement(xml, "BandsCount", QStringLiteral("4"));
	// foom: a WMS draws every picture when it is asked for, and the image services of The National Map took up to
	// 24 s for one of 1024 pixels on 2026-11-24; a tile cache answers at once or not at all.
	writeElement(xml, "Timeout", kind == Xyz ? QStringLiteral("10") : QStringLiteral("60"));
	writeElement(xml, "UserAgent", QString::fromLatin1(userAgent()));
	writeElement(xml, "ZeroBlockHttpCodes", QStringLiteral("204,404"));
	writeElement(xml, "ZeroBlockOnServerException", QStringLiteral("true"));
	if (!user.isEmpty())
		writeElement(xml, "UserPwd", user + QLatin1Char(':') + password);
	if (!cache_dir.isEmpty())
	{
		xml.writeStartElement(QStringLiteral("Cache"));
		writeElement(xml, "Path", cache_dir);
		xml.writeEndElement();  // Cache
	}
	xml.writeEndElement();  // GDAL_WMS
	return result.toUtf8();
}


namespace {

/// foom: Counts the changes of the shipped presets which a saved list takes over: 2 since the USGS services of 2026-11-24.
constexpr int map_service_presets_generation = 2;

/**
 * foom: The WMS services of the U.S. Geological Survey's National Map, checked against the servers on
 * 2026-11-24: the aerial photographs of the National Agriculture Imagery Program, and the hillshade and
 * the slope map which the 3D Elevation Program's service makes from its elevation models.
 * See the design notes, "USGS NAIP imagery and 3DEP relief as map services".
 */
std::vector<MapService> usgsWmsPresets()
{
	std::vector<MapService> result;
	auto wms = [&result](const char* name, const char* url, const char* layer, const char* format, double resolution, const char* attribution) {
		MapService service;
		service.kind = MapService::Wms;
		service.name = QString::fromUtf8(name);
		service.url = QString::fromUtf8(url);
		service.layers = QString::fromUtf8(layer);
		service.format = QString::fromUtf8(format);
		service.resolution = resolution;
		service.attribution = QString::fromUtf8(attribution);
		result.push_back(service);
	};
	wms("USGS NAIP imagery", "https://imagery.nationalmap.gov/arcgis/services/USGSNAIPImagery/ImageServer/WMSServer?",
	    "USGSNAIPImagery:NaturalColor", "image/jpeg", 0.3, "USGS, USDA, The National Map: Orthoimagery");
	auto const elevation = "https://elevation.nationalmap.gov/arcgis/services/3DEPElevation/ImageServer/WMSServer?";
	wms("USGS 3DEP hillshade", elevation, "3DEPElevation:Hillshade Multidirectional", "image/png", 1.0, "USGS 3D Elevation Program (3DEP)");
	wms("USGS 3DEP slope", elevation, "3DEPElevation:Slope Map", "image/png", 1.0, "USGS 3D Elevation Program (3DEP)");
	return result;
}

}  // namespace


// static
std::vector<MapService> MapService::presets()
{
	std::vector<MapService> result;

	MapService osm;
	osm.kind = Xyz;
	osm.name = QStringLiteral("OpenStreetMap");
	osm.url = QStringLiteral("https://tile.openstreetmap.org/${z}/${x}/${y}.png");
	osm.max_zoom = 19;
	osm.attribution = QStringLiteral("© OpenStreetMap contributors");
	osm.bulk_download = false;
	result.push_back(osm);

	MapService topo;
	topo.kind = Xyz;
	topo.name = QStringLiteral("OpenTopoMap");
	topo.url = QStringLiteral("https://tile.opentopomap.org/${z}/${x}/${y}.png");
	topo.max_zoom = 17;
	topo.attribution = QStringLiteral("© OpenStreetMap contributors, SRTM | © OpenTopoMap (CC-BY-SA)");
	topo.bulk_download = false;
	result.push_back(topo);

	for (auto const& service : usgsWmsPresets())
		result.push_back(service);  // foom

	return result;
}

// static
std::vector<MapService> MapService::onlinePresets()
{
	// foom: the design notes, "Online map and geoviewer";
	// every one checked against its server on 2026-11-23.
	std::vector<MapService> result;
	auto xyz = [&result](const char* name, const char* url, int max_zoom, const char* attribution, bool bulk) {
		MapService service;
		service.kind = Xyz;
		service.name = QString::fromUtf8(name);
		service.url = QString::fromUtf8(url);
		service.max_zoom = max_zoom;
		service.attribution = QString::fromUtf8(attribution);
		service.bulk_download = bulk;
		result.push_back(service);
	};
	xyz("USGS Imagery", "https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/${z}/${y}/${x}",
	    16, "USGS The National Map", true);
	xyz("USGS Topo", "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/${z}/${y}/${x}",
	    16, "USGS The National Map", true);
	for (auto const& service : usgsWmsPresets())
		result.push_back(service);
	for (auto const& preset : presets())
	{
		if (preset.kind == Xyz)
			result.push_back(preset);  // OpenStreetMap, OpenTopoMap
	}
	return result;
}

// static
std::vector<MapService> MapService::onlineMapServices()
{
	auto result = onlinePresets();
	for (auto const& service : fromSettings())
	{
		auto const known = std::any_of(result.begin(), result.end(), [&service](const MapService& s) { return s.name == service.name; });
		if (!known)
			result.push_back(service);
	}
	return result;
}

// static
std::vector<MapService> MapService::fromSettings()
{
	QSettings settings;
	auto const count = settings.beginReadArray(QStringLiteral("MapServices"));
	if (count == 0)
	{
		settings.endArray();
		return presets();
	}
	std::vector<MapService> result;
	result.reserve(std::size_t(count));
	for (int i = 0; i < count; ++i)
	{
		settings.setArrayIndex(i);
		MapService service;
		service.kind = kindFromName(settings.value(QStringLiteral("kind")).toString());
		service.name = settings.value(QStringLiteral("name")).toString();
		service.url = settings.value(QStringLiteral("url")).toString();
		service.version = settings.value(QStringLiteral("version"), service.version).toString();
		service.layers = settings.value(QStringLiteral("layers")).toString();
		service.styles = settings.value(QStringLiteral("styles")).toString();
		service.format = settings.value(QStringLiteral("format"), service.format).toString();
		service.crs = settings.value(QStringLiteral("crs"), service.crs).toString();
		service.bbox = bboxFromString(settings.value(QStringLiteral("bbox")).toString());
		service.resolution = settings.value(QStringLiteral("resolution")).toDouble();
		service.matrix_set = settings.value(QStringLiteral("matrix_set")).toString();
		service.min_zoom = settings.value(QStringLiteral("min_zoom"), service.min_zoom).toInt();
		service.max_zoom = settings.value(QStringLiteral("max_zoom"), service.max_zoom).toInt();
		service.attribution = settings.value(QStringLiteral("attribution")).toString();
		service.bulk_download = settings.value(QStringLiteral("bulk_download"), true).toBool();
		result.push_back(service);
	}
	settings.endArray();
	// foom: a list which was saved before the services of the U.S. Geological Survey were shipped gets them once,
	// behind its own; a user who takes them out again keeps the list as it is.
	if (settings.value(QStringLiteral("MapServicePresets"), 1).toInt() < map_service_presets_generation)
	{
		for (auto const& preset : usgsWmsPresets())
		{
			auto const known = std::any_of(result.begin(), result.end(), [&preset](const MapService& s) { return s.name == preset.name; });
			if (!known)
				result.push_back(preset);
		}
		toSettings(result);
	}
	return result;
}

// static
QByteArray MapService::userAgent()
{
	return QByteArrayLiteral("FOOM/" APP_VERSION " (+https://github.com/matejsebo/foom)");
}


// static
void MapService::toSettings(const std::vector<MapService>& services)
{
	QSettings settings;
	settings.setValue(QStringLiteral("MapServicePresets"), map_service_presets_generation);  // foom
	settings.remove(QStringLiteral("MapServices"));
	settings.beginWriteArray(QStringLiteral("MapServices"), int(services.size()));
	for (std::size_t i = 0; i < services.size(); ++i)
	{
		auto const& service = services[i];
		settings.setArrayIndex(int(i));
		settings.setValue(QStringLiteral("kind"), kindName(service.kind));
		settings.setValue(QStringLiteral("name"), service.name);
		settings.setValue(QStringLiteral("url"), service.url);
		settings.setValue(QStringLiteral("version"), service.version);
		settings.setValue(QStringLiteral("layers"), service.layers);
		settings.setValue(QStringLiteral("styles"), service.styles);
		settings.setValue(QStringLiteral("format"), service.format);
		settings.setValue(QStringLiteral("crs"), service.crs);
		settings.setValue(QStringLiteral("bbox"), service.bbox.isNull() ? QString{} : bboxToString(service.bbox));
		settings.setValue(QStringLiteral("resolution"), service.resolution);
		settings.setValue(QStringLiteral("matrix_set"), service.matrix_set);
		settings.setValue(QStringLiteral("min_zoom"), service.min_zoom);
		settings.setValue(QStringLiteral("max_zoom"), service.max_zoom);
		settings.setValue(QStringLiteral("attribution"), service.attribution);
		settings.setValue(QStringLiteral("bulk_download"), service.bulk_download);
	}
	settings.endArray();
}


// static
bool MapService::isDescriptorPath(const QString& path)
{
	auto const suffix = descriptorSuffix();
	return path.size() > suffix.size()
	       && path.endsWith(suffix, Qt::CaseInsensitive)
	       && path[path.size() - suffix.size() - 1] != QLatin1Char('/')
	       && path[path.size() - suffix.size() - 1] != QLatin1Char('\\');
}

bool MapService::saveDescriptor(const QString& path, QString* error) const
{
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly))
	{
		QXmlStreamWriter xml(&file);
		xml.setAutoFormatting(true);
		xml.writeStartDocument();
		xml.writeStartElement(QStringLiteral("map_service"));
		xml.writeAttribute(QStringLiteral("name"), name);
		save(xml);
		xml.writeEndElement();
		xml.writeEndDocument();
		if (!xml.hasError() && file.commit())
			return true;
	}
	if (error)
		*error = QCoreApplication::translate("OpenOrienteering::MapService", "The file %1 cannot be written.").arg(path);
	return false;
}

// static
bool MapService::loadDescriptor(const QString& path, MapService& service, QString* error)
{
	auto const fail = [error, &path](const QString& message) {
		if (error)
			*error = message.arg(QFileInfo(path).fileName());
		return false;
	};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return fail(QCoreApplication::translate("OpenOrienteering::MapService", "The file %1 cannot be read."));
	QXmlStreamReader xml(&file);
	if (!xml.readNextStartElement() || xml.name() != QLatin1String("map_service"))
		return fail(QCoreApplication::translate("OpenOrienteering::MapService", "The file %1 is no map service descriptor."));
	auto const service_name = xml.attributes().value(QLatin1String("name")).toString();
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::service)
		{
			service = load(xml);
			service.name = service_name;
			return true;
		}
		xml.skipCurrentElement();
	}
	return fail(QCoreApplication::translate("OpenOrienteering::MapService", "The file %1 holds no service."));
}


}  // namespace OpenOrienteering
