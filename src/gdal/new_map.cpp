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


#include "new_map.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <QtMath>
#include <QDir>
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QLatin1String>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <cpl_conv.h>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "core/symbols/symbol.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/point_cloud.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gdal/geodata_import.h"
#include "gdal/map_service.h"
#include "gdal/tile_service_template.h"
#include "templates/template.h"
#include "undo/undo_manager.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::NewMap", text);
}

QString number(double value)
{
	return QString::number(value, 'g', 10);
}

/// The CRT file for OpenStreetMap data and the map's symbol set, or an empty string.
QString defaultOsmCrt(const Map& map)
{
	auto const id = map.symbolSetId();
	if (id.isEmpty())
		return {};
	for (auto const* prefix : { "data:/symbol sets/OSM-", "data:/symbol sets/COPY_OF_OSM-" })
	{
		QString const path = QLatin1String(prefix) + id + QLatin1String(".crt");
		if (QFileInfo::exists(path))
			return path;
	}
	return {};
}

/// Loads the symbol set as the main window's New dialog does.
bool loadSymbolSet(Map& map, const NewMapParams& params, NewMapResult& result)
{
	if (params.symbol_set_path.isEmpty())
	{
		map.setScaleDenominator(params.scale > 0 ? params.scale : 10000);
		return true;
	}
	auto importer = FileFormats.makeImporter(params.symbol_set_path, map, nullptr);
	if (!importer)
	{
		result.error = tr("Cannot open file:\n%1").arg(params.symbol_set_path);
		return false;
	}
	importer->setLoadSymbolsOnly(true);
	if (!importer->doImport())
	{
		result.error = importer->warnings().empty()
		               ? tr("Cannot open file:\n%1").arg(params.symbol_set_path)
		               : importer->warnings().back();
		return false;
	}
	for (auto const& warning : importer->warnings())
		result.warnings.push_back(warning);
	
	if (params.scale > 0 && map.getScaleDenominator() != params.scale)
	{
		if (params.scale_symbols)
			map.scaleAllSymbols(double(map.getScaleDenominator()) / params.scale);
		map.setScaleDenominator(params.scale);
	}
	for (int i = map.getNumSymbols(); i > 0; i = qMin(i, map.getNumSymbols()))
	{
		--i;
		auto* symbol = map.getSymbol(i);
		if (symbol->isHidden() && !map.existsObjectWithSymbol(symbol))
			map.deleteSymbol(i);
	}
	return true;
}

bool setupGeoreferencing(Map& map, const NewMapParams& params, NewMapResult& result)
{
	auto const centre = params.box.centre();
	Georeferencing georef;
	georef.setScaleDenominator(int(map.getScaleDenominator()));
	auto ok = false;
	if (params.crs_spec.isEmpty() || params.crs_spec.startsWith(QLatin1String("EPSG:"), Qt::CaseInsensitive))
	{
		auto const code = params.crs_spec.isEmpty() ? newMapUtmEpsg(centre) : params.crs_spec.mid(5).trimmed().toInt();
		ok = code > 0 && georef.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("+init=epsg:%1").arg(code), { QString::number(code) });
	}
	else
	{
		ok = georef.setProjectedCRS(QStringLiteral("PROJ.4"), params.crs_spec, { params.crs_spec });
	}
	if (!ok || georef.getState() != Georeferencing::Geospatial)
	{
		result.error = tr("Unknown coordinate reference system: %1").arg(params.crs_spec);
		return false;
	}
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGeographicRefPoint(centre);
	if (params.rotate_to_magnetic)
		georef.setDeclination(params.declination);
	else
		georef.setGrivation(0);
	map.setGeoreferencing(georef);
	return true;
}

const MapService* findService(const std::vector<MapService>& services, const QString& name)
{
	for (auto const& service : services)
	{
		if (service.name == name)
			return &service;
	}
	return nullptr;
}

}  // namespace



int newMapUtmEpsg(const LatLon& lat_lon)
{
	auto const zone = qBound(1, int(std::floor((lat_lon.longitude() + 180.0) / 6.0)) + 1, 60);
	return (lat_lon.latitude() >= 0 ? 32600 : 32700) + zone;
}


