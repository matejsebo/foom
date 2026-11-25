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

#ifndef OPENORIENTEERING_MAP_IMPORT_POLICY_H
#define OPENORIENTEERING_MAP_IMPORT_POLICY_H

#include <vector>

#include <QPointF>
#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * How the symbols, colours and objects of another map file are taken into a map.
 *
 * See the design notes, "Map import policies".
 */
struct MapImportPolicy
{
	enum Symbols
	{
		NoSymbols,        ///< No symbols or colours: objects take the map's symbol of their number, others are dropped.
		AbsentSymbols,    ///< Only symbols whose number is absent; objects of the others take the map's symbol.
		RenumberClashes,  ///< Absent symbols, and a different symbol of a taken number under a new number.
		AllSymbols,       ///< Every symbol and colour; a different symbol of a taken number replaces the map's.
	};

	enum Placement
	{
		ByGeoreferencing,  ///< By the two maps' georeferencing.
		ByOffset,          ///< The coordinates moved by offset_mm.
		AtPoint,           ///< The centre of the imported objects at offset_mm.
	};

	Symbols symbols = AbsentSymbols;
	bool colours_above = false;      ///< New colours at the top of the colour table, else at the bottom.
	bool protect_symbols = false;    ///< Imported symbols are marked protected.
	Placement placement = ByGeoreferencing;
	QPointF offset_mm;               ///< The offset (ByOffset) or the point (AtPoint), map millimetres.
	bool rescale = true;             ///< Scale the imported data to the map's scale when they differ.
};


struct MapImportResult
{
	bool ok = false;
	int objects = 0;              ///< Objects added to the map.
	int objects_dropped = 0;      ///< Objects left out because their symbol was not taken.
	int symbols_added = 0;        ///< New symbols, renumbered ones included.
	int symbols_renumbered = 0;
	int symbols_replaced = 0;
	QStringList warnings;
	QString error;                ///< Set when ok is false.
};


/**
 * Imports another map into the map by the policy.
 *
 * The imported map is changed (rescaled, objects dropped). The objects are
 * added to the map's current part as one undo step; the symbols and colours
 * are not undoable, as in OOM's import.
 */
MapImportResult importMapWithPolicy(Map& map, Map& imported, const MapImportPolicy& policy);

/**
 * The same for a file whose objects were given symbols of the map before, as OOM's
 * symbol replacement ("Assign new symbols") does: it puts copies of the map's
 * symbols into the imported map. Those copies, map_symbols, are at the map's
 * scale already and are not scaled with the file's own symbols.
 */
MapImportResult importMapWithPolicy(Map& map, Map& imported, const MapImportPolicy& policy, const std::vector<const Symbol*>& map_symbols);

/** The symbols which a map holds now. */
std::vector<const Symbol*> mapSymbols(const Map& map);

/** The symbols of a map which are not among those it held before: what a symbol replacement added. */
std::vector<const Symbol*> mapSymbolsAddedSince(const Map& map, const std::vector<const Symbol*>& before);


}  // namespace OpenOrienteering

#endif
