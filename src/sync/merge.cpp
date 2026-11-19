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


#include "merge.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <utility>

#include <QtGlobal>
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QIODevice>
#include <QSet>
#include <QStringList>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/image_object.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Merge", text);
}

/** What stands for the symbol in the hash: its number, or the definition of a private symbol. */
QByteArray symbolKey(const Object& object)
{
	auto const* symbol = object.getSymbol();
	if (!symbol)
		return QByteArrayLiteral("none");
	if (!symbol->isPrivate())
		return symbol->getNumberAsString().toUtf8();
	
	QByteArray definition;
	if (auto const* map = object.getMap())
	{
		QBuffer buffer(&definition);
		buffer.open(QIODevice::WriteOnly);
		QXmlStreamWriter xml(&buffer);
		symbol->save(xml, *map);
	}
	QByteArray key = QByteArrayLiteral("private:");
	key += QCryptographicHash::hash(definition, QCryptographicHash::Sha1).toHex();
	return key;
}

void appendField(QByteArray& bytes, const QByteArray& field)
{
	bytes += QByteArray::number(field.size());
	bytes += ':';
	bytes += field;
	bytes += '\n';
}

/** An object and the part it is in. */
struct Located
{
	const Object* object = nullptr;
	int part = -1;
};

using Index = QHash<QUuid, Located>;

Index indexOf(const Map& map, std::vector<QUuid>* order = nullptr)
{
	Index index;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (index.contains(object->uuid()))
				continue;
			index.insert(object->uuid(), { object, p });
			if (order)
				order->push_back(object->uuid());
		}
	}
	return index;
}

/** The hash of an object together with the role and name of its part. */
QByteArray stateOf(const Map& map, const Located& located)
{
	auto const* part = map.getPart(std::size_t(located.part));
	QByteArray state = contentHash(*located.object);
	state += '|';
	state += QByteArray::number(int(part->role()));
	state += '|';
	state += part->getName().toUtf8();
	return state;
}

const Symbol* ownSymbol(const Map& map, const Symbol* theirs)
{
	auto const number = theirs->getNumberAsString();
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == theirs->getType() && symbol->getNumberAsString() == number)
			return symbol;
	}
	return nullptr;
}

MergeRow::Side sideOf(bool in_base, bool present, const QByteArray& base_state, const QByteArray& state)
{
	if (!in_base)
		return present ? MergeRow::Added : MergeRow::Absent;
	if (!present)
		return MergeRow::Deleted;
	return state == base_state ? MergeRow::Unchanged : MergeRow::Changed;
}

bool touched(MergeRow::Side side)
{
	return side == MergeRow::Changed || side == MergeRow::Added || side == MergeRow::Deleted;
}

}  // namespace


QByteArray contentHash(const Object& object)
{
	QByteArray bytes;
	appendField(bytes, symbolKey(object));
	appendField(bytes, QByteArray::number(int(object.getType())));
	
	QByteArray coords;
	for (auto const& coord : object.getRawCoordinateVector())
	{
		coords += QByteArray::number(coord.nativeX());
		coords += ',';
		coords += QByteArray::number(coord.nativeY());
		coords += ',';
		coords += QByteArray::number(int(coord.flags()));
		coords += ';';
	}
	appendField(bytes, coords);
	appendField(bytes, QByteArray::number(qRound64(object.getRotation() * 10000)));
	
	switch (object.getType())
	{
	case Object::Path:
		{
			auto const origin = object.asPath()->getPatternOrigin();
			QByteArray field = QByteArray::number(origin.nativeX());
			field += ',';
			field += QByteArray::number(origin.nativeY());
			appendField(bytes, field);
		}
		break;
	case Object::Text:
		{
			auto const* text = object.asText();
			appendField(bytes, text->getText().toUtf8());
			appendField(bytes, QByteArray::number(int(text->getHorizontalAlignment())));
			appendField(bytes, QByteArray::number(int(text->getVerticalAlignment())));
		}
		break;
	case Object::Image:
		{
			auto const& image = static_cast<const ImageObject&>(object);
			if (image.isLinked())
				appendField(bytes, image.href().toUtf8());
			else
				appendField(bytes, QCryptographicHash::hash(image.data(), QCryptographicHash::Sha1));
			appendField(bytes, QByteArray::number(image.opacity(), 'g', 6));
			appendField(bytes, QByteArray::number(int(image.layer())));
		}
		break;
	default:
		break;
	}
	
	QStringList tags;
	for (auto const& tag : object.tags())
	{
		QString line = tag.key;
		line += QLatin1Char('=');
		line += tag.value;
		tags.append(line);
	}
	tags.sort();
	for (auto const& tag : qAsConst(tags))
		appendField(bytes, tag.toUtf8());
	
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
}


