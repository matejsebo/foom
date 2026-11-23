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

#include "online_map.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <Qt>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QEventLoop>
#include <QInputDialog>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_transform.h"
#include "elevation/raster_products.h"
#include "gdal/tile_map_widget.h"
#include "gdal/tile_service_template.h"


namespace OpenOrienteering {

namespace {

const QLatin1String service_key("OnlineMap/service");

}  // namespace


bool saveOnlineMapImage(const QImage& image, const QRectF& extent, const QString& crs_spec,
                        const QString& attribution, const QString& path, QString* error)
{
	if (image.isNull() || extent.isEmpty())
	{
		if (error)
			*error = OnlineMapDialog::tr("Nothing is shown yet.");
		return false;
	}
	RasterProduct product;
	product.image = image.convertToFormat(QImage::Format_RGB32);
	product.extent = extent;
	product.crs_spec = crs_spec;
	if (!attribution.isEmpty())
	{
		// The terms of the services ask for it on every copy.
		QPainter painter(&product.image);
		QFont font;
		font.setPixelSize(std::max(9, std::min(13, product.image.height() / 40)));
		painter.setFont(font);
		auto const metrics = QFontMetrics(font);
		auto const text_size = metrics.size(Qt::TextSingleLine, attribution);
		QRect box(product.image.width() - text_size.width() - 8, product.image.height() - text_size.height() - 4,
		          text_size.width() + 8, text_size.height() + 4);
		painter.fillRect(box, QColor(255, 255, 255, 200));
		painter.setPen(Qt::black);
		painter.drawText(box, Qt::AlignCenter, attribution);
	}
	return writeGeoTiff(product, path, error);
}

QString proposedOnlineMapPath(const QString& map_path)
{
	QString base;
	if (map_path.isEmpty())
	{
		base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1String("/online");
	}
	else
	{
		QFileInfo const info(map_path);
		base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QLatin1String("_online");
	}
	for (int n = 1; ; ++n)
	{
		QString const path = base + QLatin1Char('_') + QString::number(n) + QLatin1String(".tif");
		if (!QFileInfo::exists(path))
			return path;
	}
}

double proposedOnlineMapResolution(const QRectF& extent)
{
	auto const side = std::max(extent.width(), extent.height());
	auto const limit = double(TileServiceTemplate::max_download_pixels) - 1.0;
	auto resolution = 1.0;
	if (side / resolution > limit)
		resolution = std::ceil(side / limit * 10.0) / 10.0;
	return resolution;
}

bool downloadOnlineMap(Map& map, const MapService& service, const QRectF& extent, double resolution,
                       const QString& path, QWidget* parent, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	TileServiceTemplate temp(service, &map);
	if (!temp.setupAndLoad(parent, nullptr))
		return fail(temp.errorString());
	// Whole pixels, so that the cells are square (seen 2026-11-23: an extent of
	// 11921.3 by 8940.9 m at 1 m gave cells of 0.99996 by 0.99994 m).
	auto const columns = std::ceil(extent.width() / resolution - 1e-9);
	auto const rows = std::ceil(extent.height() / resolution - 1e-9);
	auto const window = QRectF(extent.left(), extent.top(), columns * resolution, rows * resolution);
	QString message;
	if (!temp.downloadExtent(window, resolution, path, &message))
		return fail(message);

	bool success = false;
	QEventLoop loop;
	QObject::connect(&temp, &TileServiceTemplate::downloadFinished, &loop, [&](bool ok, const QString& text) {
		success = ok;
		message = text;
		loop.quit();
	});
	std::unique_ptr<QProgressDialog> progress;
	if (parent)
	{
		progress.reset(new QProgressDialog(OnlineMapDialog::tr("Downloading %1...").arg(QFileInfo(path).fileName()),
		                                   OnlineMapDialog::tr("Cancel"), 0, 100, parent));
		progress->setWindowModality(Qt::WindowModal);
		progress->setMinimumDuration(0);
		QObject::connect(&temp, &TileServiceTemplate::downloadProgress, progress.get(), &QProgressDialog::setValue);
		QObject::connect(progress.get(), &QProgressDialog::canceled, &temp, &TileServiceTemplate::cancelDownload);
	}
	if (temp.isDownloading())
		loop.exec();
	progress.reset();
	if (!success)
		return fail(message);
	return attachProductTemplate(map, path, error) != nullptr;
}


OnlineMapDialog::OnlineMapDialog(Map& map, const QString& map_path, QWidget* parent)
: QDialog(parent)
, map(map)
, map_path(map_path)
, services(MapService::onlineMapServices())
{
	setWindowTitle(tr("Online map"));
	auto const area = mapSurroundings(map);

	map_widget = new TileMapWidget();
	map_widget->setMinimumSize(640, 480);
	map_widget->setCrs(map.getGeoreferencing().getProjectedCRSSpec());
	map_widget->setExtent(area.extent);
	map_widget->setOutline(area.outline);
	map_widget->setPlaceholderText(tr("The online map is fetched from the chosen service."));
	map_widget->setCursor(Qt::OpenHandCursor);
	map_widget->installEventFilter(this);

	service_box = new QComboBox();
	service_box->setObjectName(QStringLiteral("online_map_service"));
	for (auto const& service : services)
		service_box->addItem(service.name);
	web_button = new QPushButton(tr("Open in web viewer"));
	web_button->setObjectName(QStringLiteral("online_map_web"));
	load_button = new QPushButton(tr("Load as background map"));
	load_button->setObjectName(QStringLiteral("online_map_load"));
	auto* close = new QDialogButtonBox(QDialogButtonBox::Close);

	auto* row = new QHBoxLayout();
	row->addWidget(new QLabel(tr("Service:")));
	row->addWidget(service_box, 1);
	row->addWidget(web_button);
	row->addWidget(load_button);
	row->addWidget(close);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(map_widget, 1);
	layout->addWidget(new QLabel(tr("Drag to move the map, turn the wheel to zoom.")));
	layout->addLayout(row);

	auto const remembered = QSettings().value(service_key).toString();
	auto const found = std::find_if(services.begin(), services.end(), [&remembered](const MapService& s) { return s.name == remembered; });
	if (found != services.end())
		service_box->setCurrentIndex(int(found - services.begin()));
	serviceChanged(service_box->currentIndex());

	connect(service_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OnlineMapDialog::serviceChanged);
	connect(web_button, &QPushButton::clicked, this, &OnlineMapDialog::openWebViewer);
	connect(load_button, &QPushButton::clicked, this, &OnlineMapDialog::loadClicked);
	connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// Opening the dialog is asking to go online; after the layout has its size.
	if (!area.extent.isNull())
		QTimer::singleShot(0, map_widget, &TileMapWidget::load);
}

OnlineMapDialog::~OnlineMapDialog() = default;


const MapService& OnlineMapDialog::service() const
{
	return map_widget->service();
}

void OnlineMapDialog::setServiceIndex(int index)
{
	service_box->setCurrentIndex(index);
}

void OnlineMapDialog::serviceChanged(int index)
{
	if (index < 0 || std::size_t(index) >= services.size())
		return;
	map_widget->setService(services[std::size_t(index)]);
	QSettings().setValue(service_key, services[std::size_t(index)].name);
}

bool OnlineMapDialog::centreTarget(GeoviewerTarget& target) const
{
	auto const& georef = map.getGeoreferencing();
	auto const centre = map_widget->renderedExtent().center();
	auto const map_point = georef.toMapCoordF(centre);
	return geoviewerTarget(georef, map_point, map_point, target);
}

void OnlineMapDialog::openWebViewer()
{
	GeoviewerTarget target;
	if (centreTarget(target))
		emit webViewerRequested(geoviewerUrl(geoviewerPattern(), target));
}

bool OnlineMapDialog::loadAsBackgroundMap(const QString& path, QString* error)
{
	if (!saveOnlineMapImage(map_widget->image(), map_widget->imageExtent(), map_widget->crs(),
	                        service().attribution, path, error))
		return false;
	return attachProductTemplate(map, path, error) != nullptr;
}

void OnlineMapDialog::loadClicked()
{
	if (map_widget->image().isNull())
	{
		QMessageBox::information(this, tr("Online map"), tr("Nothing is shown yet."));
		return;
	}
	// the design notes, "Online map and geoviewer", the download (2026-11-23).
	auto const extent = map_widget->renderedExtent();
	auto const download = service().bulk_download;
	double resolution = 0;
	if (download)
	{
		auto const proposed = proposedOnlineMapResolution(extent);
		auto ok = false;
		resolution = QInputDialog::getDouble(this, tr("Load as background map"),
		                                     tr("The area shown is %1 by %2 m. Resolution in metres per pixel\n"
		                                        "(at most %3 pixels on a side):")
		                                     .arg(qRound(extent.width())).arg(qRound(extent.height())).arg(TileServiceTemplate::max_download_pixels),
		                                     proposed, std::max(0.05, proposed * 0.999), 1000.0, 2, &ok);
		if (!ok)
			return;
	}
	else
	{
		auto const shown = map_widget->imageExtent().width() / std::max(1, map_widget->image().width());
		auto const answer = QMessageBox::question(this, tr("Load as background map"),
		                                          tr("%1 does not allow downloads, so the picture shown is kept, at %2 m per pixel. "
		                                             "Zoom in for more detail. Keep it?").arg(service().name).arg(shown, 0, 'f', 1));
		if (answer != QMessageBox::Yes)
			return;
	}
	auto const path = QFileDialog::getSaveFileName(this, tr("Load as background map"), proposedOnlineMapPath(map_path),
	                                               tr("GeoTIFF (*.tif *.tiff)"));
	if (path.isEmpty())
		return;
	QString error;
	auto const ok = download ? downloadOnlineMap(map, service(), extent, resolution, path, this, &error)
	                         : loadAsBackgroundMap(path, &error);
	if (!ok && error != QLatin1String("Cancelled"))
		QMessageBox::warning(this, tr("Online map"), tr("Cannot load the online map as a background map:\n%1").arg(error));
}


void OnlineMapDialog::pan(const QPoint& pixels)
{
	auto const shown = map_widget->renderedExtent();
	if (shown.isEmpty() || map_widget->width() <= 0)
		return;
	auto const scale = shown.width() / map_widget->width();
	auto extent = map_widget->extent();
	extent.translate(-pixels.x() * scale, pixels.y() * scale);  // the widget's y points down, north up
	map_widget->setExtent(extent);
}

void OnlineMapDialog::zoom(double factor, const QPointF& pixel)
{
	if (factor <= 0)
		return;
	bool invertible = false;
	auto const to_projected = map_widget->projectedToWidget().inverted(&invertible);
	if (!invertible)
		return;
	auto const p = to_projected.map(pixel);
	auto const extent = map_widget->extent();
	QRectF zoomed(p + (extent.topLeft() - p) / factor, p + (extent.bottomRight() - p) / factor);
	if (zoomed.width() < 20 || zoomed.width() > 4.0e7)
		return;
	map_widget->setExtent(zoomed.normalized());
}

bool OnlineMapDialog::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != map_widget)
		return QDialog::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	{
		auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() != Qt::LeftButton)
			break;
		dragging = true;
		drag_start = mouse->pos();
		map_widget->setCursor(Qt::ClosedHandCursor);
		return true;
	}
	case QEvent::MouseMove:
		if (dragging)
		{
			auto* mouse = static_cast<QMouseEvent*>(event);
			pan(mouse->pos() - drag_start);
			drag_start = mouse->pos();
			return true;
		}
		break;
	case QEvent::MouseButtonRelease:
		if (dragging)
		{
			dragging = false;
			map_widget->setCursor(Qt::OpenHandCursor);
			return true;
		}
		break;
	case QEvent::Wheel:
	{
		auto* wheel = static_cast<QWheelEvent*>(event);
		auto const steps = wheel->angleDelta().y() / 120.0;
		if (steps != 0)
			zoom(std::pow(1.25, steps), wheel->position());
		return true;
	}
	default:
		break;
	}
	return QDialog::eventFilter(watched, event);
}


}  // namespace OpenOrienteering
