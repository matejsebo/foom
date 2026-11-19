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


#include "rangefinder_source.h"

#include <QtGlobal>
#include <QAbstractSocket>
#include <QIODevice>
#include <QLatin1Char>
#include <QNetworkProxy>
#include <QTcpSocket>

#ifdef QT_SERIALPORT_LIB
#  include <QSerialPort>
#  include <QSerialPortInfo>
#endif


namespace OpenOrienteering {

RangefinderSource::RangefinderSource(QObject* parent)
: QObject(parent)
{
	// nothing else
}

RangefinderSource::~RangefinderSource()
{
	if (device)
		device->disconnect(this);
}


// static
QStringList RangefinderSource::serialPorts()
{
	QStringList result;
#ifdef QT_SERIALPORT_LIB
	auto const ports = QSerialPortInfo::availablePorts();
	for (auto const& port : ports)
		result.push_back(port.portName());
#endif
	return result;
}

bool RangefinderSource::open(const QString& port, int baud)
{
	close();
	error_text.clear();
	
	auto const name = port.trimmed();
	if (name.isEmpty())
	{
		error_text = tr("No port is given.");
		emit stateChanged();
		return false;
	}
	
	auto const colon = name.lastIndexOf(QLatin1Char(':'));
	if (colon > 0)
	{
		// A network port: host:port.
		auto host = name.left(colon).trimmed();
		if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']')))
			host = host.mid(1, host.length() - 2);
		auto ok = false;
		auto const number = name.mid(colon + 1).toInt(&ok);
		if (host.isEmpty() || !ok || number < 1 || number > 65535)
		{
			error_text = tr("%1 is not a network port.").arg(name);
			emit stateChanged();
			return false;
		}
		socket = new QTcpSocket(this);
		// A device server is a device nearby: no proxy of the system stands between.
		socket->setProxy(QNetworkProxy::NoProxy);
		connect(socket, &QAbstractSocket::connected, this, [this]() {
			opened = true;
			emit stateChanged();
		});
		connect(socket, &QIODevice::readyRead, this, &RangefinderSource::readLines);
		auto const lost = [this]() {
			if (socket)
				deviceLost(socket->errorString());
		};
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
		connect(socket, &QAbstractSocket::errorOccurred, this, lost);
#else
		connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, lost);
#endif
		connect(socket, &QAbstractSocket::disconnected, this, [this]() {
			deviceLost(tr("The connection was closed."));
		});
		device = socket;
		socket->connectToHost(host, quint16(number), QIODevice::ReadOnly);
		emit stateChanged();
		return true;
	}
	
#ifdef QT_SERIALPORT_LIB
	auto* serial = new QSerialPort(name, this);
	serial->setBaudRate(baud > 0 ? baud : 4800);
	serial->setDataBits(QSerialPort::Data8);
	serial->setParity(QSerialPort::NoParity);
	serial->setStopBits(QSerialPort::OneStop);
	if (!serial->open(QIODevice::ReadOnly))
	{
		error_text = serial->errorString();
		delete serial;
		emit stateChanged();
		return false;
	}
	connect(serial, &QIODevice::readyRead, this, &RangefinderSource::readLines);
	connect(serial, &QSerialPort::errorOccurred, this, [this, serial](QSerialPort::SerialPortError serial_error) {
		// The port is gone, for example a rangefinder which was switched off.
		if (serial_error == QSerialPort::ResourceError || serial_error == QSerialPort::ReadError)
			deviceLost(serial->errorString());
	});
	device = serial;
	opened = true;
	emit stateChanged();
	return true;
#else
	Q_UNUSED(baud)
	error_text = tr("Serial ports cannot be used in this program.");
	emit stateChanged();
	return false;
#endif
}

void RangefinderSource::close()
{
	if (!device)
		return;
	device->disconnect(this);
	if (socket)
		socket->abort();
	else
		device->close();
	device->deleteLater();
	device = nullptr;
	socket = nullptr;
	opened = false;
	error_text.clear();
	emit stateChanged();
}

void RangefinderSource::processLine(const QByteArray& line)
{
	auto const result = parser.feed(line);
	if (result == RangefinderSentenceParser::NoLine)
		return;
	if (result == RangefinderSentenceParser::Reading)
		emit readingReceived(parser.reading());
	emit countsChanged();
}


void RangefinderSource::readLines()
{
	while (device && device->canReadLine())
		processLine(device->readLine(1024));
	// No line end in sight: this is not a rangefinder which sends sentences.
	if (device && device->bytesAvailable() > 4096)
		device->readAll();
}

void RangefinderSource::deviceLost(const QString& reason)
{
	if (!device)
		return;
	// This is called from a signal of the device: it is deleted later.
	device->disconnect(this);
	device->deleteLater();
	device = nullptr;
	socket = nullptr;
	opened = false;
	error_text = reason;
	emit stateChanged();
}


}  // namespace OpenOrienteering