int MergePlan::count(MergeRow::Status status) const
{
	return int(std::count_if(begin(rows), end(rows), [status](const MergeRow& row) { return row.status == status; }));
}

int MergePlan::unresolved() const
{
	return int(std::count_if(begin(rows), end(rows), [](const MergeRow& row) { return !row.resolved; }));
}

const MergeRow* MergePlan::row(const QUuid& uuid) const
{
	auto const found = std::find_if(begin(rows), end(rows), [&uuid](const MergeRow& row) { return row.uuid == uuid; });
	return found == end(rows) ? nullptr : &*found;
}

void MergePlan::resolveConflicts(MergeRow::Action action)
{
	for (auto& row : rows)
	{
		if (row.status == MergeRow::Conflict)
		{
			row.action = action;
			row.resolved = true;
		}
	}
}

void MergePlan::acceptPrefilled()
{
	for (auto& row : rows)
		row.resolved = true;
}


MergePlan compareMaps(const Map* base, const Map& mine, const Map& theirs)
{
	MergePlan plan;
	plan.two_way = !base;
	
	// My objects in the order of my map, then those which only they have.
	std::vector<QUuid> order;
	auto const mine_index = indexOf(mine, &order);
	std::vector<QUuid> their_order;
	auto const their_index = indexOf(theirs, &their_order);
	for (auto const& uuid : their_order)
	{
		if (!mine_index.contains(uuid))
			order.push_back(uuid);
	}
	auto const base_index = base ? indexOf(*base) : Index{};
	
	QSet<const Symbol*> their_symbols;
	for (auto const& uuid : order)
	{
		auto const a = mine_index.value(uuid);
		auto const b = their_index.value(uuid);
		auto const state_a = a.object ? stateOf(mine, a) : QByteArray{};
		auto const state_b = b.object ? stateOf(theirs, b) : QByteArray{};
		
		MergeRow row;
		row.uuid = uuid;
		if (!base)
		{
			if (a.object && b.object && state_a == state_b)
				continue;
			row.mine = !a.object ? MergeRow::Absent : b.object ? MergeRow::Changed : MergeRow::Added;
			row.theirs = !b.object ? MergeRow::Absent : a.object ? MergeRow::Changed : MergeRow::Added;
			row.status = MergeRow::Conflict;
			row.resolved = false;
			if (!a.object || (b.object && b.object->modifiedTime() > a.object->modifiedTime()))
				row.action = MergeRow::KeepTheirs;
		}
		else
		{
			auto const o = base_index.value(uuid);
			auto const state_o = o.object ? stateOf(*base, o) : QByteArray{};
			row.mine = sideOf(o.object, a.object, state_o, state_a);
			row.theirs = sideOf(o.object, b.object, state_o, state_b);
			if (!touched(row.mine) && !touched(row.theirs))
				continue;
			if (touched(row.mine) && touched(row.theirs))
			{
				// Both deleted it, or both made the same of it.
				if (bool(a.object) == bool(b.object) && state_a == state_b)
					continue;
				row.status = MergeRow::Conflict;
				row.resolved = false;
			}
			else if (touched(row.theirs))
			{
				row.status = MergeRow::Incoming;
				row.action = MergeRow::KeepTheirs;
			}
		}
		plan.rows.push_back(row);
		if (b.object && b.object->getSymbol())
			their_symbols.insert(b.object->getSymbol());
	}
	
	// Their symbols, in the order of their symbol list: those which the rows
	// need and I lack, and those which both have with another definition.
	for (int i = 0; i < theirs.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = theirs.getSymbol(i);
		auto const* own = ownSymbol(mine, symbol);
		if (own ? own->equals(symbol) : !their_symbols.contains(symbol))
			continue;
		MergeSymbolRow row;
		row.number = symbol->getNumberAsString();
		row.name = symbol->getPlainTextName();
		row.status = own ? MergeSymbolRow::Differs : MergeSymbolRow::New;
		plan.symbols.push_back(row);
	}
	return plan;
}


