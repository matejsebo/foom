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


#include "guides.h"

#include <cmath>
#include <limits>

#include <QLatin1String>
#include <QString>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>


namespace OpenOrienteering {

namespace {

const QLatin1String guides_element("guides");
const QLatin1String guide_element("guide");
const QLatin1String orientation_attribute("orientation");
const QLatin1String position_attribute("position");
const QLatin1String visible_attribute("visible");
const QLatin1String locked_attribute("locked");
const QLatin1String horizontal_value("horizontal");
const QLatin1String vertical_value("vertical");

/** The distance in mm from the position to the guide. */
double distanceTo(const Guide& guide, const MapCoordF& position)
{
	auto const coordinate = guide.orientation == Qt::Horizontal ? position.y() : position.x();
	return std::abs(coordinate - guide.positionMM());
}

}  // namespace


bool operator==(const Guides& lhs, const Guides& rhs)
{
	return lhs.list == rhs.list && lhs.visible == rhs.visible && lhs.locked == rhs.locked;
}


void saveGuides(const Guides& guides, QXmlStreamWriter& xml)
{
	xml.writeStartElement(guides_element);
	if (!guides.visible)
		xml.writeAttribute(visible_attribute, QStringLiteral("false"));
	if (guides.locked)
		xml.writeAttribute(locked_attribute, QStringLiteral("true"));
	for (auto const& guide : guides.list)
	{
		xml.writeEmptyElement(guide_element);
		xml.writeAttribute(orientation_attribute, guide.orientation == Qt::Horizontal ? horizontal_value : vertical_value);
		xml.writeAttribute(position_attribute, QString::number(guide.position));
	}
	xml.writeEndElement();
}

Guides loadGuides(QXmlStreamReader& xml)
{
	Guides guides;
	auto const attributes = xml.attributes();
	guides.visible = attributes.value(visible_attribute) != QLatin1String("false");
	guides.locked = attributes.value(locked_attribute) == QLatin1String("true");
	while (xml.readNextStartElement())
	{
		if (xml.name() == guide_element)
		{
			Guide guide;
			guide.orientation = xml.attributes().value(orientation_attribute) == vertical_value ? Qt::Vertical : Qt::Horizontal;
			guide.position = xml.attributes().value(position_attribute).toInt();
			guides.list.push_back(guide);
		}
		xml.skipCurrentElement();
	}
	return guides;
}

QByteArray guidesToXml(const Guides& guides)
{
	QByteArray data;
	QXmlStreamWriter xml(&data);
	saveGuides(guides, xml);
	return data;
}

Guides guidesFromXml(const QByteArray& data)
{
	QXmlStreamReader xml(data);
	if (xml.readNextStartElement() && xml.name() == guides_element)
		return loadGuides(xml);
	return {};
}


bool snapToGuides(const std::vector<Guide>& guides, const MapCoordF& position, double max_distance, MapCoordF& out)
{
	const Guide* nearest[2] = { nullptr, nullptr };  // horizontal, vertical
	double distance[2] = { std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
	for (auto const& guide : guides)
	{
		auto const index = guide.orientation == Qt::Horizontal ? 0 : 1;
		auto const d = distanceTo(guide, position);
		if (d <= max_distance && d < distance[index])
		{
			distance[index] = d;
			nearest[index] = &guide;
		}
	}
	if (!nearest[0] && !nearest[1])
		return false;
	
	auto result = position;
	if (nearest[0])
		result.setY(nearest[0]->positionMM());
	if (nearest[1])
		result.setX(nearest[1]->positionMM());
	out = result;
	return true;
}

int guideAt(const std::vector<Guide>& guides, const MapCoordF& position, double max_distance)
{
	auto result = -1;
	auto best = max_distance;
	for (std::size_t i = 0; i < guides.size(); ++i)
	{
		auto const d = distanceTo(guides[i], position);
		if (d <= best)
		{
			best = d;
			result = int(i);
		}
	}
	return result;
}

Guide guideThrough(Qt::Orientation orientation, const MapCoordF& position)
{
	Guide guide;
	guide.orientation = orientation;
	auto const coord = MapCoord(position);
	guide.position = orientation == Qt::Horizontal ? coord.nativeY() : coord.nativeX();
	return guide;
}


}  // namespace OpenOrienteering
