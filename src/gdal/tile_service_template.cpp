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


#include "tile_service_template.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtGlobal>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QLatin1String>
#include <QMetaObject>
#include <QPainter>
#include <QPointF>
#include <QVector>
#include <QStringList>
#include <QStandardPaths>
#include <QThread>
#include <QTransform>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <gdal.h>

#include "settings.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "gdal/gdal_manager.h"
#include "gdal/tile_worker.h"

namespace OpenOrienteering {

namespace {

QString& cacheDirectoryStorage()
{
	static QString storage;
	return storage;
}

constexpr int fallback_levels = 8;
constexpr qint64 failure_hold_ms = 30000;
constexpr std::size_t max_pending = 64;

}  // namespace


// static
bool TileServiceTemplate::driversAvailable(QString* reason)
{
	GdalManager manager;  // registers the drivers
	Q_UNUSED(manager)
	for (auto const* name : { "WMS", "WMTS" })
	{
		if (!GDALGetDriverByName(name))
		{
			if (reason)
				*reason = tr("GDAL was built without the %1 driver.").arg(QLatin1String(name));
			return false;
		}
	}
	return true;
}

// static, foom
QString TileServiceTemplate::printWarning(const Map& map, const MapView* view)
{
	if (view && view->areAllTemplatesHidden())
		return {};
	QStringList names;
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = qobject_cast<const TileServiceTemplate*>(map.getTemplate(i));
		if (!temp || temp->getTemplateState() != Loaded)
			continue;
		if (view && !view->isTemplateVisible(temp))
			continue;
		names.append(temp->getTemplateFilename());
	}
	if (names.isEmpty())
		return {};
	return tr("Map services are printed only from the tiles already fetched for the screen, "
	          "at the screen's resolution: %1. "
	          "Areas not yet seen on screen stay empty. "
	          "Download extent in the Add map service dialog makes an ordinary raster template for a complete print.")
	        .arg(names.join(QLatin1String(", ")));
}

// static
QString TileServiceTemplate::cacheDirectory()
{
	auto& storage = cacheDirectoryStorage();
	if (storage.isEmpty())
	{
		auto const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		if (!base.isEmpty())
			storage = base + QLatin1String("/tiles");
	}
	if (!storage.isEmpty())
		QDir().mkpath(storage);
	return storage;
}

// static
void TileServiceTemplate::setCacheDirectory(const QString& path)
{
	cacheDirectoryStorage() = path;
}


TileServiceTemplate::TileServiceTemplate(const MapService& service, Map* map)
: Template(service.url, map)
, service_(service)
{
	template_path = service.url;
	template_file = service.name;
	is_georeferenced = true;
	cache.setMaxCost(qMax(1, Settings::getInstance().getSettingCached(Settings::Templates_TileCacheMB).toInt()) * 1024 * 1024);
	connect(&map->getGeoreferencing(), &Georeferencing::projectionChanged, this, &TileServiceTemplate::projectionChanged);
	connect(&map->getGeoreferencing(), &Georeferencing::transformationChanged, this, [this]() {
		if (getTemplateState() == Loaded)
			setTemplateAreaDirty();
	});
}

TileServiceTemplate::TileServiceTemplate(const QString& path, Map* map)
: Template(path, map)
{
	service_.url = path;
	template_path = path;
	is_georeferenced = true;
	cache.setMaxCost(qMax(1, Settings::getInstance().getSettingCached(Settings::Templates_TileCacheMB).toInt()) * 1024 * 1024);
	connect(&map->getGeoreferencing(), &Georeferencing::projectionChanged, this, &TileServiceTemplate::projectionChanged);
	connect(&map->getGeoreferencing(), &Georeferencing::transformationChanged, this, [this]() {
		if (getTemplateState() == Loaded)
			setTemplateAreaDirty();
	});
}

// static
std::unique_ptr<TileServiceTemplate> TileServiceTemplate::fromDescriptor(const QString& path, Map* map)
{
	MapService service;
	if (!MapService::loadDescriptor(path, service))
		return {};
	return std::make_unique<TileServiceTemplate>(service, map);
}