bool applyMerge(Map& mine, const Map& theirs, const MergePlan& plan, MergeResult* result, QString* error)
{
	MergeResult counts;
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	
	auto const mine_index = indexOf(mine);
	auto const their_index = indexOf(theirs);
	
	// What is to be done, by the part of my map it happens in.
	struct Replacement
	{
		Object* own;
		const Object* other;
	};
	struct Addition
	{
		const Object* other;
		int their_part;
		bool new_uuid;
	};
	std::map<int, std::vector<Object*>> deletions;
	std::map<int, std::vector<Replacement>> replacements;
	std::vector<Addition> additions;
	
	auto const samePart = [&mine, &theirs](const Located& a, const Located& b) {
		auto const* own = mine.getPart(std::size_t(a.part));
		auto const* other = theirs.getPart(std::size_t(b.part));
		return own->role() == other->role() && own->getName() == other->getName();
	};
	for (auto const& row : plan.rows)
	{
		auto const a = mine_index.value(row.uuid);
		auto const b = their_index.value(row.uuid);
		auto* own = const_cast<Object*>(a.object);  // The map owns its objects and may edit them.
		switch (row.action)
		{
		case MergeRow::KeepMine:
			break;
		case MergeRow::KeepBoth:
			if (b.object)
				additions.push_back({ b.object, b.part, bool(a.object) });
			break;
		case MergeRow::KeepTheirs:
			if (own && !b.object)
			{
				deletions[a.part].push_back(own);
			}
			else if (own && own->getType() == b.object->getType() && samePart(a, b))
			{
				replacements[a.part].push_back({ own, b.object });
			}
			else if (b.object)
			{
				if (own)
					deletions[a.part].push_back(own);
				additions.push_back({ b.object, b.part, false });
			}
			break;
		}
	}
	if (deletions.empty() && replacements.empty() && additions.empty())
	{
		if (result)
			*result = counts;
		return true;
	}
	
	// Symbols: mine by number, else theirs, imported with its colours.
	QHash<const Symbol*, const Symbol*> symbols;
	std::vector<bool> filter(std::size_t(theirs.getNumSymbols()), false);
	std::vector<const Symbol*> to_import;
	auto const need = [&](const Object* object) {
		auto const* symbol = object->getSymbol();
		if (!symbol || symbols.contains(symbol))
			return;
		auto const position = theirs.findSymbolIndex(symbol);
		const Symbol* own = position < 0 ? symbol  // one of the undefined symbols, which all maps share
		                    : symbol->isPrivate() ? nullptr
		                    : ownSymbol(mine, symbol);
		if (!own)
		{
			filter[std::size_t(position)] = true;
			to_import.push_back(symbol);
		}
		symbols.insert(symbol, own);
	};
	for (auto const& entry : replacements)
	{
		for (auto const& replacement : entry.second)
			need(replacement.other);
	}
	for (auto const& addition : additions)
		need(addition.other);
	if (!to_import.empty())
	{
		auto const imported = mine.importMap(theirs, Map::MinimalSymbolImport, &filter);
		for (auto const* symbol : to_import)
		{
			auto const* own = imported.value(symbol);
			if (!own)
				return fail(tr("The symbol %1 could not be imported.").arg(symbol->getNumberAsString()));
			symbols.insert(symbol, own);
		}
		counts.symbols = int(to_import.size());
	}
	
	auto undo = std::make_unique<CombinedUndoStep>(&mine);
	// Pushing the undo step stamps the objects with the time of the merge.
	// What came from them keeps their time, which is set again afterwards.
	std::vector<std::pair<Object*, qint64>> their_times;
	auto const current = mine.getCurrentPartIndex();
	mine.clearObjectSelection(false);
	
	// The parts which additions go to. A part which I lack is added.
	std::map<int, int> target_parts;
	for (auto const& addition : additions)
	{
		if (target_parts.count(addition.their_part))
			continue;
		auto const* other = theirs.getPart(std::size_t(addition.their_part));
		auto target = -1;
		for (int p = 0; p < mine.getNumParts() && target < 0; ++p)
		{
			auto const* own = mine.getPart(std::size_t(p));
			if (own->role() == other->role() && own->getName() == other->getName())
				target = p;
		}
		if (target < 0)
		{
			auto* part = new MapPart(other->getName(), &mine);
			part->setRole(other->role());
			mine.addPart(part, std::size_t(mine.getNumParts()));
			undo->push(new MapPartUndoStep(&mine, MapPartUndoStep::RemoveMapPart, part));
			target = mine.findPartIndex(part);
			++counts.parts;
		}
		target_parts[addition.their_part] = target;
	}
	
	// Deletions and replacements. The undo steps refer to the current part.
	for (int p = 0; p < mine.getNumParts(); ++p)
	{
		auto* part = mine.getPart(std::size_t(p));
		mine.setCurrentPartIndex(std::size_t(p));
		auto const to_delete = deletions.find(p);
		if (to_delete != deletions.end())
		{
			auto* step = new AddObjectsUndoStep(&mine);
			for (auto* object : to_delete->second)
				step->addObject(part->findObjectIndex(object), object);
			step->removeContainedObjects(false);
			undo->push(step);
			counts.deleted += int(to_delete->second.size());
		}
		auto const to_replace = replacements.find(p);
		if (to_replace != replacements.end())
		{
			auto* step = new ReplaceObjectsUndoStep(&mine);
			for (auto const& replacement : to_replace->second)
			{
				auto* own = replacement.own;
				step->addObject(own, own->duplicate());
				auto const group = own->group();
				// copyFrom() takes the other object's extent, so the place
				// which the object leaves has to be marked for redrawing here.
				mine.setObjectAreaDirty(own->getExtent());
				own->copyFrom(*replacement.other);
				own->setSymbol(symbols.value(replacement.other->getSymbol()), true);
				own->setGroup(group);
				own->setIdentity(replacement.other->uuid(), replacement.other->createdTime(), replacement.other->modifiedTime());
				own->setDerivedFrom(replacement.other->derivedFrom());
				own->update();
				their_times.emplace_back(own, replacement.other->modifiedTime());
			}
			undo->push(step);
			counts.replaced += int(to_replace->second.size());
		}
	}
	
	// Additions, part by part.
	for (int p = 0; p < mine.getNumParts(); ++p)
	{
		DeleteObjectsUndoStep* step = nullptr;
		for (auto const& addition : additions)
		{
			if (target_parts[addition.their_part] != p)
				continue;
			if (!step)
			{
				mine.setCurrentPartIndex(std::size_t(p));
				step = new DeleteObjectsUndoStep(&mine);
			}
			auto* copy = addition.other->duplicate();
			copy->setSymbol(symbols.value(addition.other->getSymbol()), true);
			copy->setGroup(0);
			if (addition.new_uuid)
			{
				copy->setIdentity(QUuid::createUuid(), addition.other->createdTime(), addition.other->modifiedTime());
				copy->setDerivedFrom(addition.other->uuid());
			}
			step->addObject(mine.addObject(copy, p));
			their_times.emplace_back(copy, addition.other->modifiedTime());
			++counts.added;
		}
		if (step)
			undo->push(step);
	}
	
	mine.setCurrentPartIndex(current);
	mine.push(undo.release());
	for (auto const& entry : their_times)
		entry.first->touch(entry.second);
	mine.setObjectsDirty();
	mine.emitSelectionChanged();
	if (result)
		*result = counts;
	return true;
}


}  // namespace OpenOrienteering
