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


#ifndef FOOM_NMEA_LOG_H
#define FOOM_NMEA_LOG_H

#include <QByteArray>
#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QString>

class QEvent;
class QObject;

namespace OpenOrienteering {


/**
 * The log of the sentences which a receiver sends.
 *
 * Every line is appended as it came to the file gnss-YYYY-MM-DD.nmea in
 * a folder, the date being the day on which the line arrives. Without a
 * folder nothing is written.
 *
 * A position source which reads sentences keeps one log. The folder is
 * handed to the source as its dynamic property folderProperty(), and the
 * source says what it does in fileProperty() and errorProperty().
 *
 * See the design notes, "Position offset and sentence log".
 */
class NmeaLog
{
public:
	/** The property of a source which holds the folder of its log. */
	static const char* folderProperty() { return "foomSentenceLogFolder"; }
	/** The property of a source which names the file it writes, or is empty. */
	static const char* fileProperty() { return "foomSentenceLogFile"; }
	/** The property of a source which names the file it cannot write, or is empty. */
	static const char* errorProperty() { return "foomSentenceLogError"; }
	
	/** The name of the file for a day. */
	static QString fileName(const QDate& date);
	
	NmeaLog();
	~NmeaLog();
	
	NmeaLog(const NmeaLog&) = delete;
	NmeaLog& operator=(const NmeaLog&) = delete;
	
	const QString& folder() const { return log_folder; }
	/** Sets the folder; an empty text ends the log. */
	void setFolder(const QString& folder);
	
	/**
	 * Appends the line to the file of the day.
	 *
	 * Returns false when the file cannot be written. It is then tried again
	 * with the first line which comes more than 5 seconds later.
	 */
	bool write(const QByteArray& line, const QDate& date = QDate::currentDate());
	
	/** The file which is written, or which could not be written; empty before the first line. */
	const QString& filePath() const { return file_path; }
	/** Returns true when the file could not be written. */
	bool hasError() const { return failed; }
	
	/** Closes the file. The next line opens it again. */
	void close();
	
	/** For the event() of a source: takes the folder when its property changes. */
	void propertyEvent(const QEvent* event, QObject* source);
	/** For a source: writes the line and says in the properties what happened. */
	void logLine(const QByteArray& line, QObject* source);
	
private:
	bool open(const QDate& date);
	void fail();
	void publish(QObject* source) const;
	
	QFile file;
	QString log_folder;
	QString file_path;
	QDate file_date;
	QElapsedTimer retry_timer;
	bool failed = false;
};


}  // namespace OpenOrienteering

#endif
