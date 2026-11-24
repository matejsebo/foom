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

#ifndef OPENORIENTEERING_POWERSHELL_POSITION_SOURCE_H
#define OPENORIENTEERING_POWERSHELL_POSITION_SOURCE_H

#include <QByteArray>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace OpenOrienteering
{

/**
 * A Windows position source based on Powershell's access to Windows.Device.Location.
 * 
 * @see QGeoPositionInfoSourceWinRT
 */
class PowershellPositionSource : public QGeoPositionInfoSource
{
	Q_OBJECT
	
public:
	/**
	 * A setup function configures the process which this source reads from,
	 * the start script and the stop script.
	 */
	using SetupFunction = QGeoPositionInfoSource::Error (QProcess&, QByteArray&, QByteArray&);
	
	
	/**
	 * Constructs a source with the default Powershell script.
	 */
	PowershellPositionSource(QObject* parent = nullptr);
	
	/**
	 * Constructs a source with the given Powershell script.
	 */
	PowershellPositionSource(SetupFunction& setup, QObject* parent = nullptr);
	
	PowershellPositionSource(const PowershellPositionSource&) = delete;
	PowershellPositionSource(PowershellPositionSource&&) = delete;
	PowershellPositionSource& operator=(const PowershellPositionSource&) = delete;
	PowershellPositionSource& operator=(PowershellPositionSource&&) = delete;
	
	~PowershellPositionSource() override;
	
	
	QGeoPositionInfoSource::Error error() const override;
	
	using QGeoPositionInfoSource::error;  // the signal
	
	
	/**
	 * What the source knows about the helper process and the location service.
	 * (foom)
	 * 
	 * error() cannot tell these apart. It is UnknownSourceError for NoData,
	 * which Windows reports when updates start, before a location provider
	 * has a position, and which ends by itself, and it is the same value for
	 * a helper process which cannot work at all.
	 */
	enum class Status
	{
		NotStarted,        ///< No helper process is running, and none has failed.
		HelperStarting,    ///< The helper process was started and has not reported yet.
		Listening,         ///< The helper reported the permission; no status from Windows yet.
		Initializing,      ///< Windows: the location providers are initializing.
		Ready,             ///< Windows: a location provider has data.
		NoData,            ///< Windows: no location provider has data (yet).
		Disabled,          ///< Windows: the location service is turned off.
		Denied,            ///< Windows: the program is not allowed to use the location.
		SetupFailed,       ///< The helper's script or powershell.exe was not found.
		HelperNotStarted,  ///< The helper process could not be started.
		HelperFailed,      ///< The helper process ended abnormally.
		BadData,           ///< The helper wrote a position or a status which cannot be read.
	};
	Q_ENUM(Status)
	
	/**
	 * Returns what is known about the helper process and the location service.
	 */
	Status status() const;
	
	/**
	 * Returns the status as a sentence in English, followed by what the helper
	 * process wrote to its error output, if anything. For logs and tests.
	 */
	QString statusText() const;

private:
	/**
	 * Sets the error and emits the error signal (unless NoError).
	 */
	void setError(QGeoPositionInfoSource::Error value);


public:
	QGeoPositionInfo lastKnownPosition(bool satellite_only) const override;
	
	PositioningMethods supportedPositioningMethods() const override;
	
	int minimumUpdateInterval() const override;
	

public slots:
	void startUpdates() override;
	
	void stopUpdates() override;
	
	void requestUpdate(int timeout) override;
	
private:
	bool startPowershell();
	
	void powershellStateChanged(QProcess::ProcessState new_state);
	
	void readStandardError();
	
	void readStandardOutput();
	
	void processPosition(const QByteArray& line);
	
	void processStatus(const QByteArray& line);
	
	void processPermission(const QByteArray& line);
	
	void helperReported();  // foom
	
	void nativePositionUpdate(const QGeoPositionInfo& position);
	

	void periodicUpdateTimeout();
	
	void singleUpdateTimeout();
	
	QProcess powershell;
	QByteArray start_script;
	QByteArray stop_script;
	QGeoPositionInfo last_position;
	QTimer periodic_update_timer;
	QTimer single_update_timer;
	Error position_error = NoError;
	Status source_status = Status::NotStarted;  // foom
	QByteArray helper_error_output;             // foom
	bool updates_ongoing = false;
};


}  // namespace OpenOrienteering

#endif  // OPENORIENTEERING_POWERSHELL_POSITION_SOURCE_H
