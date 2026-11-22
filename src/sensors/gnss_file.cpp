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


#include "gnss_file.h"

#include <cmath>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QList>

#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "undo/object_undo.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::GnssFile", text);
}

double numberOf(const QByteArray& field, bool* ok = nullptr)
{
	auto const trimmed = field.trimmed();
	auto good = !trimmed.isEmpty();
	auto value = good ? trimmed.toDouble(&good) : 0.0;
	if (ok)
		*ok = good;
	return good ? value : qQNaN();
}

QTime timeOf(const QByteArray& field)
{
	auto const text = field.trimmed();
	if (text.size() < 6)
		return {};
	auto const seconds = numberOf(text.mid(4));
	auto const ms = std::isnan(seconds) ? 0 : int(std::round(std::fmod(seconds, 1.0) * 1000));
	return QTime(text.left(2).toInt(), text.mid(2, 2).toInt(), std::isnan(seconds) ? 0 : int(seconds), ms);
}

QDate dateOf(const QByteArray& field)
{
	auto const text = field.trimmed();
	if (text.size() != 6)
		return {};
	return QDate(2000 + text.mid(4, 2).toInt(), text.mid(2, 2).toInt(), text.left(2).toInt());
}

/// ddmm.mmmm (or dddmm.mmmm) and a hemisphere letter to degrees.
bool angleOf(const QByteArray& value, const QByteArray& hemisphere, double* degrees)
{
	bool ok = false;
	auto const number = numberOf(value, &ok);
	if (!ok)
		return false;
	auto const whole = std::floor(number / 100.0);
	auto result = whole + (number - whole * 100.0) / 60.0;
	auto const h = hemisphere.trimmed();
	if (h == "S" || h == "W")
		result = -result;
	else if (h != "N" && h != "E")
		return false;
	*degrees = result;
	return true;
}

/// One time of the file: what its sentences say.
struct Epoch
{
	GnssFix fix;
	bool has_position = false;
	bool invalid = false;
	bool has_date = false;
};

}  // namespace



QString GnssFix::isoTime() const
{
	if (!utc.isValid())
		return {};
	if (date.isValid())
		return QDateTime(date, utc, Qt::UTC).toString(Qt::ISODate);
	return utc.toString(Qt::ISODate) + QLatin1Char('Z');
}


GnssFileData readNmea(const QByteArray& bytes)
{
	GnssFileData data;
	std::vector<Epoch> epochs;
	auto const epochAt = [&epochs](const QTime& utc) -> Epoch& {
		if (epochs.empty() || !utc.isValid() || epochs.back().fix.utc != utc)
		{
			epochs.emplace_back();
			epochs.back().fix.utc = utc;
		}
		return epochs.back();
	};

	for (auto const& raw : bytes.split('\n'))
	{
		auto line = raw.trimmed();
		auto const dollar = line.indexOf('$');
		if (dollar < 0)
			continue;
		line = line.mid(dollar);
		auto const star = line.lastIndexOf('*');
		auto body = line.mid(1);
		if (star > 0)
		{
			auto ok = false;
			auto const given = line.mid(star + 1).trimmed().toInt(&ok, 16);
			auto computed = 0;
			for (auto i = 1; i < star; ++i)
				computed ^= static_cast<unsigned char>(line.at(i));
			if (!ok || given != computed)
			{
				++data.bad_checksums;
				continue;
			}
			body = line.mid(1, star - 1);
		}
		auto const fields = body.split(',');
		auto const id = fields.value(0);
		if (id.size() != 5)
			continue;
		++data.sentences;
		auto const field = [&fields](int index) { return fields.value(index); };

		if (id.endsWith("GGA"))
		{
			auto& epoch = epochAt(timeOf(field(1)));
			auto ok = false;
			auto const quality = field(6).trimmed().toInt(&ok);
			epoch.fix.quality = ok ? quality : -1;
			if (!ok || quality == 0)
				epoch.invalid = true;
			double latitude, longitude;
			if (angleOf(field(2), field(3), &latitude) && angleOf(field(4), field(5), &longitude))
			{
				epoch.fix.latitude = latitude;
				epoch.fix.longitude = longitude;
				epoch.has_position = true;
			}
			auto const satellites = field(7).trimmed().toInt(&ok);
			if (ok && satellites >= 0)
				epoch.fix.satellites = satellites;
			epoch.fix.hdop = numberOf(field(8));
			epoch.fix.altitude = numberOf(field(9));
		}
		else if (id.endsWith("RMC"))
		{
			auto& epoch = epochAt(timeOf(field(1)));
			if (field(2).trimmed() != "A")
				epoch.invalid = true;
			double latitude, longitude;
			if (!epoch.has_position && angleOf(field(3), field(4), &latitude) && angleOf(field(5), field(6), &longitude))
			{
				epoch.fix.latitude = latitude;
				epoch.fix.longitude = longitude;
				epoch.has_position = true;
			}
			epoch.fix.date = dateOf(field(9));
			epoch.has_date = epoch.fix.date.isValid();
		}
		else if (id.endsWith("WPL"))
		{
			GnssWaypoint waypoint;
			if (angleOf(field(1), field(2), &waypoint.latitude) && angleOf(field(3), field(4), &waypoint.longitude))
			{
				waypoint.name = QString::fromLatin1(field(5).trimmed());
				data.waypoints.push_back(waypoint);
			}
		}
	}

	// The day: that of the epoch's RMC, else the last one before, else the first one after.
	QDate day;
	for (auto const& epoch : epochs)
	{
		if (epoch.has_date)
		{
			day = epoch.fix.date;
			break;
		}
	}
	for (auto& epoch : epochs)
	{
		if (epoch.has_date)
			day = epoch.fix.date;
		else
			epoch.fix.date = day;
		if (epoch.has_position && !epoch.invalid)
			data.fixes.push_back(epoch.fix);
		else
			++data.skipped;
	}

	if (data.sentences == 0)
		data.error = tr("The file holds no NMEA sentence.");
	return data;
}


