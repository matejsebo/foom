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


#include "route_store.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringRef>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace OpenOrienteering {

namespace {

/** How far an end of a route may be from the end of its leg, in mm. */
const double end_tolerance = 0.2;

bool closeTo(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y()) <= end_tolerance;
}

QString pointsText(const std::vector<MapCoordF>& points)
{
	QString text;
	text.reserve(int(points.size()) * 16);
	for (auto const& point : points)
	{
		if (!text.isEmpty())
			text += QLatin1Char(';');
		text += QString::number(point.x(), 'f', 3);
		text += QLatin1Char(' ');
		text += QString::number(point.y(), 'f', 3);
	}
	return text;
}

bool pointsFromText(const QString& text, std::vector<MapCoordF>& points)
{
	auto const pairs = text.splitRef(QLatin1Char(';'), QString::SkipEmptyParts);
	points.reserve(std::size_t(pairs.size()));
	for (auto const& pair : pairs)
	{
		auto const blank = pair.indexOf(QLatin1Char(' '));
		auto x_ok = false;
		auto y_ok = false;
		auto const x = pair.left(blank).toDouble(&x_ok);
		auto const y = pair.mid(blank + 1).toDouble(&y_ok);
		if (blank < 0 || !x_ok || !y_ok)
			return false;
		points.push_back(MapCoordF(x, y));
	}
	return true;
}

}  // namespace



bool StoredRoute::fits(const MapCoordF& begin, const MapCoordF& end) const
{
	return points.size() >= 2 && closeTo(points.front(), begin) && closeTo(points.back(), end);
}


bool operator==(const StoredRoute& lhs, const StoredRoute& rhs)
{
	if (lhs.from != rhs.from || lhs.to != rhs.to || lhs.index != rhs.index || lhs.found != rhs.found
	    || lhs.points.size() != rhs.points.size() || lhs.via.size() != rhs.via.size())
		return false;
	// What the file keeps: tenths of a metre and of a second, thousandths of a millimetre.
	if (std::abs(lhs.length_m - rhs.length_m) >= 0.05 || std::abs(lhs.time_s - rhs.time_s) >= 0.05 || std::abs(lhs.climb_m - rhs.climb_m) >= 0.05)
		return false;
	for (std::size_t i = 0; i < lhs.points.size(); ++i)
	{
		if (std::abs(lhs.points[i].x() - rhs.points[i].x()) >= 0.0005 || std::abs(lhs.points[i].y() - rhs.points[i].y()) >= 0.0005)
			return false;
	}
	for (std::size_t i = 0; i < lhs.via.size(); ++i)
	{
		if (std::abs(lhs.via[i].x() - rhs.via[i].x()) >= 0.0005 || std::abs(lhs.via[i].y() - rhs.via[i].y()) >= 0.0005)
			return false;
	}
	return true;
}



const StoredRoute* StoredRoutes::find(const QString& from, const QString& to, int index) const
{
	auto const found = std::find_if(routes.begin(), routes.end(), [&from, &to, index](const StoredRoute& route) {
		return route.index == index && route.from == from && route.to == to;
	});
	return found == routes.end() ? nullptr : &*found;
}


std::vector<const StoredRoute*> StoredRoutes::viaRoutes(const QString& from, const QString& to) const
{
	std::vector<const StoredRoute*> found;
	for (auto const& route : routes)
	{
		if (route.index > 0 && route.from == from && route.to == to)
			found.push_back(&route);
	}
	std::sort(found.begin(), found.end(), [](const StoredRoute* a, const StoredRoute* b) { return a->index < b->index; });
	return found;
}


int StoredRoutes::nextIndex(const QString& from, const QString& to) const
{
	auto next = 1;
	for (auto const& route : routes)
	{
		if (route.from == from && route.to == to)
			next = std::max(next, route.index + 1);
	}
	return next;
}


bool StoredRoutes::remove(const QString& from, const QString& to, int index)
{
	auto const found = std::find_if(routes.begin(), routes.end(), [&from, &to, index](const StoredRoute& route) {
		return route.index == index && route.from == from && route.to == to;
	});
	if (found == routes.end())
		return false;
	routes.erase(found);
	return true;
}


