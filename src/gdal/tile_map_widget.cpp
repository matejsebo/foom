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

#include "tile_map_widget.h"

#include <algorithm>

#include <Qt>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLatin1String>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QThreadPool>
#include <QTimer>

#include <cpl_error.h>

#include "gdal/tile_service_template.h"
#include "gdal/tile_worker.h"

namespace OpenOrienteering {

namespace {

/// True when no pixel of the image is opaque at all: no tile came from the service.
bool fullyTransparent(const QImage& image)
{
	if (!image.hasAlphaChannel())
		return false;
	for (int y = 0; y < image.height(); ++y)
	{
		auto const* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x)
		{
			if (qAlpha(line[x]) != 0)
				return false;
		}
	}
	return true;
}

}  // namespace


/**
 * Carries a result from the thread pool to the widget, which may be gone by then.
 *
 * It lives in the widget's thread and is deleted there (deleteLater) when the
 * widget and the last pending rendering have let go of it.
 */
struct TileMapWidget::Relay : public QObject
{
	QPointer<TileMapWidget> owner;
};


TileMapWidget::TileMapWidget(QWidget* parent)
: QWidget(parent)
, placeholder(tr("No map loaded."))
, timer(new QTimer(this))
, relay(new Relay(), [](Relay* r) { r->deleteLater(); })
{
	relay->owner = this;
	timer->setSingleShot(true);
	timer->setInterval(150);
	connect(timer, &QTimer::timeout, this, &TileMapWidget::request);
}

TileMapWidget::~TileMapWidget()
{
	relay->owner = nullptr;
}


void TileMapWidget::setService(const MapService& service)
{
	service_ = service;
	changed();
}

void TileMapWidget::setCrs(const QString& spec)
{
	crs_ = spec;
	changed();
}

void TileMapWidget::setExtent(const QRectF& extent)
{
	extent_ = extent;
	changed();
}

void TileMapWidget::setOutline(const QPolygonF& outline)
{
	outline_ = outline;
	update();
}

void TileMapWidget::setPlaceholderText(const QString& text)
{
	placeholder = text;
	update();
}


// static
QRectF TileMapWidget::fitExtent(const QRectF& extent, const QSizeF& size)
{
	if (extent.isEmpty() || size.isEmpty())
		return extent;
	auto const centre = extent.center();
	auto const aspect = size.width() / size.height();
	auto width = extent.width();
	auto height = extent.height();
	if (width / height < aspect)
		width = height * aspect;
	else
		height = width / aspect;
	return { centre.x() - width / 2, centre.y() - height / 2, width, height };
}

QRectF TileMapWidget::renderedExtent() const
{
	return fitExtent(extent_, QSizeF(size()));
}

QTransform TileMapWidget::projectedToWidget() const
{
	auto const rect = renderedExtent();
	if (rect.isEmpty() || width() <= 0 || height() <= 0)
		return {};
	auto const sx = width() / rect.width();
	auto const sy = height() / rect.height();
	// x' = (x - left) sx, y' = (bottom - y) sy
	return QTransform(sx, 0, 0, -sy, -rect.left() * sx, rect.bottom() * sy);
}

QPolygonF TileMapWidget::outlineInWidget() const
{
	return projectedToWidget().map(outline_);
}

QSize TileMapWidget::sizeHint() const
{
	return { 320, 240 };
}


void TileMapWidget::load()
{
	loaded = true;
	request();
}

void TileMapWidget::changed()
{
	update();
	if (loaded)
		timer->start();
}

void TileMapWidget::request()
{
	timer->stop();
	auto const id = ++request_id;
	auto const rect = renderedExtent();
	auto const pixels = size();
	requested_size = pixels;
	if (service_.url.isEmpty() || crs_.isEmpty() || rect.isEmpty() || pixels.isEmpty())
	{
		finished(id, {}, rect, tr("Nothing to show."));
		return;
	}
	busy = true;
	update();

	// No disk cache for local tiles.
	auto const local = service_.url.startsWith(QLatin1String("file:"), Qt::CaseInsensitive);
	auto const source = service_.gdalSource(local ? QString() : TileServiceTemplate::cacheDirectory());
	auto const crs = crs_;
	auto const user = service_.user;
	auto const password = service_.password;
	auto const r = relay;
	QThreadPool::globalInstance()->start([r, id, source, crs, rect, pixels, user, password]() {
		QImage image;
		QString message;
		{
			TileWorker worker;
			double r0 = 0;
			if (!worker.open(source, crs, rect.center(), user, password, &message, &r0)
			    || !worker.renderWindow(rect, pixels.width(), pixels.height(), image, message))
			{
				image = QImage();
			}
			else if (fullyTransparent(image))
			{
				// GDAL leaves tiles which it cannot fetch empty.
				auto const* gdal_message = CPLGetLastErrorMsg();
				message = (gdal_message && *gdal_message) ? QString::fromUtf8(gdal_message).trimmed()
				                                          : tr("The service gave no map for this area.");
				image = QImage();
			}
		}
		QMetaObject::invokeMethod(r.get(), [r, id, image, rect, message]() {
			if (r->owner)
				r->owner->finished(id, image, rect, message);
		}, Qt::QueuedConnection);
	});
}

void TileMapWidget::finished(int id, const QImage& image, const QRectF& extent, const QString& message)
{
	if (id != request_id)
		return;  // An older request
	busy = false;
	auto const success = !image.isNull();
	image_ = image;
	image_extent = success ? extent : QRectF();
	error = success ? QString() : (message.isEmpty() ? tr("The map could not be loaded.") : message);
	update();
	emit rendered(success);
}


void TileMapWidget::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Mid));
	auto const transform = projectedToWidget();
	if (!image_.isNull())
		painter.drawImage(transform.mapRect(image_extent), image_);

	if (!outline_.isEmpty())
	{
		painter.setRenderHint(QPainter::Antialiasing);
		QPen pen(QColor(220, 0, 0));
		pen.setWidthF(2);
		pen.setCosmetic(true);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawPolygon(transform.map(outline_));
	}

	auto const draw_box = [&painter](const QRect& box, int flags, const QString& text) {
		auto const bounds = painter.fontMetrics().boundingRect(box, flags | Qt::TextWordWrap, text).adjusted(-3, -1, 3, 1);
		painter.fillRect(bounds, QColor(255, 255, 255, 200));
		painter.setPen(Qt::black);
		painter.drawText(box, flags | Qt::TextWordWrap, text);
	};

	QString status;
	if (busy)
		status = tr("Loading the map...");
	else if (!error.isEmpty())
		status = error;
	else if (!loaded)
		status = placeholder;
	if (!status.isEmpty())
		draw_box(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter, status);

	if (loaded && !service_.attribution.isEmpty())
	{
		auto font = painter.font();
		font.setPointSizeF(std::max(6.0, font.pointSizeF() * 0.8));
		painter.setFont(font);
		draw_box(rect().adjusted(4, 4, -4, -2), Qt::AlignRight | Qt::AlignBottom, service_.attribution);
	}
}

void TileMapWidget::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (loaded && size() != requested_size)
		timer->start();
}


}  // namespace OpenOrienteering
