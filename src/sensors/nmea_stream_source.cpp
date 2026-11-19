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


#include "nmea_stream_source.h"

#include <algorithm>

#include <QAbstractSocket>
#include <QByteArray>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QVariant>

#ifdef QT_SERIALPORT_LIB
#  include <QSerialPort>
#  include <QSerialPortInfo>
#endif


namespace OpenOrienteering {

// static
bool NmeaStreamSource::parseHostPort(const QString& text, QString* host, quint16* port)
{
	auto const trimmed = text.trimmed();
	QString host_part;
	QString port_part;
	if (trimmed.startsWith(QLatin1Char('[')))
	{
		// An IPv6 address in brackets.
		auto const end = trimmed.indexOf(QLatin1String("]:"));
		if (end < 0)
			return false;
		host_part = trimmed.mid(1, end - 1);
		port_part = trimmed.mid(end + 2);
	}
	else
	{
		if (trimmed.count(QLatin1Char(':')) != 1)
			return false;
		auto const colon = trimmed.indexOf(QLatin1Char(':'));
		host_part = trimmed.left(colon);
		port_part = trimmed.mid(colon + 1);
	}
	
	auto ok = false;
	auto const number = port_part.toInt(&ok);
	if (host_part.trimmed().isEmpty() || !ok || number < 1 || number > 65535)
		return false;
	
	if (host)
		*host = host_part.trimmed();
	if (port)
		*port = quint16(number);
	return true;
}


NmeaStreamSource::NmeaStreamSource(const QVariantMap& parameters, const QString& serial_port, QObject* parent)
: QNmeaPositionInfoSource(QNmeaPositionInfoSource::RealTimeMode, parent)
{
	auto ok = false;
	auto interval = parameters.value(QStringLiteral("nmea.reconnect")).toInt(&ok);
	if (!ok || interval <= 0)
		interval = 5000;
	reconnect_timer.setSingleShot(true);
	reconnect_timer.setInterval(std::max(10, interval));
	connect(&reconnect_timer, &QTimer::timeout, this, &NmeaStreamSource::openDevice);
	
	auto const tcp = parameters.value(QStringLiteral("nmea.tcp")).toString().trimmed();
	if (!tcp.isEmpty())
	{
		if (!parseHostPort(tcp, &host, &port))
		{
			position_error = UnknownSourceError;
			return;
		}
		socket = new QTcpSocket(this);
		// A receiver is a device nearby: no proxy of the system stands between.
		socket->setProxy(QNetworkProxy::NoProxy);
		connect(socket, &QAbstractSocket::connected, this, &NmeaStreamSource::deviceOpened);
		connect(socket, &QAbstractSocket::disconnected, this, &NmeaStreamSource::deviceLost);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
		connect(socket, &QAbstractSocket::errorOccurred, this, &NmeaStreamSource::deviceLost);
#else
		connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &NmeaStreamSource::deviceLost);
#endif
		setDevice(socket);
		return;
	}
	
#ifdef QT_SERIALPORT_LIB
	auto name = serial_port.trimmed();
	if (name.isEmpty())
	{
		auto const ports = QSerialPortInfo::availablePorts();
		if (!ports.empty())
			name = ports.front().portName();
	}
	if (name.isEmpty())
	{
		position_error = UnknownSourceError;
		return;
	}
	
	auto baud = parameters.value(QStringLiteral("nmea.baud")).toInt(&ok);
	if (!ok || baud <= 0)
		baud = 4800;
	auto* serial_device = new QSerialPort(name, this);
	serial_device->setBaudRate(baud);
	serial_device->setDataBits(QSerialPort::Data8);
	serial_device->setParity(QSerialPort::NoParity);
	serial_device->setStopBits(QSerialPort::OneStop);
	connect(serial_device, &QSerialPort::errorOccurred, this, [this, serial_device](QSerialPort::SerialPortError serial_error) {
		// The port is gone, for example an unplugged receiver.
		if (serial_error == QSerialPort::ResourceError || serial_error == QSerialPort::ReadError)
		{
			if (serial_device->isOpen())
				serial_device->close();
			deviceLost();
		}
	});
	serial = serial_device;
	setDevice(serial);
#else
	Q_UNUSED(serial_port)
	position_error = UnknownSourceError;
#endif
}

NmeaStreamSource::~NmeaStreamSource()
{
	running = false;
	reconnect_timer.stop();
	if (socket)
	{
		socket->disconnect(this);
		socket->abort();
	}
}


void NmeaStreamSource::startUpdates()
{
	if (!socket && !serial)
		return;
	running = true;
	openDevice();
}

void NmeaStreamSource::stopUpdates()
{
	running = false;
	reconnect_timer.stop();
	if (base_started)
	{
		base_started = false;
		QNmeaPositionInfoSource::stopUpdates();
	}
	if (socket)
		socket->abort();
	else if (serial && serial->isOpen())
		serial->close();
	sentence_log.close();
}

QGeoPositionInfoSource::Error NmeaStreamSource::error() const
{
	return position_error;
}

bool NmeaStreamSource::event(QEvent* event)
{
	sentence_log.propertyEvent(event, this);
	return QNmeaPositionInfoSource::event(event);
}

bool NmeaStreamSource::parsePosInfoFromNmeaData(const char* data, int size, QGeoPositionInfo* posInfo, bool* hasFix)
{
	sentence_log.logLine(QByteArray::fromRawData(data, size), this);
	auto const result = fix_parser.feed(QByteArray::fromRawData(data, size));
	if (result == NmeaFixParser::Gga || result == NmeaFixParser::Gst)
		setProperty(FixInfo::propertyName(), QVariant::fromValue(fix_parser.fix()));
	return QNmeaPositionInfoSource::parsePosInfoFromNmeaData(data, size, posInfo, hasFix);
}


void NmeaStreamSource::openDevice()
{
	if (!running)
		return;
	
	if (socket)
	{
		// While an attempt is under way, its end will bring us here again.
		if (socket->state() == QAbstractSocket::UnconnectedState)
			socket->connectToHost(host, port, QIODevice::ReadOnly);
	}
	else if (serial)
	{
		if (serial->isOpen() || serial->open(QIODevice::ReadOnly))
		{
			deviceOpened();
		}
		else
		{
			reportError(AccessError);
			reconnect_timer.start();
		}
	}
}

void NmeaStreamSource::deviceOpened()
{
	reconnect_timer.stop();
	position_error = NoError;
	if (running && !base_started)
	{
		// The base class takes the device only when it is open.
		base_started = true;
		QNmeaPositionInfoSource::startUpdates();
	}
}

void NmeaStreamSource::deviceLost()
{
	if (!running)
		return;
	reportError(ClosedError);
	if (!reconnect_timer.isActive())
		reconnect_timer.start();
}

void NmeaStreamSource::reportError(QGeoPositionInfoSource::Error value)
{
	if (position_error == value)
		return;
	position_error = value;
	if (value != NoError)
		emit QGeoPositionInfoSource::error(value);
}


}  // namespace OpenOrienteering
