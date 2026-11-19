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


#include "extents.h"

#include <algorithm>
#include <QCoreApplication>

#include "core/map.h"
#include "core/map_printer.h"
#include "undo/map_data_undo.h"

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
		extent_element.writeAttribute(literal::x, extent.rect_mm.x(), 3);
		extent_element.writeAttribute(literal::y, extent.rect_mm.y(), 3);
		extent_element.writeAttribute(literal::width, extent.rect_mm.width(), 3);
		extent_element.writeAttribute(literal::height, extent.rect_mm.height(), 3);
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


bool addDefaultExtent(Map& map)
{
	// the design notes, "Extents in print and export".
	if (!map.hasPrinterConfig() || !map.extents().empty())
		return false;
	auto const config = static_cast<const Map&>(map).printerConfig();
	if (config.print_area.width() <= 0 || config.print_area.height() <= 0)
		return false;
	
	Extent extent;
	extent.name = QStringLiteral("default");
	// To the thousandth of a millimetre, as extents and the print area are saved.
	auto const rounded = [](qreal value) { return qRound64(value * 1000) / 1000.0; };
	extent.rect_mm = QRectF(rounded(config.print_area.x()), rounded(config.print_area.y()), rounded(config.print_area.width()), rounded(config.print_area.height()));
	if (config.options.scale > 0 && config.options.scale != map.getScaleDenominator())
		extent.scale = config.options.scale;
#ifdef QT_PRINTSUPPORT_LIB
	if (config.page_format.page_size != QPageSize::Custom)
		extent.paper = QPageSize::key(config.page_format.page_size);
#endif
	map.setExtents({ extent });
	return true;
}

int importExtents(Map& map, const std::vector<Extent>& extents)
{
	if (extents.empty())
		return 0;
	auto* step = new MapDataUndoStep(&map, MapDataUndoStep::Extents, MapDataUndoStep::currentXml(map, MapDataUndoStep::Extents));
	auto merged = map.extents();
	for (auto const& extent : extents)
	{
		auto const existing = std::find_if(begin(merged), end(merged), [&extent](const Extent& other) { return other.name == extent.name; });
		if (existing != end(merged))
			*existing = extent;
		else
			merged.push_back(extent);
	}
	map.setExtents(std::move(merged));
	map.push(step);
	map.setOtherDirty();
	return int(extents.size());
}

int importExtentsFromFile(Map& map, const QString& path, QString* error)
{
	Map source;
	if (!source.loadFrom(path))
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::Extents", "Cannot read the file %1.").arg(path);
		return -1;
	}
	return importExtents(map, source.extents());
}


}  // namespace OpenOrienteering
