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


#ifndef FOOM_NMEA_SOURCE_SETTINGS_H
#define FOOM_NMEA_SOURCE_SETTINGS_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QSettings;
class QWidget;

namespace OpenOrienteering {


/**
 * The settings of the NMEA position source beyond the serial port's name.
 *
 * See the design notes, "NMEA source over TCP and on Windows".
 */
struct NmeaSourceSettings
{
	/** The network receiver as host:port, or empty to read the serial port. */
	QString tcp;
	/** The baud rate of the serial port. */
	int baud = 4800;
	
	/** Reads the settings; keys which are absent or make no sense give the defaults. */
	static NmeaSourceSettings load(const QSettings& settings);
	/** Reads the settings of the application. */
	static NmeaSourceSettings load();
	
	void save(QSettings& settings) const;
	/** Writes the settings of the application. */
	void save() const;
	
	/** The parameters for QGeoPositionInfoSource::createSource(). */
	QVariantMap parameters() const;
};

bool operator==(const NmeaSourceSettings& lhs, const NmeaSourceSettings& rhs);
inline bool operator!=(const NmeaSourceSettings& lhs, const NmeaSourceSettings& rhs) { return !(lhs == rhs); }


/**
 * The rows of the sensors settings page which edit the NMEA source.
 *
 * The widgets carry the object names nmea-tcp and nmea-baud.
 */
class NmeaSourceRows : public QObject
{
Q_OBJECT
public:
	/** Appends the rows to the layout. */
	NmeaSourceRows(QFormLayout* layout, QWidget* parent);
	~NmeaSourceRows() override;
	
	NmeaSourceSettings settings() const;
	void setSettings(const NmeaSourceSettings& settings);
	
private:
	QLineEdit* tcp_edit;
	QComboBox* baud_box;
};


}  // namespace OpenOrienteering

#endif
