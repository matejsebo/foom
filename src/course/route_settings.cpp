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


#include "route_settings.h"

#include <cmath>
#include <iterator>

#include <QtGlobal>
#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>
#include <QStringRef>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "course/route_store.h"

namespace OpenOrienteering {

namespace {

struct KindName
{
	Runnability::Kind kind;
	const char* name;
};

const KindName kind_names[] = {
    { Runnability::None, "none" },
    { Runnability::Ground, "ground" },
    { Runnability::Vegetation, "vegetation" },
    { Runnability::Hindrance, "hindrance" },
    { Runnability::Surface, "surface" },
    { Runnability::Path, "path" },
    { Runnability::Impassable, "impassable" },
};

}  // namespace



QString runnabilityKindName(Runnability::Kind kind)
{
	for (auto const& entry : kind_names)
	{
		if (entry.kind == kind)
			return QLatin1String(entry.name);
	}
	return QStringLiteral("none");
}


bool runnabilityKindFromName(const QString& name, Runnability::Kind& kind)
{
	for (auto const& entry : kind_names)
	{
		if (name == QLatin1String(entry.name))
		{
			kind = entry.kind;
			return true;
		}
	}
	return false;
}



namespace {

/** The same points, to what the file keeps. */
bool samePoints(const std::vector<MapCoordF>& lhs, const std::vector<MapCoordF>& rhs)
{
	if (lhs.size() != rhs.size())
		return false;
	for (std::size_t i = 0; i < lhs.size(); ++i)
	{
		if (std::abs(lhs[i].x() - rhs[i].x()) >= 0.0005 || std::abs(lhs[i].y() - rhs[i].y()) >= 0.0005)
			return false;
	}
	return true;
}

}  // namespace


bool RouteSettings::isDefault() const
{
	return *this == RouteSettings{};
}


bool operator==(const RouteSettings& lhs, const RouteSettings& rhs)
{
	return lhs.base_pace == rhs.base_pace
	       && lhs.use_runnability == rhs.use_runnability
	       && lhs.use_elevation == rhs.use_elevation
	       && std::abs(lhs.climb_factor - rhs.climb_factor) < 0.0005
	       && lhs.symbols == rhs.symbols
	       && samePoints(lhs.perimeter, rhs.perimeter);
}



void saveRouteSettings(const RouteSettings& settings, QXmlStreamWriter& xml)
{
	xml.writeStartElement(QStringLiteral("route_settings"));
	xml.writeAttribute(QStringLiteral("base_pace"), QString::number(settings.base_pace));
	if (!settings.use_runnability)
		xml.writeAttribute(QStringLiteral("runnability"), QStringLiteral("false"));
	if (!settings.use_elevation)
		xml.writeAttribute(QStringLiteral("elevation"), QStringLiteral("false"));
	if (std::abs(settings.climb_factor - 1.0) >= 0.0005)
		xml.writeAttribute(QStringLiteral("climb"), QString::number(settings.climb_factor, 'f', 3));
	for (auto const& entry : settings.symbols)
	{
		xml.writeEmptyElement(QStringLiteral("symbol"));
		xml.writeAttribute(QStringLiteral("number"), entry.first);
		xml.writeAttribute(QStringLiteral("kind"), runnabilityKindName(entry.second.kind));
		xml.writeAttribute(QStringLiteral("percent"), QString::number(entry.second.percent));
	}
	if (!settings.perimeter.empty())
		xml.writeTextElement(QStringLiteral("perimeter"), routePointsText(settings.perimeter));
	xml.writeEndElement();
}


RouteSettings loadRouteSettings(QXmlStreamReader& xml)
{
	RouteSettings settings;
	auto const attributes = xml.attributes();
	if (attributes.hasAttribute(QLatin1String("base_pace")))
		settings.base_pace = qBound(1, attributes.value(QLatin1String("base_pace")).toInt(), 100000);
	settings.use_runnability = attributes.value(QLatin1String("runnability")) != QLatin1String("false");
	settings.use_elevation = attributes.value(QLatin1String("elevation")) != QLatin1String("false");
	if (attributes.hasAttribute(QLatin1String("climb")))
		settings.climb_factor = attributes.value(QLatin1String("climb")).toDouble();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("symbol"))
		{
			auto const inner = xml.attributes();
			Runnability runnability;
			auto const number = inner.value(QLatin1String("number")).toString();
			if (!number.isEmpty() && runnabilityKindFromName(inner.value(QLatin1String("kind")).toString(), runnability.kind))
			{
				runnability.percent = qBound(0, inner.value(QLatin1String("percent")).toInt(), 100);
				settings.symbols.push_back({ number, runnability });
			}
		}
		else if (xml.name() == QLatin1String("perimeter"))
		{
			std::vector<MapCoordF> points;
			if (routePointsFromText(xml.readElementText(), points) && points.size() >= 3)
				settings.perimeter = std::move(points);
			continue;
		}
		xml.skipCurrentElement();
	}
	return settings;
}



bool routeSettingsFromOcdRecord(const QString& record, RouteSettings& settings)
{
	RouteSettings result;
	result.symbols = settings.symbols;
	auto has_pace = false;
	auto const fields = record.split(QLatin1Char('\t'));
	for (int i = 1; i < fields.size(); ++i)
	{
		if (fields[i].isEmpty())
			continue;
		auto const value = fields[i].mid(1);
		switch (fields[i].at(0).unicode())
		{
		case 'b':
			{
				auto ok = false;
				auto const pace = value.toInt(&ok);
				if (ok && pace > 0)
				{
					result.base_pace = pace;
					has_pace = true;
				}
			}
			break;
		case 'r':
			result.use_runnability = value != QLatin1String("0");
			break;
		case 'e':
			result.use_elevation = value != QLatin1String("0");
			break;
		case 'c':
			{
				auto ok = false;
				auto const factor = value.toDouble(&ok);
				if (ok)
					result.climb_factor = factor;
			}
			break;
		default:
			break;
		}
	}
	if (!has_pace)
		return false;
	settings = result;
	return true;
}


QString routeSettingsToOcdRecord(const RouteSettings& settings)
{
	return QLatin1String("\tb") + QString::number(settings.base_pace)
	       + QLatin1String("\tr") + QLatin1String(settings.use_runnability ? "1" : "0")
	       + QLatin1String("\te") + QLatin1String(settings.use_elevation ? "1" : "0")
	       + QLatin1String("\tc") + QString::number(settings.climb_factor, 'f', 2);
}


}  // namespace OpenOrienteering
