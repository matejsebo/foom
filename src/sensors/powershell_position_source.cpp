/*
 *    Copyright 2019-2020 Kai Pastor
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

#include "powershell_position_source.h"

#include <algorithm>
#include <iterator>

#include <Qt>
#include <QtGlobal>
#include <QtNumeric>
#include <QChar>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QGeoCoordinate>
#include <QIODevice>
#include <QStandardPaths>
#include <QStringList>  // IWYU pragma: keep


inline void initPowershellPositionResources()
{
#ifdef Q_OS_WIN
	// This resource is to be linked under Windows only.
	Q_INIT_RESOURCE(powershell_position_source);
#endif
}


namespace OpenOrienteering
{

namespace {

/**
 * A utility to continuously read fields separated by ';'
 *
 * The data is assumed to use Latin-1 encoding.
 */
class CsvFieldReader
{
private:
	const QByteArray& line;
	int pos = 0;
	
public:
	CsvFieldReader(const QByteArray& line, int pos = 0)
	: line(line)
	, pos(std::max(0, std::min(pos, line.size())))
	{}
	
	bool atEnd() const
	{
		return pos < 0;
	}
	
	QByteArray readByteArray()
	{
		auto next_pos = line.indexOf(';', pos);
		auto result = QByteArray::fromRawData(line.constData() + pos, (next_pos < 0 ? line.size() : next_pos) - pos);
		pos = next_pos > 0 ? next_pos + 1 : -1;
		return result;
	}
	
	QString readString()
	{
		auto next_pos = line.indexOf(';', pos);
		auto result = QString::fromLatin1(line.constData() + pos, (next_pos < 0 ? line.size() : next_pos) - pos);
		pos = next_pos > 0 ? next_pos + 1 : -1;
		return result;
	}
	
	template<typename T>
	QDateTime readDateTime(T format_or_spec)
	{
		return QDateTime::fromString(readString(), format_or_spec);
	}
	
	double readDouble(bool* ok = nullptr)
	{
		return readString().toDouble(ok);
	}
	
};


/**
 * Returns the main Powershell script.
 */
QByteArray startScript()
{
	QByteArray script;
	initPowershellPositionResources();
	QFile script_file(QStringLiteral(":/sensors/powershell_position_source.ps1"));
	if (script_file.open(QIODevice::ReadOnly))
		script = script_file.readAll();
	if (script_file.error() != QFileDevice::NoError)
		script.clear();
	return script;
}

/**
 * Configures the process for accessing the Windows Location API via powershell.
 */
QGeoPositionInfoSource::Error defaultSetup(QProcess& process, QByteArray& start_script, QByteArray& stop_script)
{
	start_script = startScript();
	if (start_script.isEmpty())
		return QGeoPositionInfoSource::UnknownSourceError;
	
	stop_script = QByteArrayLiteral("Exit 0 \r\n");
	
	auto const powershell_path = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
	if (powershell_path.isEmpty())
		return QGeoPositionInfoSource::UnknownSourceError;
	
	process.setProgram(powershell_path);
	process.setArguments(QStringLiteral("-NoLogo -NoProfile -NonInteractive -Command -").split(QChar::Space));
	return QGeoPositionInfoSource::NoError;
}


}  // namespace


PowershellPositionSource::PowershellPositionSource(QObject* parent)
: PowershellPositionSource(defaultSetup, parent)
{}

PowershellPositionSource::PowershellPositionSource(SetupFunction& setup, QObject* parent)
: QGeoPositionInfoSource(parent)
{
	setError(setup(powershell, start_script, stop_script));
	if (position_error != QGeoPositionInfoSource::NoError)
	{
		source_status = Status::SetupFailed;  // foom
		return;
	}
	
	powershell.setReadChannel(QProcess::StandardOutput);
	
	connect(&powershell, &QProcess::stateChanged, this, &PowershellPositionSource::powershellStateChanged);
	connect(&powershell, &QProcess::readyReadStandardOutput, this, &PowershellPositionSource::readStandardOutput);
	connect(&powershell, &QProcess::readyReadStandardError, this, &PowershellPositionSource::readStandardError);
	
	periodic_update_timer.setSingleShot(true);
	connect(&periodic_update_timer, &QTimer::timeout, this, &PowershellPositionSource::periodicUpdateTimeout);
	
	single_update_timer.setSingleShot(true);
	connect(&single_update_timer, &QTimer::timeout, this, &PowershellPositionSource::singleUpdateTimeout);
}

PowershellPositionSource::~PowershellPositionSource()
{
	powershell.disconnect(this);
	if (powershell.state() != QProcess::NotRunning)
		powershell.kill();
	powershell.waitForFinished(100);
}


QGeoPositionInfoSource::Error PowershellPositionSource::error() const
{
	return position_error;
}