double newMapAreaKm2(const GeoBox& box)
{
	constexpr double radius_km = 6371.0088;
	auto const d_lon = qDegreesToRadians(box.east - box.west);
	auto const d_sin = std::sin(qDegreesToRadians(box.north)) - std::sin(qDegreesToRadians(box.south));
	return std::abs(radius_km * radius_km * d_lon * d_sin);
}


QString newMapOverpassQuery(const GeoBox& box)
{
	return QLatin1String("[out:xml][timeout:90];(nwr(")
	       + number(box.south) + QLatin1Char(',') + number(box.west) + QLatin1Char(',')
	       + number(box.north) + QLatin1Char(',') + number(box.east)
	       + QLatin1String(");>;);out body;");
}


QString newMapDefaultEndpoint()
{
	QSettings settings;
	return settings.value(QStringLiteral("NewMap/overpass_endpoint"), QStringLiteral("https://overpass-api.de/api/interpreter")).toString();
}


QPolygonF newMapBoxPolygon(const Map& map, const GeoBox& box)
{
	auto const& georef = map.getGeoreferencing();
	QPolygonF polygon;
	polygon << QPointF(georef.toMapCoordF(LatLon(box.south, box.west)))
	        << QPointF(georef.toMapCoordF(LatLon(box.south, box.east)))
	        << QPointF(georef.toMapCoordF(LatLon(box.north, box.east)))
	        << QPointF(georef.toMapCoordF(LatLon(box.north, box.west)));
	return polygon;
}


QRectF newMapProjectedArea(const Map& map, const GeoBox& box)
{
	auto const& georef = map.getGeoreferencing();
	constexpr int samples = 16;
	auto min_x = std::numeric_limits<double>::max(), min_y = min_x;
	auto max_x = std::numeric_limits<double>::lowest(), max_y = max_x;
	auto const add = [&](double lat, double lon) {
		auto const p = georef.toProjectedCoords(LatLon(lat, lon));
		min_x = std::min(min_x, p.x());
		min_y = std::min(min_y, p.y());
		max_x = std::max(max_x, p.x());
		max_y = std::max(max_y, p.y());
	};
	for (int i = 0; i <= samples; ++i)
	{
		auto const t = double(i) / samples;
		auto const lat = box.south + t * (box.north - box.south);
		auto const lon = box.west + t * (box.east - box.west);
		add(box.south, lon);
		add(box.north, lon);
		add(lat, box.west);
		add(lat, box.east);
	}
	return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
}


bool downloadOverpass(const QString& endpoint, const GeoBox& box, const QString& out_path, int retry_delay_ms, QString* error)
{
	QNetworkAccessManager network;
	QByteArray const body = "data=" + QUrl::toPercentEncoding(newMapOverpassQuery(box));
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		if (attempt > 0)
		{
			QEventLoop wait;
			QTimer::singleShot(retry_delay_ms, &wait, &QEventLoop::quit);
			wait.exec();
		}
		
		QNetworkRequest request{ QUrl(endpoint) };
		request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
		request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("FOOM"));
		std::unique_ptr<QNetworkReply> reply(network.post(request, body));
		QEventLoop loop;
		QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QTimer timeout;
		timeout.setSingleShot(true);
		QObject::connect(&timeout, &QTimer::timeout, reply.get(), &QNetworkReply::abort);
		timeout.start(180000);
		loop.exec();
		
		auto const status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError && status == 200)
		{
			QSaveFile file(out_path);
			if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0 || !file.commit())
			{
				if (error)
					*error = tr("Cannot save the OpenStreetMap data:\n%1\n\n%2").arg(out_path, file.errorString());
				return false;
			}
			return true;
		}
		auto const busy = status == 429 || status == 504;
		if (busy && attempt == 0)
			continue;
		if (error)
		{
			*error = busy
			         ? tr("The OpenStreetMap server is busy (HTTP %1). Try again later, or open a downloaded file instead.").arg(status)
			         : tr("Cannot download the OpenStreetMap data: %1").arg(reply->errorString());
		}
		return false;
	}
	return false;
}


