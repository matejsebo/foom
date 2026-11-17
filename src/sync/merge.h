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


#ifndef FOOM_SYNC_MERGE_H
#define FOOM_SYNC_MERGE_H

#include <vector>

#include <QByteArray>
#include <QString>
#include <QUuid>

namespace OpenOrienteering {

class Map;
class Object;


/**
 * Merge changes from file: comparing a working map with a returned copy,
 * with or without their common base, and carrying out the result.
 *
 * See the design notes, "Merge changes from file".
 */

/**
 * A hash over what an object is: symbol, type, coordinates with flags,
 * rotation, text, tags. The uuid and the timestamps are left out.
 */
QByteArray contentHash(const Object& object);


/** One object which differs between my map and theirs. */
struct MergeRow
{
	/** What a side did to the object since the base. */
	enum Side { Absent, Unchanged, Changed, Added, Deleted };
	
	enum Status
	{
		Incoming,  ///< Only they touched it.
		Outgoing,  ///< Only I touched it.
		Conflict   ///< Both did, differently. Without a base: any difference.
	};
	
	enum Action { KeepMine, KeepTheirs, KeepBoth };
	
	QUuid uuid;
	Side mine = Absent;
	Side theirs = Absent;
	Status status = Outgoing;
	Action action = KeepMine;
	bool resolved = true;  ///< False for a conflict nobody has decided.
};

/** A symbol of theirs which I lack, or whose definition differs from mine. */
struct MergeSymbolRow
{
	enum Status { New, Differs };
	
	QString number;
	QString name;
	Status status = New;
};

struct MergePlan
{
	bool two_way = false;  ///< True when there was no base.
	std::vector<MergeRow> rows;
	std::vector<MergeSymbolRow> symbols;
	
	int count(MergeRow::Status status) const;
	int unresolved() const;
	const MergeRow* row(const QUuid& uuid) const;
	
	/** Gives every conflict the action and marks it resolved. */
	void resolveConflicts(MergeRow::Action action);
	
	/** Marks every conflict resolved with the action it has. */
	void acceptPrefilled();
};

/**
 * Compares my map with theirs. With the base, changes on one side only are
 * settled; without it, every difference is a conflict whose action is the
 * side with the later modification time.
 */
MergePlan compareMaps(const Map* base, const Map& mine, const Map& theirs);


struct MergeResult
{
	int added = 0;
	int replaced = 0;
	int deleted = 0;
	int symbols = 0;  ///< Symbols imported.
	int parts = 0;    ///< Parts added.
	
	int changes() const { return added + replaced + deleted; }
};

/**
 * Carries out the actions of the plan's rows in my map, as one undo step.
 * A plan which changes nothing adds no undo step.
 */
bool applyMerge(Map& mine, const Map& theirs, const MergePlan& plan, MergeResult* result, QString* error);


}  // namespace OpenOrienteering

#endif
