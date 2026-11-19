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


#include "gps_tail.h"

#include <cmath>

#include <QtGlobal>
#include <QChar>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSettings>
#include <QVariant>
#include <QWidget>


namespace OpenOrienteering {

namespace {

QString antennaHeightKey() { return QStringLiteral("Sensors/antenna_height"); }
QString tailLengthKey() { return QStringLiteral("Sensors/tail_length"); }

double distance(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// Reads a length which must not be negative.
void readLength(const QSettings& settings, const QString& key, double& value)
{
	if (!settings.contains(key))
		return;
	auto ok = false;
	auto const read = settings.value(key).toDouble(&ok);
	if (ok && read >= 0)
		value = read;
}

}  // namespace



void GpsTail::setMaxLength(double length)
{
	max_length = qMax(0.0, length);
	trim();
}

void GpsTail::add(const MapCoordF& position)
{
	if (max_length <= 0)
		return;
	if (!tail.empty() && tail.back() == position)
		return;
	tail.push_back(position);
	trim();
}

void GpsTail::clear()
{
	tail.clear();
}

double GpsTail::length() const
{
	auto result = 0.0;
	for (std::size_t i = 1; i < tail.size(); ++i)
		result += distance(tail[i - 1], tail[i]);
	return result;
}

double GpsTail::opacityAt(std::size_t index) const
{
	if (index >= tail.size() || max_length <= 0)
		return 0.0;
	auto behind = 0.0;
	for (auto i = tail.size() - 1; i > index; --i)
		behind += distance(tail[i - 1], tail[i]);
	return qBound(0.0, 1.0 - behind / max_length, 1.0);
}

void GpsTail::trim()
{
	if (max_length <= 0)
	{
		tail.clear();
		return;
	}
	// From the newest point backwards, to the place where the length is used up.
	auto walked = 0.0;
	for (auto i = tail.size(); i > 1; --i)
	{
		auto const& newer = tail[i - 1];
		auto& older = tail[i - 2];
		auto const piece = distance(newer, older);
		auto const rest = max_length - walked;
		if (rest <= 0)
		{
			tail.erase(tail.begin(), tail.begin() + (i - 1));
			return;
		}
		if (piece > rest)
		{
			auto const t = rest / piece;
			older = MapCoordF(newer.x() + (older.x() - newer.x()) * t, newer.y() + (older.y() - newer.y()) * t);
			tail.erase(tail.begin(), tail.begin() + (i - 2));
			return;
		}
		walked += piece;
	}
}



// static
GpsDisplaySettings GpsDisplaySettings::load(const QSettings& settings)
{
	GpsDisplaySettings result;
	readLength(settings, antennaHeightKey(), result.antenna_height);
	readLength(settings, tailLengthKey(), result.tail_length);
	return result;
}

// static
GpsDisplaySettings GpsDisplaySettings::load()
{
	QSettings settings;
	return load(settings);
}

void GpsDisplaySettings::save(QSettings& settings) const
{
	settings.setValue(antennaHeightKey(), antenna_height);
	settings.setValue(tailLengthKey(), tail_length);
}

void GpsDisplaySettings::save() const
{
	QSettings settings;
	save(settings);
}

bool operator==(const GpsDisplaySettings& lhs, const GpsDisplaySettings& rhs)
{
	return lhs.antenna_height == rhs.antenna_height
	       && lhs.tail_length == rhs.tail_length;
}



GpsDisplayRows::GpsDisplayRows(QFormLayout* layout, QWidget* parent)
: QObject(parent)
{
	QString suffix = QString(QChar::Space);
	suffix += tr("m", "meters");
	
	antenna_box = new QDoubleSpinBox();
	antenna_box->setObjectName(QStringLiteral("antenna-height"));
	antenna_box->setDecimals(2);
	antenna_box->setRange(0, 10);
	antenna_box->setSingleStep(0.05);
	antenna_box->setSuffix(suffix);
	layout->addRow(tr("Antenna height above the ground:"), antenna_box);
	
	tail_box = new QDoubleSpinBox();
	tail_box->setObjectName(QStringLiteral("tail-length"));
	tail_box->setDecimals(0);
	tail_box->setRange(0, 1000);
	tail_box->setSingleStep(10);
	tail_box->setSuffix(suffix);
	tail_box->setSpecialValueText(tr("None"));
	layout->addRow(tr("Tail behind the GPS position:"), tail_box);
	
	setSettings({});
}

GpsDisplayRows::~GpsDisplayRows() = default;


GpsDisplaySettings GpsDisplayRows::settings() const
{
	GpsDisplaySettings result;
	result.antenna_height = antenna_box->value();
	result.tail_length = tail_box->value();
	return result;
}

void GpsDisplayRows::setSettings(const GpsDisplaySettings& settings)
{
	antenna_box->setValue(settings.antenna_height);
	tail_box->setValue(settings.tail_length);
}


}  // namespace OpenOrienteering
