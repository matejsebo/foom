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


#ifndef FOOM_RANGEFINDER_SOURCE_H
#define FOOM_RANGEFINDER_SOURCE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/rangefinder.h"

class QIODevice;
class QTcpSocket;

namespace OpenOrienteering {


/**
 * Reads the lines which a rangefinder sends and gives its readings on.
 *
 * The port is the name of a serial port, or a text of the form host:port
 * for a network port. When the device goes away, the source closes and
 * keeps the reason in errorString(); it does not connect again by itself.
 *
 * See the design notes, "Readings from a device".
 */
class RangefinderSource : public QObject
{
Q_OBJECT
public:
	explicit RangefinderSource(QObject* parent = nullptr);
	~RangefinderSource() override;
	
	RangefinderSource(const RangefinderSource&) = delete;
	RangefinderSource& operator=(const RangefinderSource&) = delete;
	
	/** The names of the serial ports of this computer. */
	static QStringList serialPorts();
	
	/**
	 * Opens the port; a port which was open is closed first.
	 *
	 * A serial port is open when this returns true. A network port is
	 * then being connected: stateChanged() says when it is open, or
	 * that it could not be reached. Returns false, with errorString(),
	 * when the port cannot be opened.
	 */
	bool open(const QString& port, int baud);
	
	/** Closes the port. */
	void close();
	
	/** Returns true while lines can arrive. */
	bool isOpen() const { return opened; }
	/** Returns true while a network port is being connected. */
	bool isConnecting() const { return device && !opened; }
	/** Why the port could not be opened or was lost; empty after open() succeeded and after close(). */
	const QString& errorString() const { return error_text; }
	
	/** Takes a line as if it had been read from the device. */
	void processLine(const QByteArray& line);
	
	int readingCount() const { return parser.readingCount(); }
	int otherCount() const { return parser.otherCount(); }
	
signals:
	void readingReceived(const OpenOrienteering::RangefinderReading& reading);
	/** A line was counted. */
	void countsChanged();
	/** The port was opened, closed or lost. */
	void stateChanged();
	
private:
	void readLines();
	void deviceLost(const QString& reason);
	
	RangefinderSentenceParser parser;
	QIODevice* device = nullptr;
	QTcpSocket* socket = nullptr;
	QString error_text;
	bool opened = false;
};


}  // namespace OpenOrienteering

#endif