TileServiceTemplate::TileServiceTemplate(const TileServiceTemplate& proto)
: Template(proto)
, service_(proto.service_)
{
	cache.setMaxCost(proto.cache.maxCost());
	connect(&map->getGeoreferencing(), &Georeferencing::projectionChanged, this, &TileServiceTemplate::projectionChanged);
	connect(&map->getGeoreferencing(), &Georeferencing::transformationChanged, this, [this]() {
		if (getTemplateState() == Loaded)
			setTemplateAreaDirty();
	});
}

TileServiceTemplate::~TileServiceTemplate()
{
	stopWorker();
}

TileServiceTemplate* TileServiceTemplate::duplicate() const
{
	auto* copy = new TileServiceTemplate(*this);
	if (getTemplateState() == Loaded)
	{
		if (copy->loadTemplateFileImpl())
			copy->setTemplateState(Loaded);
		else
			copy->setTemplateState(Invalid);
	}
	return copy;
}


QRectF TileServiceTemplate::getTemplateExtent() const
{
	constexpr double half = 100000.0;  // millimetres on paper
	return { -half, -half, 2 * half, 2 * half };
}


Template::LookupResult TileServiceTemplate::tryToFindTemplateFile(const QString& /*map_path*/)
{
	if (getTemplateState() != Loaded)
		setTemplateState(Unloaded);
	return FoundByAbsPath;
}


void TileServiceTemplate::startWorker()
{
	if (thread)
		return;
	thread = new QThread();
	thread->setObjectName(QStringLiteral("TileServiceTemplate worker"));
	worker = new TileWorker();
	worker->moveToThread(thread);
	connect(worker, &TileWorker::tileReady, this, &TileServiceTemplate::tileReady);
	connect(worker, &TileWorker::tileFailed, this, &TileServiceTemplate::tileFailed);
	connect(worker, &TileWorker::downloadProgress, this, &TileServiceTemplate::downloadProgress);
	connect(worker, &TileWorker::downloadFinished, this, &TileServiceTemplate::workerDownloadFinished);
	thread->start();
}

void TileServiceTemplate::stopWorker()
{
	if (!thread)
		return;
	worker->cancel();
	QMetaObject::invokeMethod(worker, &TileWorker::close, Qt::BlockingQueuedConnection);
	thread->quit();
	thread->wait();
	delete worker;
	worker = nullptr;
	delete thread;
	thread = nullptr;
	in_flight.clear();
	downloading = false;
}


bool TileServiceTemplate::loadTemplateFileImpl()
{
	auto const& georef = map->getGeoreferencing();
	if (georef.getState() != Georeferencing::Geospatial)
	{
		setErrorString(tr("The map is not georeferenced. A map service needs a projected coordinate system."));
		return false;
	}
	QString reason;
	if (!driversAvailable(&reason))
	{
		setErrorString(reason);
		return false;
	}
	if (service_.url.isEmpty())
	{
		setErrorString(tr("The service has no URL."));
		return false;
	}

	startWorker();

	auto const source = service_.gdalSource(cacheDirectory());
	auto const crs_spec = georef.getProjectedCRSSpec();
	auto const reference = georef.toProjectedCoords(georef.getMapRefPoint());
	QString error;
	double base = 0;
	bool ok = false;
	// The worker opens the source; a WMTS source fetches its capabilities
	// document now. Events keep flowing meanwhile (ExcludeUserInputEvents).
	QEventLoop loop;
	auto* w = worker;
	QMetaObject::invokeMethod(worker, [&, w]() {
		ok = w->open(source, crs_spec, reference, service_.user, service_.password, &error, &base);
		QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
	}, Qt::QueuedConnection);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	if (!ok)
	{
		setErrorString(error);
		stopWorker();
		return false;
	}

	r0 = base;
	cache.clear();
	failed_until.clear();
	in_flight.clear();
	last_requests.clear();
	last_tile_error.clear();
	setWarning({});
	return true;
}

void TileServiceTemplate::unloadTemplateFileImpl()
{
	stopWorker();
	cache.clear();
	failed_until.clear();
	r0 = 0;
	setWarning({});
}