void PowershellPositionSource::setError(QGeoPositionInfoSource::Error value)
{
	if (this->position_error == value)
		return;
	
	this->position_error = value;
	switch (value)
	{
	case NoError:
		break;
	case AccessError:
		emit this->QGeoPositionInfoSource::error(value);
		// Exit gracefully
		powershell.write(stop_script);
		break;
	default:
		emit this->QGeoPositionInfoSource::error(value);
		break;
	}
}


// foom
PowershellPositionSource::Status PowershellPositionSource::status() const
{
	return source_status;
}

// foom
QString PowershellPositionSource::statusText() const
{
	auto text = QString{};
	switch (source_status)
	{
	case Status::NotStarted:
		text = QStringLiteral("The helper process is not running.");
		break;
	case Status::HelperStarting:
		text = QStringLiteral("The helper process was started and has not reported yet.");
		break;
	case Status::Listening:
		text = QStringLiteral("The helper process runs, and Windows has not reported the status of the location service.");
		break;
	case Status::Initializing:
		text = QStringLiteral("The location providers of Windows are initializing.");
		break;
	case Status::Ready:
		text = QStringLiteral("The location service of Windows is ready.");
		break;
	case Status::NoData:
		text = QStringLiteral("The location service of Windows has no data from any location provider.");
		break;
	case Status::Disabled:
		text = QStringLiteral("The location service of Windows is turned off.");
		break;
	case Status::Denied:
		text = QStringLiteral("Windows does not allow this program to use the location.");
		break;
	case Status::SetupFailed:
		text = QStringLiteral("The helper script or powershell.exe was not found.");
		break;
	case Status::HelperNotStarted:
		text = QStringLiteral("The helper process could not be started: %1").arg(powershell.errorString());
		break;
	case Status::HelperFailed:
		text = QStringLiteral("The helper process ended abnormally: %1").arg(powershell.errorString());
		break;
	case Status::BadData:
		text = QStringLiteral("The helper process wrote a position or a status which cannot be read.");
		break;
	}
	if (!helper_error_output.isEmpty())
	{
		text += QStringLiteral(" Error output of the helper process: ");
		text += QString::fromLocal8Bit(helper_error_output).trimmed();
	}
	return text;
}


QGeoPositionInfo PowershellPositionSource::lastKnownPosition(bool /* satellite_only */) const
{
	return last_position;
}

QGeoPositionInfoSource::PositioningMethods PowershellPositionSource::supportedPositioningMethods() const
{
	switch (position_error)
	{
	case NoError:
		return AllPositioningMethods;
	default:
		return NoPositioningMethods;
	}
}

int PowershellPositionSource::minimumUpdateInterval() const
{
	return 1000;
}

// slot
void PowershellPositionSource::startUpdates()
{
	if (!startPowershell())
		return;
	
	updates_ongoing = true;
	// Grant some extra delay for the update to arrive.
	periodic_update_timer.setInterval(std::max(1500, updateInterval()));
	periodic_update_timer.start();
}

// slot
void PowershellPositionSource::stopUpdates()
{
	if (!updates_ongoing)
		return;
	
	updates_ongoing = false;
	periodic_update_timer.stop();
	// Exit gracefully
	powershell.write(stop_script);
}

// slot
void PowershellPositionSource::requestUpdate(int timeout)
{
	if (!startPowershell())
		return;
	
	setError(QGeoPositionInfoSource::NoError);
	if (timeout == 0)
	{
		timeout = 120000; // 2 min for cold start
	}
	else if (timeout < minimumUpdateInterval())
	{
		emit updateTimeout();
		return;
	}
	
	single_update_timer.start(timeout);
}


bool PowershellPositionSource::startPowershell()
{
	if (powershell.state() != QProcess::NotRunning)
		return true;
	
	helper_error_output.clear();  // foom
	powershell.start();
	if (powershell.state() == QProcess::NotRunning)
	{
		source_status = Status::HelperNotStarted;  // foom
		setError(QGeoPositionInfoSource::UnknownSourceError);
		return false;
	}
	source_status = Status::HelperStarting;  // foom
	
	return true;
}

void PowershellPositionSource::powershellStateChanged(QProcess::ProcessState new_state)
{
	switch (new_state)
	{
	case QProcess::Starting:
		// nothing
		break;
	case QProcess::Running:
		powershell.write(start_script);
		break;
	case QProcess::NotRunning:
		updates_ongoing = false;
		periodic_update_timer.stop();
		single_update_timer.stop();
		if (powershell.exitStatus() != QProcess::NormalExit)
		{
			source_status = Status::HelperFailed;  // foom
			setError(UnknownSourceError);
		}
		else  // foom: a status which says why there is no position stays
		{
			switch (source_status)
			{
			case Status::HelperStarting:
			case Status::Listening:
			case Status::Initializing:
			case Status::Ready:
			case Status::NoData:
				source_status = Status::NotStarted;
				break;
			default:
				break;
			}
		}
		break;
	}
}

void PowershellPositionSource::readStandardError()
{
	auto const output = powershell.readAllStandardError();
	// foom: kept for statusText(); the beginning, where the first error is
	helper_error_output += output;
	helper_error_output.truncate(2000);
	qWarning("PowershellPositionSource: Error output:\r\n%s", output.data());
}

