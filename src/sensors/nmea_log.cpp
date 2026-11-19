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


#include "nmea_log.h"

#include <QtGlobal>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QIODevice>
#include <QObject>
#include <QVariant>


namespace OpenOrienteering {

namespace {

/// The time after which a file which could not be written is tried again.
constexpr qint64 retry_interval = 5000;  // milliseconds

}  // namespace



// static
QString NmeaLog::fileName(const QDate& date)
{
	return QStringLiteral("gnss-") + date.toString(Qt::ISODate) + QStringLiteral(".nmea");
}


NmeaLog::NmeaLog() = default;

NmeaLog::~NmeaLog() = default;


void NmeaLog::setFolder(const QString& folder)
{
	if (folder == log_folder)
		return;
	file.close();
	log_folder = folder;
	file_path.clear();
	failed = false;
}

bool NmeaLog::write(const QByteArray& line, const QDate& date)
{
	if (log_folder.isEmpty())
		return true;
	if (failed && retry_timer.isValid() && retry_timer.elapsed() < retry_interval)
		return false;
	if ((!file.isOpen() || file_date != date) && !open(date))
		return false;
	if (file.write(line) != line.size() || !file.flush())
	{
		fail();
		return false;
	}
	return true;
}

void NmeaLog::close()
{
	file.close();
}

void NmeaLog::propertyEvent(const QEvent* event, QObject* source)
{
	if (event->type() != QEvent::DynamicPropertyChange)
		return;
	if (static_cast<const QDynamicPropertyChangeEvent*>(event)->propertyName() != QByteArray(folderProperty()))
		return;
	setFolder(source->property(folderProperty()).toString());
	publish(source);
}

void NmeaLog::logLine(const QByteArray& line, QObject* source)
{
	if (log_folder.isEmpty())
		return;
	write(line);
	publish(source);
}


bool NmeaLog::open(const QDate& date)
{
	file.close();
	file_date = date;
	file_path = QDir(log_folder).filePath(fileName(date));
	file.setFileName(file_path);
	// The bytes go to the file as they came: no text mode.
	if (!QDir().mkpath(log_folder) || !file.open(QIODevice::WriteOnly | QIODevice::Append))
	{
		fail();
		return false;
	}
	failed = false;
	return true;
}

void NmeaLog::fail()
{
	file.close();
	failed = true;
	retry_timer.start();
}

void NmeaLog::publish(QObject* source) const
{
	auto const written = (!failed && !log_folder.isEmpty()) ? file_path : QString{};
	auto const refused = failed ? file_path : QString{};
	if (source->property(fileProperty()).toString() != written)
		source->setProperty(fileProperty(), written);
	if (source->property(errorProperty()).toString() != refused)
		source->setProperty(errorProperty(), refused);
}


}  // namespace OpenOrienteering