void TileServiceTemplate::setWarning(const QString& text)
{
	if (warning == text)
		return;
	warning = text;
	emit warningChanged();
}

void TileServiceTemplate::projectionChanged()
{
	if (getTemplateState() != Loaded)
		return;
	setTemplateAreaDirty();
	unloadTemplateFile();
	if (loadTemplateFile())
		setTemplateAreaDirty();
}


// static
QRectF TileServiceTemplate::projectedBounds(const Georeferencing& georef, const QRectF& map_rect_mm)
{
	return georef.mapToProjected().mapRect(map_rect_mm);
}

// static, foom
QPolygonF TileServiceTemplate::downloadWindowOutline(const Georeferencing& georef, const QRectF& projected_rect)
{
	if (projected_rect.isEmpty())
		return {};
	auto const to_map = georef.projectedToMap();
	return QPolygonF{ QVector<QPointF>{ to_map.map(projected_rect.topLeft()), to_map.map(projected_rect.topRight()),
	                                    to_map.map(projected_rect.bottomRight()), to_map.map(projected_rect.bottomLeft()) } };
}

QRectF TileServiceTemplate::projectedBounds(const QRectF& map_rect_mm) const
{
	return projectedBounds(map->getGeoreferencing(), map_rect_mm);
}


void TileServiceTemplate::drawTemplate(QPainter* painter, const QRectF& clip_rect, double /*scale*/, bool on_screen, qreal opacity) const
{
	last_requests.clear();
	if (!(r0 > 0) || !worker)
		return;

	auto const& georef = map->getGeoreferencing();
	// Ground resolution of one device pixel: projected units per mm over device pixels per mm.
	auto const device_per_mm = std::sqrt(std::abs(painter->worldTransform().determinant()));
	auto const projected_per_mm = std::sqrt(std::abs(georef.mapToProjected().determinant()));
	if (!(device_per_mm > 0) || !(projected_per_mm > 0))
		return;
	auto const resolution = projected_per_mm / device_per_mm;
	auto const level = TilePyramid::levelForResolution(r0, resolution);
	auto const bounds = projectedBounds(clip_rect);
	auto const tiles = TilePyramid::tilesIntersecting(r0, level, bounds);
	if (tiles.empty())
		return;

	painter->save();
	painter->setOpacity(opacity);
	painter->setRenderHint(QPainter::SmoothPixmapTransform);
	// Tiles are drawn in a projected system with y pointing down,
	// which the flip turns into the projected system proper.
	QTransform const flip(1, 0, 0, -1, 0, 0);
	painter->setWorldTransform(flip * georef.projectedToMap(), true);

	std::vector<TileKey> missing;
	for (auto const& key : tiles)
	{
		auto const rect = TilePyramid::tileRect(r0, key);
		QRectF const target(rect.left(), -rect.bottom(), rect.width(), rect.height());
		if (auto const* image = cache.object(key))
		{
			painter->drawImage(target, *image);
			continue;
		}
		missing.push_back(key);
		for (int coarser = level + 1; coarser <= level + fallback_levels; ++coarser)
		{
			auto const ancestor = TilePyramid::ancestor(key, coarser);
			auto const* image = cache.object(ancestor);
			if (!image)
				continue;
			auto const ancestor_rect = TilePyramid::tileRect(r0, ancestor);
			auto const px_per_unit = TilePyramid::tile_size / ancestor_rect.width();
			QRectF const source((rect.left() - ancestor_rect.left()) * px_per_unit,
			                    (ancestor_rect.bottom() - rect.bottom()) * px_per_unit,
			                    rect.width() * px_per_unit,
			                    rect.height() * px_per_unit);
			painter->drawImage(target, *image, source);
			break;
		}
	}
	painter->restore();

	if (!on_screen || missing.empty())
		return;

	// Queue the missing tiles, nearest to the centre of the painted area first,
	// in front of what is still pending. Tiles of distant levels are dropped,
	// and the list is capped.
	auto const center = bounds.center();
	std::sort(missing.begin(), missing.end(), [this, &center](const TileKey& a, const TileKey& b) {
		auto const da = TilePyramid::tileRect(r0, a).center() - center;
		auto const db = TilePyramid::tileRect(r0, b).center() - center;
		return da.x() * da.x() + da.y() * da.y() < db.x() * db.x() + db.y() * db.y();
	});
	auto const now = QDateTime::currentMSecsSinceEpoch();
	std::vector<TileKey> queue;
	for (auto const& key : missing)
	{
		if (in_flight.contains(key))
			continue;
		auto const failed = failed_until.constFind(key);
		if (failed != failed_until.constEnd() && *failed > now)
			continue;
		queue.push_back(key);
	}
	if (queue.empty())
		return;
	last_requests = queue;

	std::vector<TileKey> pending = queue;
	for (auto const& key : in_flight)
		if (std::abs(key.level - level) <= 1 && std::find(pending.begin(), pending.end(), key) == pending.end())
			pending.push_back(key);
	if (pending.size() > max_pending)
		pending.resize(max_pending);
	in_flight.clear();
	for (auto const& key : pending)
		in_flight.insert(key);
	worker->setPending(std::move(pending));
	QMetaObject::invokeMethod(worker, &TileWorker::work, Qt::QueuedConnection);
}