void PowershellPositionSource::readStandardOutput()
{
	while (powershell.canReadLine())
	{
		auto const line = powershell.readLine(100).trimmed();
		if (line.startsWith("Position;"))
			processPosition(line);
		else if (line.startsWith("Status;"))
			processStatus(line);
		else if (line.startsWith("Permission;"))
			processPermission(line);
		else if (!line.isEmpty())
			qDebug("PowershellPositionSource: Bogus data: '%s'", line.data());
	}
}

void PowershellPositionSource::processPosition(const QByteArray& line)
{
	Q_ASSERT(line.startsWith("Position;"));
	auto reader = CsvFieldReader(line, 9);
	auto const date_time  = reader.readDateTime(Qt::ISODate);
	bool numbers_ok[5];
	auto latitude   = reader.readDouble(&numbers_ok[0]);
	auto longitude  = reader.readDouble(&numbers_ok[1]);
	auto altitude   = reader.readDouble(&numbers_ok[2]);
	auto const h_accuracy = reader.readDouble(&numbers_ok[3]);
	auto const v_accuracy = reader.readDouble(&numbers_ok[4]);
	
	using std::begin; using std::end;
	if (std::find(begin(numbers_ok), end(numbers_ok), false) != end(numbers_ok)
	    || !date_time.isValid())
	{
		qDebug("PowershellPositionSource: Could not parse location '%s'", line.data());
		source_status = Status::BadData;  // foom
		setError(UnknownSourceError);
		return;
	}
	
	if (qIsNaN(h_accuracy))
	{
		qDebug("PowershellPositionSource: Horizontal accuracy unknown");
		return;
	}
	
	auto geo_coord = QGeoCoordinate{latitude, longitude};
	if (!qIsNaN(v_accuracy))
		geo_coord.setAltitude(altitude);
	
	auto position = QGeoPositionInfo{geo_coord, date_time};
	position.setAttribute(QGeoPositionInfo::HorizontalAccuracy, h_accuracy);
	if (!qIsNaN(v_accuracy))
		position.setAttribute(QGeoPositionInfo::VerticalAccuracy, v_accuracy);
	
	nativePositionUpdate(position);
}

void PowershellPositionSource::processStatus(const QByteArray& line)
{
	Q_ASSERT(line.startsWith("Status;"));
	auto reader = CsvFieldReader(line, 7);
	auto value = reader.readByteArray();
	// Cf. https://docs.microsoft.com/en-US/dotnet/api/system.device.location.geopositionstatus
	if (value == "Disabled")
	{
		source_status = Status::Disabled;  // foom
		setError(AccessError);
	}
	else if (value == "Initializing")
	{
		source_status = Status::Initializing;  // foom
		setError(NoError);
	}
	else if (value == "NoData")
	{
		// foom: Windows reports this at the start, too, until a provider has
		// a position. The next status or position clears the error.
		source_status = Status::NoData;
		setError(UnknownSourceError);
	}
	else if (value == "Ready")
	{
		source_status = Status::Ready;  // foom
		setError(NoError);
	}
	else
	{
		qDebug("PowershellPositionSource: Unknown status '%s'", line.data()+7);
		source_status = Status::BadData;  // foom
		setError(UnknownSourceError);
	}
}

void PowershellPositionSource::processPermission(const QByteArray& line)
{
	Q_ASSERT(line.startsWith("Permission;"));
	auto reader = CsvFieldReader(line, 11);
	auto value = reader.readByteArray();
	// Cf. https://docs.microsoft.com/en-US/dotnet/api/system.device.location.geopositionpermission
	if (value == "Denied")
	{
		source_status = Status::Denied;  // foom
		setError(AccessError);
	}
	else if (value == "Granted")
	{
		helperReported();  // foom
	}
	else if (value == "Unknown")
	{
		helperReported();  // foom
	}
	else
	{
		qDebug("PowershellPositionSource: Unknown permission '%s'", line.data()+11);
		// nothing else
	}
}

// foom
void PowershellPositionSource::helperReported()
{
	if (source_status == Status::HelperStarting)
		source_status = Status::Listening;
}

void PowershellPositionSource::nativePositionUpdate(const QGeoPositionInfo& position)
{
	periodic_update_timer.stop();
	single_update_timer.stop();
	last_position = position;
	source_status = Status::Ready;  // foom: a position is data
	setError(NoError);
	emit positionUpdated(last_position);
	
	if (updates_ongoing)
		periodic_update_timer.start();
}

void PowershellPositionSource::periodicUpdateTimeout()
{
	if (last_position.isValid())
	{
		// Emit virtual position update
		last_position.setTimestamp(last_position.timestamp().addMSecs(updateInterval()));
		emit positionUpdated(last_position);
	}
	periodic_update_timer.start();
}

void PowershellPositionSource::singleUpdateTimeout()
{
	emit updateTimeout();
}

}  // namespace OpenOrienteering
