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


#include "gps_path_recorder.h"

#include <cmath>

#include <QChar>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSettings>
#include <QVariant>
#include <QWidget>


namespace OpenOrienteering {

namespace {

QString minDistanceKey() { return QStringLiteral("Sensors/record_min_distance"); }

}  // namespace



GpsPathRecorder::GpsPathRecorder(double min_distance, double scale_denominator)
: min_distance_mm(scale_denominator > 0 ? min_distance * 1000.0 / scale_denominator : 0.0)
{}

bool GpsPathRecorder::accept(const MapCoordF& position)
{
	if (paused)
		return false;
	if (taken > 0 && min_distance_mm > 0)
	{
		auto const distance = std::hypot(position.x() - last.x(), position.y() - last.y());
		if (distance <= min_distance_mm)
			return false;
	}
	take(position);
	return true;
}

void GpsPathRecorder::take(const MapCoordF& position)
{
	last = position;
	++taken;
}



// static
GpsRecordSettings GpsRecordSettings::load(const QSettings& settings)
{
	GpsRecordSettings result;
	auto ok = false;
	auto const distance = settings.value(minDistanceKey()).toDouble(&ok);
	if (settings.contains(minDistanceKey()) && ok && distance >= 0)
		result.min_distance = distance;
	return result;
}

// static
GpsRecordSettings GpsRecordSettings::load()
{
	QSettings settings;
	return load(settings);
}

void GpsRecordSettings::save(QSettings& settings) const
{
	settings.setValue(minDistanceKey(), min_distance);
}

void GpsRecordSettings::save() const
{
	QSettings settings;
	save(settings);
}



GpsRecordRows::GpsRecordRows(QFormLayout* layout, QWidget* parent)
: QObject(parent)
{
	distance_box = new QDoubleSpinBox();
	distance_box->setObjectName(QStringLiteral("record-min-distance"));
	distance_box->setDecimals(1);
	distance_box->setRange(0, 100);
	distance_box->setSingleStep(0.5);
	QString suffix = QString(QChar::Space);
	suffix += tr("m", "meters");
	distance_box->setSuffix(suffix);
	distance_box->setSpecialValueText(tr("Every position"));
	layout->addRow(tr("Recording into a path, minimum distance:"), distance_box);
	
	setSettings({});
}

GpsRecordRows::~GpsRecordRows() = default;


GpsRecordSettings GpsRecordRows::settings() const
{
	GpsRecordSettings result;
	result.min_distance = distance_box->value();
	return result;
}

void GpsRecordRows::setSettings(const GpsRecordSettings& settings)
{
	distance_box->setValue(settings.min_distance);
}


}  // namespace OpenOrienteering
