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


#ifndef FOOM_SYNC_MERGE_DECISIONS_H
#define FOOM_SYNC_MERGE_DECISIONS_H

#include <QString>

namespace OpenOrienteering {

struct MergePlan;


/**
 * The decisions of a paused merge, kept in a file beside the working map.
 *
 * See the design notes, "Merge changes from file: detail", second change.
 */

/** The file of the decisions: <map name>.merge-<id>.json beside the working file. */
QString decisionsFilePath(const QString& map_path, const QString& id);

/**
 * Writes the action of every row of the plan, and whether it is decided,
 * and whose definition each symbol and colour which differs keeps.
 */
bool saveDecisions(const QString& path, const MergePlan& plan, QString* error);

/**
 * Reads decisions into the plan. A stored decision is taken for a row which
 * is still there with the same status and the same sides; a choice of
 * definition is taken for a symbol or colour which still differs.
 * Returns the number of rows which took one; 0 when there is no file.
 */
int loadDecisions(const QString& path, MergePlan& plan);


}  // namespace OpenOrienteering

#endif
