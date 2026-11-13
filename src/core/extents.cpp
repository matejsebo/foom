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


#include "extents.h"

#include <vector>

#include <QtGlobal>
#include <QLatin1String>
#include <QPageSize>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "util/xml_stream_util.h"


namespace OpenOrienteering {

namespace literal
{
	static const QLatin1String extents("extents");
	static const QLatin1String extent("extent");
	static const QLatin1String count("count");
	static const QLatin1String name("name");
	static const QLatin1String x("x");
	static const QLatin1String y("y");
	static const QLatin1String width("width");
	static const QLatin1String height("height");
	static const QLatin1String scale("scale");
	static const QLatin1String paper("paper");
	static const QLatin1String margin("margin");
	static const QLatin1String back_of("back_of");
}


bool operator==(const Extent& lhs, const Extent& rhs)
{
	return lhs.name == rhs.name
	       && lhs.rect_mm == rhs.rect_mm
	       && lhs.scale == rhs.scale
	       && lhs.paper == rhs.paper
	       && qFuzzyCompare(1.0 + lhs.margin_mm, 1.0 + rhs.margin_mm)
	       && lhs.back_of == rhs.back_of;
}

void saveExtents(const std::vector<Extent>& extents, QXmlStreamWriter& xml)
{
	XmlElementWriter extents_element(xml, literal::extents);
	extents_element.writeAttribute(literal::count, extents.size());
	for (auto const& extent : extents)
	{
		xml.writeCharacters(QString::fromLatin1("\n"));
		XmlElementWriter extent_element(xml, literal::extent);
		extent_element.writeAttribute(literal::name, extent.name);
		extent_element.writeAttribute(literal::x, extent.rect_mm.x());
		extent_element.writeAttribute(literal::y, extent.rect_mm.y());
		extent_element.writeAttribute(literal::width, extent.rect_mm.width());
		extent_element.writeAttribute(literal::height, extent.rect_mm.height());
		extent_element.writeAttribute(literal::scale, extent.scale);
		extent_element.writeAttribute(literal::paper, extent.paper);
		extent_element.writeAttribute(literal::margin, extent.margin_mm);
		extent_element.writeAttribute(literal::back_of, extent.back_of);
	}
	xml.writeCharacters(QString::fromLatin1("\n"));
}

std::vector<Extent> loadExtents(QXmlStreamReader& xml)
{
	std::vector<Extent> extents;
	XmlElementReader extents_element(xml);
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::extent)
		{
			XmlElementReader extent_element(xml);
			Extent extent;
			extent.name = extent_element.attribute<QString>(literal::name);
			extent.rect_mm = QRectF(extent_element.attribute<double>(literal::x), extent_element.attribute<double>(literal::y), extent_element.attribute<double>(literal::width), extent_element.attribute<double>(literal::height));
			extent.scale = extent_element.attribute<unsigned int>(literal::scale);
			extent.paper = extent_element.attribute<QString>(literal::paper);
			extent.margin_mm = extent_element.attribute<double>(literal::margin);
			extent.back_of = extent_element.attribute<QString>(literal::back_of);
			extents.push_back(extent);
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	return extents;
}

QByteArray extentsToXml(const std::vector<Extent>& extents)
{
	QByteArray data;
	QXmlStreamWriter xml(&data);
	saveExtents(extents, xml);
	return data;
}

std::vector<Extent> extentsFromXml(const QByteArray& data)
{
	QXmlStreamReader xml(data);
	if (xml.readNextStartElement() && xml.name() == literal::extents)
		return loadExtents(xml);
	return {};
}

QSizeF extentSizeFromPaper(QPageSize::PageSizeId paper, bool landscape, unsigned int print_scale, unsigned int map_scale)
{
	auto size = QPageSize::size(paper, QPageSize::Millimeter);
	if (landscape)
		size.transpose();
	if (print_scale > 0 && map_scale > 0)
		size *= double(print_scale) / double(map_scale);
	return size;
}

QPageSize::PageSizeId pageSizeFromKey(const QString& key)
{
	for (int i = 0; i <= int(QPageSize::LastPageSize); ++i)
	{
		auto const id = QPageSize::PageSizeId(i);
		if (id != QPageSize::Custom && QPageSize::key(id) == key)
			return id;
	}
	return QPageSize::Custom;
}


}  // namespace OpenOrienteering
