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


#include "map_data_undo.h"

#include <QByteArray>
#include <QLatin1String>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/extents.h"
#include "core/map.h"
#include "util/xml_stream_util.h"


namespace OpenOrienteering {

namespace literal
{
	static const QLatin1String data("data");
	static const QLatin1String section("section");
}


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
	return data_section == Extents;
}

QByteArray MapDataUndoStep::currentXml(const Map& map, Section section)
{
	switch (section)
	{
	case Extents:
		return extentsToXml(map.extents());
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
