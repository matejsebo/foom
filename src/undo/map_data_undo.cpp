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


#include "map_data_undo.h"

#include <vector>

#include <QByteArray>
#include <QLatin1String>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/bookmarks.h"

#include "core/extents.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "course/course_storage.h"
#include "templates/template.h"
#include "util/xml_stream_util.h"


namespace OpenOrienteering {

namespace literal
{
	static const QLatin1String data("data");
	static const QLatin1String section("section");
}


namespace {

// foom: the bookmarks and the template placements, for the design notes,
// "Move, stretch and mirror". Numbers are written in full, so that undo is exact.

QString number(double value)
{
	return QString::number(value, 'g', 17);
}

QByteArray bookmarksToXml(const std::vector<Bookmark>& bookmarks)
{
	QByteArray data;
	QXmlStreamWriter xml(&data);
	xml.writeStartElement(QStringLiteral("bookmarks"));
	for (auto const& bookmark : bookmarks)
	{
		xml.writeStartElement(QStringLiteral("bookmark"));
		xml.writeAttribute(QStringLiteral("name"), bookmark.name);
		xml.writeAttribute(QStringLiteral("x"), QString::number(bookmark.center.nativeX()));
		xml.writeAttribute(QStringLiteral("y"), QString::number(bookmark.center.nativeY()));
		xml.writeAttribute(QStringLiteral("zoom"), number(bookmark.zoom));
		xml.writeAttribute(QStringLiteral("note"), bookmark.note);
		xml.writeEndElement();
	}
	xml.writeEndElement();
	return data;
}

std::vector<Bookmark> bookmarksFromXml(const QByteArray& data)
{
	std::vector<Bookmark> bookmarks;
	QXmlStreamReader xml(data);
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("bookmarks"))
			continue;
		if (xml.name() == QLatin1String("bookmark"))
		{
			auto const attributes = xml.attributes();
			Bookmark bookmark;
			bookmark.name = attributes.value(QLatin1String("name")).toString();
			bookmark.center = MapCoord::fromNative(attributes.value(QLatin1String("x")).toInt(), attributes.value(QLatin1String("y")).toInt());
			bookmark.zoom = attributes.value(QLatin1String("zoom")).toDouble();
			bookmark.note = attributes.value(QLatin1String("note")).toString();
			bookmarks.push_back(bookmark);
		}
		xml.skipCurrentElement();
	}
	return bookmarks;
}

void writePlacement(QXmlStreamWriter& xml, const QString& role, const TemplateTransform& t)
{
	xml.writeStartElement(QStringLiteral("placement"));
	xml.writeAttribute(QStringLiteral("role"), role);
	xml.writeAttribute(QStringLiteral("x"), QString::number(t.template_x));
	xml.writeAttribute(QStringLiteral("y"), QString::number(t.template_y));
	xml.writeAttribute(QStringLiteral("rotation"), number(t.template_rotation));
	xml.writeAttribute(QStringLiteral("scale_x"), number(t.template_scale_x));
	xml.writeAttribute(QStringLiteral("scale_y"), number(t.template_scale_y));
	xml.writeAttribute(QStringLiteral("shear"), number(t.template_shear));
	xml.writeEndElement();
}

TemplateTransform readPlacement(const QXmlStreamAttributes& attributes)
{
	TemplateTransform t;
	t.template_x = attributes.value(QLatin1String("x")).toInt();
	t.template_y = attributes.value(QLatin1String("y")).toInt();
	t.template_rotation = attributes.value(QLatin1String("rotation")).toDouble();
	t.template_scale_x = attributes.value(QLatin1String("scale_x")).toDouble();
	t.template_scale_y = attributes.value(QLatin1String("scale_y")).toDouble();
	t.template_shear = attributes.value(QLatin1String("shear")).toDouble();
	return t;
}

