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


#ifndef FOOM_SYNC_RECORD_H
#define FOOM_SYNC_RECORD_H

#include <vector>

#include <QtGlobal>
#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

/**
 * A field copy of the map which is still out: its sync id, the file name of
 * the base snapshot beside the working file, and when the copy was made.
 * The copy itself lists its own id, without a base.
 *
 * See the design notes, "Merge changes from file".
 */
struct SyncCopy
{
	QString id;
	QString base;        ///< The file name of the base snapshot, or empty in the copy.
	qint64 created = 0;  ///< Seconds since the epoch (UTC).
};

bool operator==(const SyncCopy& lhs, const SyncCopy& rhs);
inline bool operator!=(const SyncCopy& lhs, const SyncCopy& rhs) { return !(lhs == rhs); }

/** Writes the element <sync>. */
void saveSyncCopies(const std::vector<SyncCopy>& copies, QXmlStreamWriter& xml);

/** Reads the element <sync> the reader stands on. */
std::vector<SyncCopy> loadSyncCopies(QXmlStreamReader& xml);


}  // namespace OpenOrienteering

#endif
