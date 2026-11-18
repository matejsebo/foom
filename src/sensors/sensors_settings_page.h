/*
 *    Copyright 2019 Kai Pastor
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

#ifndef OPENORIENTEERING_SENSORS_SETTINGS_PAGE_H
#define OPENORIENTEERING_SENSORS_SETTINGS_PAGE_H

#include <QObject>
#include <QString>

#include "gui/widgets/settings_page.h"

class QComboBox;
class QWidget;

namespace OpenOrienteering {


class FixGateRows;
class GpsDisplayRows;
class GpsRecordRows;
class NmeaSourceRows;

class SensorsSettingsPage : public SettingsPage
{
Q_OBJECT
public:
	explicit SensorsSettingsPage(QWidget* parent = nullptr);
	
	~SensorsSettingsPage() override;
	
	QString title() const override;

	void apply() override;
	
	void reset() override;
	
protected:
	void updateWidgets();
	
private:
	QComboBox* position_source_box = nullptr;
	QComboBox* nmea_serialport_box = nullptr;
	FixGateRows* fix_gate_rows = nullptr;  // foom
	GpsRecordRows* gps_record_rows = nullptr;  // foom
	GpsDisplayRows* gps_display_rows = nullptr;  // foom
	NmeaSourceRows* nmea_source_rows = nullptr;  // foom
	
};


}  // namespace OpenOrienteering

#endif  // OPENORIENTEERING_SENSORS_SETTINGS_PAGE_H
