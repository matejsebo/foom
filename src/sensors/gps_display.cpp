/*
 *    Copyright 2013 Thomas Schöps
 *    Copyright 2014-2020 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gps_display.h"

#if defined(Q_OS_ANDROID)
#  include <jni.h>
#  include <QAndroidJniObject>
#endif
#if defined(QT_POSITIONING_LIB)
#  include <QGeoCoordinate>
#  include <QGeoPositionInfo>
#  include <QGeoPositionInfoSource>  // IWYU pragma: keep
#endif

#include <algorithm>
#include <type_traits>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QBrush>  // foom
#include <QColor>
#include <QFlags>
#include <QLatin1String>
#include <QLinearGradient>  // foom
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QTimer>  // IWYU pragma: keep
#include <QTimerEvent>
#include <QVariant>

#include "settings.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map_view.h"
#include "gui/util_gui.h"
#include "gui/map/map_widget.h"
#include "sensors/compass.h"
#include "sensors/nmea_log.h"  // foom
#include "sensors/nmea_source_settings.h"  // foom
#include "util/backports.h"  // IWYU pragma: keep

#if defined(MAPPER_USE_FAKE_POSITION_PLUGIN)
#include "sensors/fake_position_source.h"
#endif

namespace OpenOrienteering {

namespace {

// Opacities as understood by QPainter::setOpacity().
static qreal opacity_curve[] = { 0.8, 1.0, 0.8, 0.5, 0.2, 0.0, 0.2, 0.5 };

#if defined(QT_POSITIONING_LIB)
/// foom: The quality data of a fix: what the source read from its sentences,
/// if it reads sentences, and the accuracy of the position.
FixInfo fixInfoOf(const QObject* source, const QGeoPositionInfo& info)
{
	FixInfo fix;
	auto const property = source ? source->property(FixInfo::propertyName()) : QVariant{};
	if (property.canConvert<FixInfo>())
		fix = property.value<FixInfo>();
	if (info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
		fix.accuracy = info.attribute(QGeoPositionInfo::HorizontalAccuracy);
	return fix;
}
#endif

}  // namespace


void GPSDisplay::PulsatingOpacity::start(QObject& object)
{
	if (!timer_id)
		timer_id = object.startTimer(1000 / std::extent<decltype(opacity_curve)>::value);
}

void GPSDisplay::PulsatingOpacity::stop(QObject& object)
{
	if (timer_id)
	{
		object.killTimer(timer_id);
		*this = {};
	}
}

bool GPSDisplay::PulsatingOpacity::advance()
{
	if (isActive())
	{
		++index;
		if (index < std::extent<decltype(opacity_curve)>::value)
			return true;
		index = 0;
	}
	return false;
}

qreal GPSDisplay::PulsatingOpacity::current() const
{
	return opacity_curve[index];
}



GPSDisplay::GPSDisplay(MapWidget* widget, const Georeferencing& georeferencing, QObject* parent)
 : QObject(parent)
 , widget(widget)
 , georeferencing(georeferencing)
{
	// foom: the quality gate
	qRegisterMetaType<FixInfo>();
	fix_gate = FixGateSettings::load();
	connect(&Settings::getInstance(), &Settings::settingsChanged, this, [this]() { setFixGate(FixGateSettings::load()); });
	// foom: the antenna height and the tail
	auto const load_display_settings = [this]() {
		auto const display_settings = GpsDisplaySettings::load();
		setAntennaHeight(display_settings.antenna_height);
		setTailLength(display_settings.tail_length);
	};
	load_display_settings();
	connect(&Settings::getInstance(), &Settings::settingsChanged, this, load_display_settings);
	
#if defined(QT_POSITIONING_LIB)
	
#if defined(MAPPER_USE_FAKE_POSITION_PLUGIN)
	{
		auto const ref = georeferencing.getGeographicRefPoint();
		FakePositionSource::setReferencePoint({ref.latitude(), ref.longitude(), 400});
	}
#endif
	
	auto const & settings = Settings::getInstance();
	auto const nmea_serialport = settings.nmeaSerialPort();
	if (!nmea_serialport.isEmpty())
		qputenv("QT_NMEA_SERIAL_PORT", nmea_serialport.toUtf8());
	
	auto const nmea_parameters = NmeaSourceSettings::load().parameters();  // foom: the network receiver and the baud rate
	auto source_name = settings.positionSource();
	if (source_name.isEmpty())
	{
		source_name = QLatin1String("default");
		source = QGeoPositionInfoSource::createDefaultSource(nmea_parameters, this);
	}
	else
	{
		source = QGeoPositionInfoSource::createSource(source_name, nmea_parameters, this);
	}
	
	if (!source)
	{
		qDebug("Cannot create QGeoPositionInfoSource '%s'!", qPrintable(source_name));
		return;
	}
	
	source->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
	source->setUpdateInterval(1000);
	connect(source, &QGeoPositionInfoSource::positionUpdated, this, &GPSDisplay::positionUpdated, Qt::QueuedConnection);
	connect(source, QOverload<QGeoPositionInfoSource::Error>::of(&QGeoPositionInfoSource::error), this, &GPSDisplay::error);
	connect(source, &QGeoPositionInfoSource::updateTimeout, this, &GPSDisplay::updateTimeout);
	// foom: the folder for the log of the sentences which the source receives
	auto const set_log_folder = [this]() {
		source->setProperty(NmeaLog::folderProperty(), NmeaSourceSettings::load().log_folder);
	};
	set_log_folder();
	connect(&Settings::getInstance(), &Settings::settingsChanged, this, set_log_folder);
#endif

	widget->setGPSDisplay(this);
}

GPSDisplay::~GPSDisplay()
{
	stopUpdates();
	widget->setGPSDisplay(nullptr);
}

bool GPSDisplay::checkGPSEnabled()
{
#if defined(Q_OS_ANDROID)
	static bool translation_initialized = false;
	if (!translation_initialized)
	{
		QAndroidJniObject gps_disabled_string = QAndroidJniObject::fromString(tr("GPS is disabled in the device settings. Open settings now?"));
		QAndroidJniObject yes_string = QAndroidJniObject::fromString(tr("Yes"));
		QAndroidJniObject no_string  = QAndroidJniObject::fromString(tr("No"));
		QAndroidJniObject::callStaticMethod<void>(
			"org/openorienteering/mapper/MapperActivity",
			"setTranslatableStrings",
			"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
			yes_string.object<jstring>(),
			no_string.object<jstring>(),
			gps_disabled_string.object<jstring>());
		translation_initialized = true;
	}
	
	QAndroidJniObject::callStaticMethod<void>("org/openorienteering/mapper/MapperActivity",
                                       "checkGPSEnabled",
                                       "()V");
#endif
	return true;
}

void GPSDisplay::startUpdates()
{
#if defined(QT_POSITIONING_LIB)
	if (source)
	{
		checkGPSEnabled();
		source->startUpdates();
	}
#endif
}

void GPSDisplay::stopUpdates()
{
#if defined(QT_POSITIONING_LIB)
	if (source)
	{
		source->stopUpdates();
		has_valid_position = false;
		gps_tail.clear();  // foom
		has_raw_gps_coord = false;  // foom
	}
#endif
}

void GPSDisplay::setVisible(bool visible)
{
	if (this->visible != visible)
	{
		this->visible = visible;
		updateMapWidget();
	}
}

void GPSDisplay::setFixGate(const FixGateSettings& settings)
{
	fix_gate = settings;
	if (has_valid_position)
	{
		fix_rejected = FixGate::check(latest_fix, fix_gate) != FixGate::Accepted;
		updateMapWidget();
	}
}

void GPSDisplay::setAntennaHeight(double metres)
{
	antenna_height = metres;
}

void GPSDisplay::setTailLength(double metres)
{
	auto const scale = double(georeferencing.getScaleDenominator());
	gps_tail.setMaxLength(scale > 0 ? metres * 1000.0 / scale : 0.0);
	if (visible)
		updateMapWidget();
}

// foom: the design notes, "Position offset and sentence log"
void GPSDisplay::setPositionOffset(const MapCoordF& offset)
{
	// So that the position without the offset is the latest one.
	bool ok = false;
	calcLatestGPSCoord(ok);
	
	position_offset = offset;
	has_position_offset = !qFuzzyIsNull(offset.x()) || !qFuzzyIsNull(offset.y());
	if (has_raw_gps_coord)
		latest_gps_coord = MapCoordF(raw_gps_coord.x() + offset.x(), raw_gps_coord.y() + offset.y());
	// The tail shows no way which was not gone.
	gps_tail.clear();
	if (has_raw_gps_coord && hasValidPosition())
		gps_tail.add(latest_gps_coord);
	emit positionOffsetChanged();
	updateMapWidget();
}

void GPSDisplay::clearPositionOffset()
{
	if (has_position_offset)
		setPositionOffset({});
}

bool GPSDisplay::setTruePosition(const MapCoordF& true_position)
{
	if (!hasValidPosition())
		return false;
	bool ok = false;
	calcLatestGPSCoord(ok);
	if (!ok || !has_raw_gps_coord)
		return false;
	setPositionOffset(MapCoordF(true_position.x() - raw_gps_coord.x(), true_position.y() - raw_gps_coord.y()));
	return true;
}

void GPSDisplay::checkSentenceLog()
{
#if defined(QT_POSITIONING_LIB)
	if (!source)
		return;
	auto const file = source->property(NmeaLog::fileProperty()).toString();
	auto const refused = source->property(NmeaLog::errorProperty()).toString();
	if (file == sentence_log_file && refused == sentence_log_error)
		return;
	sentence_log_file = file;
	sentence_log_error = refused;
	if (!refused.isEmpty())
		emit sentenceLogChanged(refused, false);
	else if (!file.isEmpty())
		emit sentenceLogChanged(file, true);
#endif
}

void GPSDisplay::enableDistanceRings(bool enable)
{
	distance_rings_enabled = enable;
	if (visible && has_valid_position)
		updateMapWidget();
}

void GPSDisplay::enableHeadingIndicator(bool enable)
{
	if (enable && ! heading_indicator_enabled)
		Compass::getInstance().startUsage();
	else if (! enable && heading_indicator_enabled)
		Compass::getInstance().stopUsage();
	
	heading_indicator_enabled = enable;
}

void GPSDisplay::paint(QPainter* painter)
{
	if (!visible || !has_valid_position)
		return;
	paintTail(painter);  // foom: also while a failing fix is hidden
	if (fix_rejected && fix_gate.policy == FixGateSettings::Hide)  // foom
		return;
	
	// Get GPS position on map widget
	bool ok = true;
	MapCoordF gps_coord = calcLatestGPSCoord(ok);
	if (!ok)
		return;
	QPointF gps_pos = widget->mapToViewport(gps_coord);
	
	const auto one_mm = Util::mmToPixelLogical(1);
	const auto mmToPixelLogical = [one_mm](qreal mm) { return mm * one_mm; };
	
	const auto flags = painter->renderHints();
	painter->setRenderHints(flags | QPainter::Antialiasing);
	const auto opacity = painter->opacity();
	painter->setOpacity(pulsating_opacity.current() * opacity);
	
	// Highlight markers by white framing.
	const auto framing = QColor(Qt::white);
	const auto foreground = QColor((tracking_lost || fix_rejected) ? Qt::gray : Qt::red);  // foom: or a fix which failed the quality gate
	
	// Draw center dot or arrow
	if (heading_indicator_enabled)
	{
		// For heading indicator, get azimuth from compass and calculate
		// the relative rotation to map view rotation, clockwise.
		const auto heading_rotation_deg = qreal(Compass::getInstance().getCurrentAzimuth())
		                                  + qRadiansToDegrees(widget->getMapView()->getRotation());
		
		painter->save();
		painter->translate(gps_pos);
		painter->rotate(heading_rotation_deg);
		painter->scale(one_mm, one_mm);
		
		// Draw arrow
		static const QPointF arrow_points[4] = {
		    { 0, -2.5 },
		    { 1, 1 },
		    { 0, 0 },
		    { -1, 1 }
		};
		painter->setPen(QPen{framing, mmToPixelLogical(0.1)});
		painter->setBrush(foreground);
		painter->drawPolygon(arrow_points, std::extent<decltype(arrow_points)>::value);
		
		// Draw heading line
		painter->setPen(QPen(Qt::gray, 0.2));
		painter->setBrush(Qt::NoBrush);
		painter->drawLine(QPointF(0, 0), QPointF(0, -500)); // very long
		
		painter->restore();
	}
	else
	{
		const auto dot_radius = mmToPixelLogical(0.6);
		painter->setPen(QPen{framing, mmToPixelLogical(0.3)});
		painter->setBrush(foreground);
		painter->drawEllipse(gps_pos, dot_radius, dot_radius);
		
		const auto five_mm = mmToPixelLogical(5);
		const auto ten_mm = 2 * five_mm;
		const auto draw_crosshairs = [painter, gps_pos, five_mm, ten_mm]() {
			painter->drawLine(gps_pos - QPointF{five_mm, 0}, gps_pos - QPointF{ten_mm, 0});
			painter->drawLine(gps_pos + QPointF{five_mm, 0}, gps_pos + QPointF{ten_mm, 0});
			painter->drawLine(gps_pos - QPointF{0, five_mm}, gps_pos - QPointF{0, ten_mm});
			painter->drawLine(gps_pos + QPointF{0, five_mm}, gps_pos + QPointF{0, ten_mm});
		};
		painter->setBrush(Qt::NoBrush);
		painter->setPen(QPen(framing, mmToPixelLogical(1)));
		draw_crosshairs();
		painter->setPen(QPen(foreground, mmToPixelLogical(0.5)));
		draw_crosshairs();
	}
	
	auto meters_to_pixels = widget->getMapView()->lengthToPixel(qreal(1000000) / georeferencing.getScaleDenominator());
	// Draw distance circles
	if (distance_rings_enabled)
	{
		const auto num_distance_rings = 2;
		const auto distance_ring_radius_meters = 10;
		const auto distance_ring_radius_pixels = distance_ring_radius_meters * meters_to_pixels;
		painter->setPen(QPen(Qt::gray, mmToPixelLogical(0.1)));
		painter->setBrush(Qt::NoBrush);
		auto radius = distance_ring_radius_pixels;
		for (int i = 0; i < num_distance_rings; ++i)
		{
			painter->drawEllipse(gps_pos, radius, radius);
			radius += distance_ring_radius_pixels;
		}
	}
	
	// Draw accuracy circle
	if (latest_gps_coord_accuracy >= 0)
	{
		const auto accuracy_pixels = qreal(latest_gps_coord_accuracy) * meters_to_pixels;
		painter->setBrush(Qt::NoBrush);
		painter->setPen(QPen(framing, mmToPixelLogical(1)));
		painter->drawEllipse(gps_pos, accuracy_pixels, accuracy_pixels);
		painter->setPen(QPen(foreground, mmToPixelLogical(0.5)));
		painter->drawEllipse(gps_pos, accuracy_pixels, accuracy_pixels);
	}
	
	painter->setOpacity(opacity);
	painter->setRenderHints(QPainter::Antialiasing);
}

// foom: the design notes, "Antenna height, averaged vertex and tail"
void GPSDisplay::paintTail(QPainter* painter)
{
	auto const& points = gps_tail.points();
	if (points.size() < 2)
		return;
	
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	auto const width = Util::mmToPixelLogical(0.5);
	auto color = QColor(Qt::red);
	// From the position backwards; every piece fades from its newer end to its older one.
	QPointF newer = widget->mapToViewport(points.back());
	auto newer_opacity = gps_tail.opacityAt(points.size() - 1);
	for (auto i = points.size() - 1; i > 0; --i)
	{
		QPointF const older = widget->mapToViewport(points[i - 1]);
		auto const older_opacity = gps_tail.opacityAt(i - 1);
		QLinearGradient gradient(newer, older);
		color.setAlphaF(newer_opacity);
		gradient.setColorAt(0, color);
		color.setAlphaF(older_opacity);
		gradient.setColorAt(1, color);
		painter->setPen(QPen(QBrush(gradient), width, Qt::SolidLine, Qt::FlatCap));
		painter->drawLine(newer, older);
		newer = older;
		newer_opacity = older_opacity;
	}
	painter->restore();
}


void GPSDisplay::startBlinking(int seconds)
{
	blink_count = std::max(1, seconds);
	pulsating_opacity.start(*this);
}

void GPSDisplay::stopBlinking()
{
	blink_count = 0;
	pulsating_opacity.stop(*this);
}

void GPSDisplay::timerEvent(QTimerEvent* e)
{
	if (e->timerId() == pulsating_opacity.timerId())
	{
		if (!pulsating_opacity.advance())
		{
			--blink_count;
			if (blink_count <= 0)
				stopBlinking();
		}
		updateMapWidget();
	}
}


void GPSDisplay::positionUpdated(const QGeoPositionInfo& info)
{
#if defined(QT_POSITIONING_LIB)
	checkSentenceLog();  // foom
	// foom: the quality gate. A fix which fails is not passed on;
	// its marker is drawn in grey, or not at all.
	latest_fix = fixInfoOf(source, info);
	auto const verdict = FixGate::check(latest_fix, fix_gate);
	fix_rejected = verdict != FixGate::Accepted;
	emit fixChecked(latest_fix, int(verdict));
	if (fix_rejected)
	{
		tail_gap = true;
		gps_updated = true;
		tracking_lost = false;
		has_valid_position = true;
		updateMapWidget();
		return;
	}
	
	gps_updated = true;
	tracking_lost = false;
	has_valid_position = true;
	
	bool ok = false;
	calcLatestGPSCoord(ok);
	if (ok)
	{
		if (tail_gap)  // foom: the tail does not bridge a time without usable positions
			gps_tail.clear();
		tail_gap = false;
		gps_tail.add(latest_gps_coord);  // foom
		emit mapPositionUpdated(latest_gps_coord, latest_gps_coord_accuracy);
		// foom: with an offset, the latitude and longitude of the shifted place
		auto latitude = info.coordinate().latitude();
		auto longitude = info.coordinate().longitude();
		if (has_position_offset)
		{
			bool shifted_ok = false;
			auto const shifted = georeferencing.toGeographicCoords(latest_gps_coord, &shifted_ok);
			if (shifted_ok)
			{
				latitude = shifted.latitude();
				longitude = shifted.longitude();
			}
		}
		emit latLonUpdated(
			latitude,
			longitude,
			(info.coordinate().type() == QGeoCoordinate::Coordinate3D) ? info.coordinate().altitude() - antenna_height : -9999,  // foom: less the height of the antenna
			latest_gps_coord_accuracy
		);
	}
	
	updateMapWidget();
#endif
}

void GPSDisplay::error()
{
#if defined(QT_POSITIONING_LIB)
	checkSentenceLog();  // foom
	if (source->error() != QGeoPositionInfoSource::NoError)
	{
		if (!tracking_lost)
		{
			tracking_lost = true;
			tail_gap = true;  // foom
			emit positionUpdatesInterrupted();
			updateMapWidget();
		}
	}
#endif
}

void GPSDisplay::updateTimeout()
{
	checkSentenceLog();  // foom
	// Lost satellite fix
	if (!tracking_lost)
	{
		tracking_lost = true;
		tail_gap = true;  // foom
		emit positionUpdatesInterrupted();
		updateMapWidget();
	}
}

MapCoordF GPSDisplay::calcLatestGPSCoord(bool& ok)
{
#if defined(QT_POSITIONING_LIB)
	if (!has_valid_position)
	{
		ok = false;
		return latest_gps_coord;
	}
	if (!gps_updated)
	{
		ok = true;
		return latest_gps_coord;
	}
	
	const auto latest_pos_info = source->lastKnownPosition(true);
	latest_gps_coord_accuracy = latest_pos_info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)
	                            ? float(latest_pos_info.attribute(QGeoPositionInfo::HorizontalAccuracy))
	                            : -1;
	
	QGeoCoordinate qgeo_coord = latest_pos_info.coordinate();
	if (!qgeo_coord.isValid())
	{
		ok = false;
		return latest_gps_coord;
	}
	
	LatLon latlon(qgeo_coord.latitude(), qgeo_coord.longitude());
	latest_gps_coord = georeferencing.toMapCoordF(latlon, &ok);
	if (!ok)
	{
		qDebug("GPSDisplay::calcLatestGPSCoord(): Cannot convert LatLon to MapCoordF!");
		return latest_gps_coord;
	}
	
	// foom: the offset of the position
	raw_gps_coord = latest_gps_coord;
	has_raw_gps_coord = true;
	if (has_position_offset)
		latest_gps_coord = MapCoordF(raw_gps_coord.x() + position_offset.x(), raw_gps_coord.y() + position_offset.y());
	
	gps_updated = false;
	ok = true;
#else
	ok = has_valid_position;
#endif
	return latest_gps_coord;
}

void GPSDisplay::updateMapWidget()
{
	// TODO: Limit update region to union of old and new bounding rect
	widget->update();
}


}  // namespace OpenOrienteering
