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


#ifndef OPENORIENTEERING_FOREIGN_DATA_H
#define OPENORIENTEERING_FOREIGN_DATA_H

#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QString>

namespace OpenOrienteering {

/**
 * A parameter string of another file format, kept verbatim.
 *
 * See the design notes, "OCD round trip".
 */
struct ForeignString
{
	qint32 type = 0;
	quint32 object_index = 0;
	/** The raw bytes as stored in the file, including a terminating zero for OCD. */
	QByteArray data;
};


/** An object record of another file format, kept verbatim. */
struct ForeignObject
{
	/** The raw index entry. */
	QByteArray entry;
	/** The raw object record. */
	QByteArray data;
};


/**
 * Data read from another file format which the map model does not represent.
 *
 * It is written back when the map is exported to the same format and version.
 */
struct ForeignData
{
	/** The format name, for example "OCD". */
	QString format;
	/** The format version the records were read from. */
	int version = 0;
	std::vector<ForeignString> strings;
	std::vector<ForeignObject> objects;
	
	bool isEmpty() const { return strings.empty() && objects.empty(); }
};


}  // namespace OpenOrienteering

#endif
