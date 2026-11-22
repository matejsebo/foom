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


#include "sync_record.h"

#include <QLatin1String>
#include <QStringList>
#include <QVector>
#include <QStringRef>
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
	static const QLatin1String extent("extent");
	static const QLatin1String review("review");
	static const QLatin1String true_value("true");
}


bool operator==(const SyncCopy& lhs, const SyncCopy& rhs)
{
	return lhs.id == rhs.id && lhs.base == rhs.base && lhs.created == rhs.created
	       && lhs.extent == rhs.extent && lhs.review == rhs.review;
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
		if (!copy.extent.isNull())
		{
			auto const rect = copy.extent;
			QString value;
			for (auto number : { rect.x(), rect.y(), rect.width(), rect.height() })
			{
				if (!value.isEmpty())
					value += QLatin1Char(' ');
				value += QString::number(number, 'g', 10);
			}
			xml.writeAttribute(literal::extent, value);
		}
		if (copy.review)
			xml.writeAttribute(literal::review, literal::true_value);
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
			auto const numbers = attributes.value(literal::extent).split(QLatin1Char(' '), Qt::SkipEmptyParts);
			if (numbers.size() == 4)
				copy.extent = QRectF(numbers[0].toDouble(), numbers[1].toDouble(), numbers[2].toDouble(), numbers[3].toDouble());
			copy.review = attributes.value(literal::review) == literal::true_value;
			if (!copy.id.isEmpty())
				copies.push_back(copy);
		}
		xml.skipCurrentElement();
	}
	return copies;
}


}  // namespace OpenOrienteering
