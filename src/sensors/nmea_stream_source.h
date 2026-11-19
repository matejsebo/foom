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


#ifndef FOOM_NMEA_STREAM_SOURCE_H
#define FOOM_NMEA_STREAM_SOURCE_H

#include <QtGlobal>
#include <QGeoPositionInfoSource>
#include <QNmeaPositionInfoSource>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "nmea_fix.h"
#include "nmea_log.h"

class QEvent;
class QGeoPositionInfo;
class QIODevice;
class QTcpSocket;

namespace OpenOrienteering {


/**
 * A position source which reads NMEA sentences from a network receiver
 * (TCP) or from a serial port.
 *
 * Unlike the source in nmea_position_plugin.cpp, it needs no /bin/cat and
 * so runs on Windows. When the connection cannot be made or drops, it
 * reports ClosedError and tries again until updates are stopped.
 *
 * Parameters: nmea.tcp (host:port), nmea.baud, nmea.reconnect (milliseconds).
 *
 * See the design notes, "NMEA source over TCP and on Windows".
 */
class NmeaStreamSource : public QNmeaPositionInfoSource
{
Q_OBJECT
public:
	/**
	 * Reads host and port from a text like "host:port" or "[::1]:port".
	 */
	static bool parseHostPort(const QString& text, QString* host, quint16* port);
	
	/**
	 * Creates the source for the network receiver named by the parameters,
	 * or else for the serial port. An empty name means the first port.
	 */
	NmeaStreamSource(const QVariantMap& parameters, const QString& serial_port, QObject* parent);
	~NmeaStreamSource() override;
	
	NmeaStreamSource(const NmeaStreamSource&) = delete;
	NmeaStreamSource& operator=(const NmeaStreamSource&) = delete;
	
	void startUpdates() override;
	void stopUpdates() override;
	
	QGeoPositionInfoSource::Error error() const override;
	using QGeoPositionInfoSource::error;  // the signal
	
	/** Takes the folder of the sentence log from the property NmeaLog::folderProperty(). */
	bool event(QEvent* event) override;
	
protected:
	/** Reads the quality fields of GGA and GST in addition to the position, and writes the log. */
	bool parsePosInfoFromNmeaData(const char* data, int size, QGeoPositionInfo* posInfo, bool* hasFix) override;
	
private:
	void openDevice();
	void deviceOpened();
	void deviceLost();
	void reportError(QGeoPositionInfoSource::Error value);
	
	QTcpSocket* socket = nullptr;
	QIODevice* serial = nullptr;
	QString host;
	quint16 port = 0;
	QTimer reconnect_timer;
	NmeaFixParser fix_parser;
	NmeaLog sentence_log;
	Error position_error = NoError;
	bool running = false;
	bool base_started = false;
};


}  // namespace OpenOrienteering

#endif