void StoredRoutes::put(StoredRoute route)
{
	auto const found = std::find_if(routes.begin(), routes.end(), [&route](const StoredRoute& other) {
		return other.index == route.index && other.from == route.from && other.to == route.to;
	});
	if (found == routes.end())
		routes.push_back(std::move(route));
	else
		*found = std::move(route);
}


void StoredRoutes::take(const StoredRoutes& others)
{
	if (others.isEmpty())
		return;
	if (with_heights != others.with_heights)
	{
		routes.clear();
		with_heights = others.with_heights;
	}
	for (auto const& route : others.routes)
		put(route);
}


int StoredRoutes::takeViaRoutes(const StoredRoutes& others)
{
	if (others.with_heights != with_heights)
		return 0;
	auto taken = 0;
	for (auto const& route : others.routes)
	{
		if (route.index <= 0 || route.points.size() < 2)
			continue;
		auto const* fastest = find(route.from, route.to);
		if (!fastest || !fastest->found || !fastest->fits(route.points.front(), route.points.back()))
			continue;
		put(route);
		++taken;
	}
	return taken;
}


bool operator==(const StoredRoutes& lhs, const StoredRoutes& rhs)
{
	return lhs.with_heights == rhs.with_heights && lhs.routes == rhs.routes;
}



void saveStoredRoutes(const StoredRoutes& routes, QXmlStreamWriter& xml)
{
	xml.writeStartElement(QStringLiteral("routes"));
	if (routes.with_heights)
		xml.writeAttribute(QStringLiteral("heights"), QStringLiteral("true"));
	for (auto const& route : routes.routes)
	{
		xml.writeStartElement(QStringLiteral("route"));
		xml.writeAttribute(QStringLiteral("from"), route.from);
		xml.writeAttribute(QStringLiteral("to"), route.to);
		if (route.index > 0)
			xml.writeAttribute(QStringLiteral("index"), QString::number(route.index));
		if (!route.found)
			xml.writeAttribute(QStringLiteral("found"), QStringLiteral("false"));
		xml.writeAttribute(QStringLiteral("length"), QString::number(route.length_m, 'f', 1));
		xml.writeAttribute(QStringLiteral("time"), QString::number(route.time_s, 'f', 1));
		if (route.climb_m >= 0.05)
			xml.writeAttribute(QStringLiteral("climb"), QString::number(route.climb_m, 'f', 1));
		if (!route.via.empty())
			xml.writeAttribute(QStringLiteral("via"), pointsText(route.via));
		xml.writeCharacters(pointsText(route.points));
		xml.writeEndElement();
	}
	xml.writeEndElement();
}


StoredRoutes loadStoredRoutes(QXmlStreamReader& xml)
{
	StoredRoutes routes;
	routes.with_heights = xml.attributes().value(QLatin1String("heights")) == QLatin1String("true");
	while (xml.readNextStartElement())
	{
		if (xml.name() != QLatin1String("route"))
		{
			xml.skipCurrentElement();
			continue;
		}
		auto const attributes = xml.attributes();
		StoredRoute route;
		route.from = attributes.value(QLatin1String("from")).toString();
		route.to = attributes.value(QLatin1String("to")).toString();
		route.found = attributes.value(QLatin1String("found")) != QLatin1String("false");
		route.length_m = attributes.value(QLatin1String("length")).toDouble();
		route.time_s = attributes.value(QLatin1String("time")).toDouble();
		route.climb_m = attributes.value(QLatin1String("climb")).toDouble();
		route.index = qMax(0, attributes.value(QLatin1String("index")).toInt());
		auto ok = pointsFromText(attributes.value(QLatin1String("via")).toString(), route.via);
		ok = pointsFromText(xml.readElementText(), route.points) && ok;
		// A route which cannot be read is as if it had not been searched.
		if (ok && !route.from.isEmpty() && !route.to.isEmpty() && route.points.size() >= 2)
			routes.put(std::move(route));
	}
	return routes;
}


}  // namespace OpenOrienteering