namespace {

/// The path beside the map: <map dir>/<map base>_<suffix>.tif
QString besideMap(const QString& map_path, const QString& suffix)
{
	QFileInfo const info(map_path);
	return info.path() + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('_') + suffix + QLatin1String(".tif");
}

/// A grid in the map's system, cropped to the area, or nullptr with the error set.
std::unique_ptr<ElevationModel> fitToArea(std::unique_ptr<ElevationModel> grid, const QRectF& area, const QString& source, NewMapResult& result)
{
	if (!grid)
	{
		result.error = tr("Cannot read the elevation data %1:\n%2").arg(source, ElevationModel::lastError());
		return nullptr;
	}
	auto const extent = grid->extent();
	if (!extent.intersects(area))
	{
		result.error = tr("The elevation data %1 does not overlap the area of the map.").arg(source);
		return nullptr;
	}
	if (!extent.contains(area))
		result.warnings.push_back(tr("The elevation data %1 covers only part of the area of the map.").arg(source));
	return grid->cropped(area);
}

/**
 * Adds the elevation grids of params.elevation_path to the map, saved beside it.
 *
 * See the design notes, "Elevation on the data page".
 */
bool addElevation(Map& map, const NewMapParams& params, NewMapResult& result)
{
	auto const& source = params.elevation_path;
	if (params.map_path.isEmpty())
	{
		result.error = tr("Choose where to save the map before adding elevation data.");
		return false;
	}
	auto const area = newMapProjectedArea(map, params.box);
	auto const target_crs = map.getGeoreferencing().getProjectedCRSSpec();
	
	// The grids to save, made completely before any file is written.
	std::vector<std::pair<ElevationSet::Kind, std::unique_ptr<ElevationModel>>> grids;
	QStringList suffixes;
	auto const suffix = QFileInfo(source).suffix().toLower();
	if (suffix == QLatin1String("las") || suffix == QLatin1String("laz"))
	{
		auto const info = PointCloud::analyse({ source });
		if (!info.error.isEmpty())
		{
			result.error = tr("Cannot read the elevation data %1:\n%2").arg(source, info.error);
			return false;
		}
		auto const same_system = info.crs_spec.isEmpty() || target_crs.isEmpty() || ElevationModel::sameCrs(info.crs_spec, target_crs);
		GriddingParams gridding;
		gridding.cell = params.elevation_cell > 0 ? params.elevation_cell : 1.0;
		gridding.dtm = true;
		gridding.dsm = true;
		if (same_system)
		{
			if (!info.extent.intersects(area))
			{
				result.error = tr("The elevation data %1 does not overlap the area of the map.").arg(source);
				return false;
			}
			if (!info.extent.contains(area))
				result.warnings.push_back(tr("The elevation data %1 covers only part of the area of the map.").arg(source));
			gridding.box = area;
		}
		auto gridded = PointCloud::grid({ source }, gridding);
		if (!gridded.error.isEmpty())
		{
			result.error = tr("Cannot read the elevation data %1:\n%2").arg(source, gridded.error);
			return false;
		}
		QTemporaryDir temp;
		auto const reproject = [&](std::unique_ptr<ElevationModel> grid, const QString& name) -> std::unique_ptr<ElevationModel> {
			if (same_system)
			{
				if (grid && grid->crsSpec().isEmpty())
					grid->setCrsSpec(target_crs);
				return grid;
			}
			// Warped into the map's system through a file, as an imported grid is.
			auto const path = temp.filePath(name + QLatin1String(".tif"));
			if (!grid || !temp.isValid() || !grid->save(path))
				return fitToArea(nullptr, area, source, result);
			ElevationModel::OpenOptions options;
			options.target_crs = target_crs;
			return fitToArea(ElevationModel::open(path, options), area, source, result);
		};
		auto dtm = reproject(std::move(gridded.dtm), QStringLiteral("DTM"));
		if (!dtm)
			return false;
		auto dsm = reproject(std::move(gridded.dsm), QStringLiteral("DSM"));
		if (!dsm)
			return false;
		grids.emplace_back(ElevationSet::Terrain, std::move(dtm));
		suffixes.push_back(QStringLiteral("DTM"));
		grids.emplace_back(ElevationSet::Surface, std::move(dsm));
		suffixes.push_back(QStringLiteral("DSM"));
	}
	else
	{
		ElevationModel::OpenOptions options;
		options.target_crs = target_crs;
		auto grid = fitToArea(ElevationModel::open(source, options), area, source, result);
		if (!grid)
			return false;
		grids.emplace_back(ElevationSet::Terrain, std::move(grid));
		suffixes.push_back(ElevationSet::kindName(ElevationSet::Terrain));
	}
	
	for (std::size_t i = 0; i < grids.size(); ++i)
	{
		auto const href = besideMap(params.map_path, suffixes[int(i)]);
		if (!grids[i].second->save(href))
		{
			result.error = tr("Cannot save the elevation grid %1:\n%2").arg(href, ElevationModel::lastError());
			return false;
		}
		map.elevationSet().setModel(grids[i].first, std::move(grids[i].second), href);
	}
	return true;
}

}  // namespace