void TileServiceTemplate::tileReady(const TileKey& key, const QImage& image)
{
	in_flight.remove(key);
	failed_until.remove(key);
	setWarning({});
	if (getTemplateState() == Loaded && r0 > 0)
	{
		auto* copy = new QImage(image);
		cache.insert(key, copy, qMax(1, int(copy->sizeInBytes())));
		auto const rect = TilePyramid::tileRect(r0, key);
		map->setTemplateAreaDirty(this, map->getGeoreferencing().projectedToMap().mapRect(rect), 1);
	}
	emit tilesChanged();
}

void TileServiceTemplate::tileFailed(const TileKey& key, const QString& message)
{
	in_flight.remove(key);
	failed_until.insert(key, QDateTime::currentMSecsSinceEpoch() + failure_hold_ms);
	last_tile_error = message;
	setErrorString(message);
	setWarning(message);
	emit tilesChanged();
}


void TileServiceTemplate::saveTypeSpecificTemplateConfiguration(QXmlStreamWriter& xml) const
{
	service_.save(xml);
}

bool TileServiceTemplate::loadTypeSpecificTemplateConfiguration(QXmlStreamReader& xml)
{
	if (xml.name() == QLatin1String("service"))
	{
		auto const user = service_.user;
		auto const password = service_.password;
		service_ = MapService::load(xml);
		service_.name = template_file;
		service_.user = user;
		service_.password = password;
		template_path = service_.url;
		return true;
	}
	xml.skipCurrentElement();
	return true;
}


bool TileServiceTemplate::downloadExtent(const QRectF& projected_rect, double resolution, const QString& path, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	if (downloading)
		return fail(tr("A download is running."));
	if (!service_.bulk_download)
		return fail(tr("The service does not allow bulk download."));
	if (!worker || getTemplateState() != Loaded)
		return fail(tr("The service is not loaded."));
	if (!(resolution > 0) || projected_rect.isEmpty())
		return fail(tr("The window or the resolution is empty."));
	auto const width = qint64(std::ceil(projected_rect.width() / resolution - 1e-9));
	auto const height = qint64(std::ceil(projected_rect.height() / resolution - 1e-9));
	if (width > max_download_pixels || height > max_download_pixels)
		return fail(tr("The window is %1 by %2 pixels; at most %3 pixels on a side can be downloaded.").arg(width).arg(height).arg(max_download_pixels));
	if (path.isEmpty())
		return fail(tr("No file name."));

	downloading = true;
	auto* w = worker;
	QMetaObject::invokeMethod(worker, [w, projected_rect, width, height, path]() {
		w->download(projected_rect, int(width), int(height), path);
	}, Qt::QueuedConnection);
	return true;
}

void TileServiceTemplate::cancelDownload()
{
	if (worker && downloading)
		worker->cancel();
}

void TileServiceTemplate::workerDownloadFinished(bool success, const QString& message)
{
	downloading = false;
	emit downloadFinished(success, message);
}


}  // namespace OpenOrienteering
