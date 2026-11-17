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


#include "sync_record.h"

#include <QLatin1String>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>


namespace OpenOrienteering {

namespace literal
{
	static const QLatin1String sync("sync");
	static const QLatin1String copy("copy");
	static const QLatin1String id("id");
	static const QLatin1String base("base");
	static const QLatin1String created("created");
}


bool operator==(const SyncCopy& lhs, const SyncCopy& rhs)
{
	return lhs.id == rhs.id && lhs.base == rhs.base && lhs.created == rhs.created;
}

void saveSyncCopies(const std::vector<SyncCopy>& copies, QXmlStreamWriter& xml)
{
	xml.writeStartElement(literal::sync);
	for (auto const& copy : copies)
	{
		xml.writeCharacters(QString::fromLatin1("\n"));
		xml.writeEmptyElement(literal::copy);
		xml.writeAttribute(literal::id, copy.id);
		if (!copy.base.isEmpty())
			xml.writeAttribute(literal::base, copy.base);
		xml.writeAttribute(literal::created, QString::number(copy.created));
	}
	xml.writeCharacters(QString::fromLatin1("\n"));
	xml.writeEndElement();
}

std::vector<SyncCopy> loadSyncCopies(QXmlStreamReader& xml)
{
	std::vector<SyncCopy> copies;
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::copy)
		{
			auto const attributes = xml.attributes();
			SyncCopy copy;
			copy.id = attributes.value(literal::id).toString();
			copy.base = attributes.value(literal::base).toString();
			copy.created = attributes.value(literal::created).toLongLong();
			if (!copy.id.isEmpty())
				copies.push_back(copy);
		}
		xml.skipCurrentElement();
	}
	return copies;
}


}  // namespace OpenOrienteering