NewMapResult createNewMap(const NewMapParams& params)
{
	NewMapResult result;
	if (!params.box.isValid())
	{
		result.error = tr("The area is empty: the north must be above the south and the east right of the west.");
		return result;
	}
	
	auto map = std::make_unique<Map>();
	if (!loadSymbolSet(*map, params, result) || !setupGeoreferencing(*map, params, result))
		return result;
	
	// Before the download, so that a bad file fails before any request.
	if (!params.elevation_path.isEmpty() && !addElevation(*map, params, result))
		return result;
	
	result.osm_path = params.osm_path;
	if (result.osm_path.isEmpty())
	{
		auto const area = newMapAreaKm2(params.box);
		if (area > new_map_max_download_km2)
		{
			result.error = tr("The area is %1 km², larger than the limit of %2 km² for downloading OpenStreetMap data. Open a downloaded file instead.")
			               .arg(QString::number(area, 'f', 1), QString::number(new_map_max_download_km2));
			return result;
		}
		if (params.map_path.isEmpty())
		{
			result.error = tr("Choose where to save the map before downloading.");
			return result;
		}
		QFileInfo const info(params.map_path);
		result.osm_path = info.path() + QLatin1Char('/') + info.completeBaseName() + QLatin1String(".osm");
		auto const endpoint = params.endpoint.isEmpty() ? newMapDefaultEndpoint() : params.endpoint;
		if (!downloadOverpass(endpoint, params.box, result.osm_path, params.retry_delay_ms, &result.error))
			return result;
	}
	
	GeodataImportOptions options;
	options.clip = newMapBoxPolygon(*map, params.box);
	options.crt_file = params.crt_file.isEmpty() ? defaultOsmCrt(*map) : params.crt_file;
	if (options.crt_file.isEmpty())
		result.warnings.push_back(tr("There is no rule file for OpenStreetMap data and this symbol set. The objects keep the import's default symbols."));
	auto const osm_config = QDir::toNativeSeparators(params.osm_config).toLocal8Bit();
	if (!params.osm_config.isEmpty())
		CPLSetThreadLocalConfigOption("OSM_CONFIG_FILE", osm_config.constData());
	auto const imported = importGeodata(*map, { result.osm_path }, options);
	if (!params.osm_config.isEmpty())
		CPLSetThreadLocalConfigOption("OSM_CONFIG_FILE", nullptr);
	if (!imported.ok)
	{
		result.error = imported.error;
		return result;
	}
	result.objects = imported.objects;
	result.warnings.append(imported.warnings);
	
	MapView view(map.get());
	view.setCenter(MapCoord(0, 0));
	if (!params.tile_service.isEmpty())
	{
		auto const services = MapService::fromSettings();
		auto const presets = MapService::presets();
		auto const* service = findService(services, params.tile_service);
		if (!service)
			service = findService(presets, params.tile_service);
		if (service)
		{
			auto temp = std::make_unique<TileServiceTemplate>(*service, map.get());
			auto* raw = temp.get();
			map->addTemplate(-1, std::move(temp));
			view.setTemplateVisibility(raw, { 1.0, true });
		}
		else
		{
			result.warnings.push_back(tr("There is no map service named %1.").arg(params.tile_service));
		}
	}
	
	map->undoManager().clear();
	if (!params.map_path.isEmpty())
	{
		auto const* format = FileFormats.findFormatForFilename(params.map_path, &FileFormat::supportsFileSaveAs);
		auto exporter = format ? format->makeExporter(params.map_path, map.get(), &view) : nullptr;
		if (!exporter || !exporter->doExport())
		{
			result.error = exporter && !exporter->warnings().empty()
			               ? exporter->warnings().back()
			               : tr("Cannot save the map:\n%1").arg(params.map_path);
			return result;
		}
	}
	
	result.map = std::move(map);
	result.ok = true;
	return result;
}


}  // namespace OpenOrienteering