GnssFileData readGnssFile(const QString& path)
{
	GnssFileData data;
	if (path.endsWith(QLatin1String(".frwd"), Qt::CaseInsensitive))
	{
		data.error = tr("The FRWD format is not known to FOOM; convert the file to GPX or NMEA.");
		return data;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		data.error = tr("Cannot read %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
		return data;
	}
	return readNmea(file.readAll());
}



GnssObjects::GnssObjects() = default;
GnssObjects::GnssObjects(GnssObjects&&) = default;
GnssObjects& GnssObjects::operator=(GnssObjects&&) = default;
GnssObjects::~GnssObjects() = default;


GnssObjects gnssObjects(const Map& map, const GnssFileData& data, GnssImportKind kind, const Symbol* symbol)
{
	GnssObjects result;
	auto const& georef = map.getGeoreferencing();
	if (georef.getState() != Georeferencing::Geospatial)
	{
		result.error = tr("The map is not georeferenced.");
		return result;
	}
	auto const wants_line = kind == GnssImportKind::Track;
	auto const fits = symbol && (wants_line ? (symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Combined)
	                                        : symbol->getType() == Symbol::Point);
	if (!fits)
	{
		result.error = wants_line ? tr("A track needs a line symbol.") : tr("Waypoints and positions need a point symbol.");
		return result;
	}
	auto const toMap = [&georef](double latitude, double longitude, bool* ok) {
		return georef.toMapCoordF(LatLon(latitude, longitude), ok);
	};

	switch (kind)
	{
	case GnssImportKind::Track:
	{
		if (data.fixes.size() < 2)
			break;
		auto* path = new PathObject(symbol);
		for (auto const& fix : data.fixes)
		{
			bool ok = true;
			auto const coord = toMap(fix.latitude, fix.longitude, &ok);
			if (ok)
				path->addCoordinate(MapCoord(coord));
		}
		result.objects.emplace_back(path);
		break;
	}
	case GnssImportKind::Waypoints:
		for (auto const& waypoint : data.waypoints)
		{
			bool ok = true;
			auto const coord = toMap(waypoint.latitude, waypoint.longitude, &ok);
			if (!ok)
				continue;
			auto* point = new PointObject(symbol);
			point->setPosition(coord);
			if (!waypoint.name.isEmpty())
				point->setTag(QStringLiteral("name"), waypoint.name);
			result.objects.emplace_back(point);
		}
		break;
	case GnssImportKind::Positions:
		for (auto const& fix : data.fixes)
		{
			bool ok = true;
			auto const coord = toMap(fix.latitude, fix.longitude, &ok);
			if (!ok)
				continue;
			auto* point = new PointObject(symbol);
			point->setPosition(coord);
			auto const time = fix.isoTime();
			if (!time.isEmpty())
				point->setTag(QStringLiteral("time"), time);
			if (!std::isnan(fix.altitude))
				point->setTag(QStringLiteral("ele"), QString::number(fix.altitude));
			if (fix.quality >= 0)
				point->setTag(QStringLiteral("fix"), QString::number(fix.quality));
			if (fix.satellites >= 0)
				point->setTag(QStringLiteral("satellites"), QString::number(fix.satellites));
			if (!std::isnan(fix.hdop))
				point->setTag(QStringLiteral("hdop"), QString::number(fix.hdop));
			result.objects.emplace_back(point);
		}
		break;
	}
	return result;
}


int addGnssObjects(Map& map, std::vector<std::unique_ptr<Object>>&& objects)
{
	if (objects.empty())
		return 0;
	auto* part = map.getCurrentPart();
	map.clearObjectSelection(false);
	auto delete_step = std::make_unique<DeleteObjectsUndoStep>(&map);
	std::vector<Object*> added;
	added.reserve(objects.size());
	for (auto& object : objects)
	{
		auto* output = object.release();
		map.addObject(output);
		map.addObjectToSelection(output, false);
		added.push_back(output);
	}
	for (auto* output : added)
		delete_step->addObject(part->findObjectIndex(output));
	map.push(delete_step.release());
	map.setObjectsDirty();
	map.emitSelectionChanged();
	map.emitSelectionEdited();
	return int(added.size());
}


}  // namespace OpenOrienteering
