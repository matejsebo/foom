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
	auto const path = QFileDialog::getSaveFileName(this, tr("Load as background map"), proposedOnlineMapPath(map_path),
	                                               tr("GeoTIFF (*.tif *.tiff)"));
	if (path.isEmpty())
		return;
	QString error;
	if (!loadAsBackgroundMap(path, &error))
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
