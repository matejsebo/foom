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


#ifndef FOOM_LAYOUT_PART_H
#define FOOM_LAYOUT_PART_H

#include <functional>
#include <memory>
#include <vector>

class QString;

namespace OpenOrienteering {

class Map;
class MapPart;
class Object;


/**
 * The layout part and the commands on it.
 *
 * See the design notes, "Layout part commands".
 */

/** The name of a new layout part. */
QString layoutPartName();

/** Returns the index of the map's first part with the role layout, or -1. */
int layoutPartIndex(const Map& map);

/** Returns the index of the first part with the role map, the target of to-map, or -1. */
int mapContentPartIndex(const Map& map);

/**
 * Adds a visible layout part at the end of the part list, makes it current,
 * and pushes one undo step (layout-create).
 *
 * When the map has a layout part, returns its index and changes nothing.
 */
int addLayoutPart(Map& map);

/**
 * Shows or hides the layout part (layout-hide).
 *
 * When the layout part is the current part and gets hidden, the first part
 * with the role map becomes current. Returns false when there is no layout part.
 */
bool setLayoutVisible(Map& map, bool visible);

/**
 * Moves the selected objects of the current part to the part with the given
 * index, as one SwitchPartUndoStep (to-layout, to-map).
 *
 * Returns the number of objects moved.
 */
int moveSelectionToPart(Map& map, int target);

/** Returns false for layout parts, which map-wide operations leave out. */
bool isMapContent(const MapPart& part);

/** Applies the operation to the matching objects outside layout parts. */
void applyOnMapContent(const Map& map, const std::function<void (const Object*)>& operation, const std::function<bool (const Object*)>& condition);

/** As Map::determineSymbolsInUse(), counting the objects outside layout parts only. */
void determineMapContentSymbolsInUse(const Map& map, std::vector<bool>& out);

/**
 * Returns a map whose only part is a copy of the given part, with the symbols
 * its objects use, their colours, and the scale and georeferencing of the map.
 */
std::unique_ptr<Map> extractPart(const Map& map, int part_index);

/**
 * Returns a map whose only part is a copy of the layout part, with the symbols
 * its objects use, their colours, and the scale and georeferencing of the map.
 *
 * Returns nullptr when the map has no layout part.
 */
std::unique_ptr<Map> extractLayout(const Map& map);

/**
 * Writes the layout part to a map file in the native format (layout-save).
 */
bool saveLayout(const Map& map, const QString& path, QString* error);

/**
 * Adds the objects of the source's layout part to the map's layout part,
 * creating that part when the map has none, at unchanged coordinates, as one
 * undo step (layout-import).
 *
 * Returns the number of objects added, or -1 with a message in error.
 */
int importLayout(Map& map, const Map& source, QString* error);

/** Reads the map file, then calls importLayout(). */
int importLayoutFile(Map& map, const QString& path, QString* error);


}  // namespace OpenOrienteering

#endif
