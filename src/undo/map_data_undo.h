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


#ifndef OPENORIENTEERING_MAP_DATA_UNDO_H
#define OPENORIENTEERING_MAP_DATA_UNDO_H

#include <QByteArray>

#include "undo.h"

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

class Map;


/**
 * An undo step for map data which is not an object: a section of the map,
 * serialised as before the change.
 *
 * See the design notes, "Undo for map-level data".
 */
class MapDataUndoStep : public UndoStep
{
public:
	enum Section
	{
		UndefinedSection = 0,
		Extents = 1
	};
	
	/** Creates a step which restores the section to the given serialisation. */
	MapDataUndoStep(Map* map, Section section, const QByteArray& xml);
	
	/** Creates an empty step, for loading. */
	explicit MapDataUndoStep(Map* map);
	
	~MapDataUndoStep() override;
	
	bool isValid() const override;
	
	/** Swaps the stored serialisation with the section's current state. */
	UndoStep* undo() override;
	
	Section section() const { return data_section; }
	const QByteArray& xml() const { return data_xml; }
	
	/** Returns the current serialisation of the section. */
	static QByteArray currentXml(const Map& map, Section section);

protected:
	void saveImpl(QXmlStreamWriter& xml) const override;
	void loadImpl(QXmlStreamReader& xml, SymbolDictionary& symbol_dict) override;

private:
	Section data_section;
	QByteArray data_xml;
};


}  // namespace OpenOrienteering

#endif