QByteArray templatesToXml(const Map& map)
{
	QByteArray data;
	QXmlStreamWriter xml(&data);
	xml.writeStartElement(QStringLiteral("templates"));
	auto const write = [&xml](const Template& temp, int index, bool closed) {
		xml.writeStartElement(QStringLiteral("template"));
		xml.writeAttribute(QStringLiteral("index"), QString::number(index));
		xml.writeAttribute(QStringLiteral("closed"), closed ? QStringLiteral("1") : QStringLiteral("0"));
		TemplateTransform t;
		temp.getTransform(t);
		writePlacement(xml, QStringLiteral("active"), t);
		temp.getOtherTransform(t);
		writePlacement(xml, QStringLiteral("other"), t);
		xml.writeEndElement();
	};
	for (int i = 0; i < map.getNumTemplates(); ++i)
		write(*map.getTemplate(i), i, false);
	for (int i = 0; i < map.getNumClosedTemplates(); ++i)
		write(*map.getClosedTemplate(i), i, true);
	xml.writeEndElement();
	return data;
}

void templatesFromXml(Map& map, const QByteArray& data)
{
	QXmlStreamReader xml(data);
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("templates"))
			continue;
		if (xml.name() != QLatin1String("template"))
		{
			xml.skipCurrentElement();
			continue;
		}
		auto const index = xml.attributes().value(QLatin1String("index")).toInt();
		auto const closed = xml.attributes().value(QLatin1String("closed")) == QLatin1String("1");
		auto* temp = closed ? (index < map.getNumClosedTemplates() ? map.getClosedTemplate(index) : nullptr)
		                    : (index < map.getNumTemplates() ? map.getTemplate(index) : nullptr);
		if (temp && !closed)
			map.setTemplateAreaDirty(index);
		while (xml.readNextStartElement())
		{
			if (temp && xml.name() == QLatin1String("placement"))
			{
				auto const t = readPlacement(xml.attributes());
				if (xml.attributes().value(QLatin1String("role")) == QLatin1String("other"))
					temp->setOtherTransform(t);
				else
					temp->setTransform(t);
			}
			xml.skipCurrentElement();
		}
		if (temp && !closed)
			map.setTemplateAreaDirty(index);
	}
}

}  // namespace


MapDataUndoStep::MapDataUndoStep(Map* map, Section section, const QByteArray& xml)
: UndoStep(MapDataUndoStepType, map)
, data_section(section)
, data_xml(xml)
{
	// nothing else
}

MapDataUndoStep::MapDataUndoStep(Map* map)
: UndoStep(MapDataUndoStepType, map)
, data_section(UndefinedSection)
{
	// nothing else
}

MapDataUndoStep::~MapDataUndoStep() = default;

bool MapDataUndoStep::isValid() const
{
	return data_section == Extents || data_section == Courses || data_section == Bookmarks || data_section == Templates;
}

QByteArray MapDataUndoStep::currentXml(const Map& map, Section section)
{
	switch (section)
	{
	case Extents:
		return extentsToXml(map.extents());
	case Courses:
		return courseDataToXml(map.courseData());
	case Bookmarks:
		return bookmarksToXml(map.bookmarks());
	case Templates:
		return templatesToXml(map);
	case UndefinedSection:
		break;
	}
	return {};
}

UndoStep* MapDataUndoStep::undo()
{
	auto* redo_step = new MapDataUndoStep(map, data_section, currentXml(*map, data_section));
	switch (data_section)
	{
	case Extents:
		map->setExtents(extentsFromXml(data_xml));
		break;
	case Courses:
		map->setCourseData(courseDataFromXml(data_xml));
		break;
	case Bookmarks:
		map->setBookmarks(bookmarksFromXml(data_xml));
		break;
	case Templates:
		templatesFromXml(*map, data_xml);
		break;
	case UndefinedSection:
		break;
	}
	return redo_step;
}

void MapDataUndoStep::saveImpl(QXmlStreamWriter& xml) const
{
	UndoStep::saveImpl(xml);
	XmlElementWriter data_element(xml, literal::data);
	data_element.writeAttribute(literal::section, int(data_section));
	xml.writeCharacters(QString::fromLatin1(data_xml.toBase64()));
}

void MapDataUndoStep::loadImpl(QXmlStreamReader& xml, SymbolDictionary& /*symbol_dict*/)
{
	// UndoStep::load() calls this for each child element of the step.
	if (xml.name() == literal::data)
	{
		data_section = Section(xml.attributes().value(literal::section).toInt());
		data_xml = QByteArray::fromBase64(xml.readElementText().toLatin1());
	}
	else
	{
		xml.skipCurrentElement();
	}
}


}  // namespace OpenOrienteering
