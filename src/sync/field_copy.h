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


#ifndef FOOM_SYNC_FIELD_COPY_H
#define FOOM_SYNC_FIELD_COPY_H

#include <memory>

#include <QString>

#include "core/sync_record.h"

namespace OpenOrienteering {

class Map;


/**
 * The files of a field copy: the copy, and the base snapshot beside the
 * working file which the three-way comparison needs.
 *
 * See the design notes, "Merge changes from file".
 */

/** The path of the base snapshot: <map name>.base-<id>.xmap beside the working file. */
QString baseFilePath(const QString& map_path, const QString& id);

/**
 * Makes a field copy of the working map.
 *
 * Adds the sync record to the map, writes the copy with its own record, and
 * writes the base as a byte copy of the copy's file. The caller saves the
 * working map, so that its file has the record too.
 *
 * Returns the sync id, or an empty string and a message.
 */
QString createFieldCopy(Map& map, const QString& map_path, const QString& copy_path, QString* error);

/**
 * Closes a field copy: removes the record from the map and deletes the base.
 * With an empty id, closes the only open copy.
 */
bool closeFieldCopy(Map& map, const QString& map_path, const QString& id, QString* error);

/** A returned copy and its base, read for a merge. */
struct MergeInput
{
	SyncCopy copy;                ///< The record of the working map.
	std::unique_ptr<Map> theirs;
	std::unique_ptr<Map> base;    ///< Null when the base file is missing or unreadable.
};

/**
 * Reads the returned copy and its base.
 *
 * Refuses when the copy's sync id is not among the working map's open
 * copies, or when scale or georeferencing differ.
 */
bool loadForMerge(const Map& mine, const QString& map_path, const QString& copy_path, MergeInput& input, QString* error);

/**
 * Makes the returned copy the new base, by copying its file. To be called
 * only after the merged working map is saved.
 */
bool refreshBase(const QString& map_path, const QString& copy_path, const SyncCopy& copy, QString* error);


}  // namespace OpenOrienteering

#endif
