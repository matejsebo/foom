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
#include <QCryptographicHash>
#include <QHash>
#include <QIODevice>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/image_object.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "core/sync_record.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"


namespace OpenOrienteering {

namespace {

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

bool sameCmykAndSpot(const MapColor& a, const MapColor& b)
{
	return a.getCmyk() == b.getCmyk()
	       && a.getSpotColorMethod() == b.getSpotColorMethod()
	       && (a.getSpotColorMethod() != MapColor::SpotColor
	           || a.getSpotColorName().compare(b.getSpotColorName(), Qt::CaseInsensitive) == 0);
}

bool hasColorNamed(const Map& map, const QString& name)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName().compare(name, Qt::CaseInsensitive) == 0)
			return true;
	}
	return false;
}

/**
 * The index of my colour which stands for theirs: the one of the same name,
 * else one with the same CMYK and spot colour whose name they do not have
 * (a colour which one side renamed). -1 when there is none.
 */
int matchingColor(const Map& mine, const Map& theirs, const MapColor& color)
{
	for (int i = 0; i < mine.getNumColors(); ++i)
	{
		if (mine.getColor(i)->getName().compare(color.getName(), Qt::CaseInsensitive) == 0)
			return i;
	}
	for (int i = 0; i < mine.getNumColors(); ++i)
	{
		auto const* own = mine.getColor(i);
		if (sameCmykAndSpot(*own, color) && !hasColorNamed(theirs, own->getName()))
			return i;
	}
	return -1;
}

/** Their colours to my matching ones; a colour which I lack to itself. */
MapColorMap matchedColors(const Map& mine, const Map& theirs)
{
	MapColorMap colors;
	for (int i = 0; i < theirs.getNumColors(); ++i)
	{
		auto const* color = theirs.getColor(i);
		auto const own = matchingColor(mine, theirs, *color);
		colors[color] = own < 0 ? color : mine.getColor(own);
	}
	return colors;
}

/**
 * True when their symbol has my symbol's definition, apart from the
 * definitions of the colours: those are compared on their own.
 */
bool sameDefinition(const Symbol& own, const Symbol& other, const MapColorMap& colors)
{
	auto const copy = duplicate(other);
	copy->replaceColors(colors);
	return own.equals(copy.get());
}

/** Their symbol with the number which has a counterpart of another definition in my map. */
const Symbol* differingSymbol(const Map& mine, const Map& theirs, const QString& number)
{
	auto const colors = matchedColors(mine, theirs);
	for (int i = 0; i < theirs.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = theirs.getSymbol(i);
		if (symbol->getNumberAsString() != number)
			continue;
		auto const* own = ownSymbol(mine, symbol);
		if (own && !sameDefinition(*own, *symbol, colors))
			return symbol;
	}
	return nullptr;
}


/**
 * Brings symbols and colours of theirs into my map. Colours are matched to
 * mine by matchingColor(); a colour which I lack is appended below mine.
 */
class Adopter
{
public:
	Adopter(Map& mine, const Map& theirs) : mine(mine), theirs(theirs) {}
	
	/** My colour for theirs, added when I lack it. */
	const MapColor* color(const MapColor* other)
	{
		if (!other || other->getPriority() < 0)
			return other;  // one of the reserved colours, which all maps share
		if (colors.contains(other))
			return colors.value(other);
		auto const match = matchingColor(mine, theirs, *other);
		auto* own = match < 0 ? nullptr : mine.getMapColor(match);
		if (!own)
		{
			own = other->duplicate();
			if (other->getSpotColorMethod() == MapColor::CustomColor)
				own->setSpotColorComposition(components(*other));
			mine.addColor(own, mine.getNumColors());
			++added_colors;
		}
		colors[other] = own;
		return own;
	}
	
	/** Their spot colour composition in terms of my colours. */
	SpotColorComponents components(const MapColor& other)
	{
		auto result = other.getComponents();
		for (auto& component : result)
			component.spot_color = color(component.spot_color);
		return result;
	}
	
	/** Gives my colour which matches theirs their definition, in its place. */
	void takeColor(const MapColor& other)
	{
		auto const match = matchingColor(mine, theirs, other);
		if (match < 0)
			return;
		auto* own = mine.getMapColor(match);
		auto const composition = other.getSpotColorMethod() == MapColor::CustomColor ? components(other) : SpotColorComponents{};
		*own = other;
		own->setPriority(match);
		if (own->getSpotColorMethod() == MapColor::CustomColor)
			own->setSpotColorComposition(composition);
		colors[&other] = own;
		mine.setColor(own, match);
		mine.setColorsDirty();
		mine.updateAllObjects();
	}
	
	/**
	 * A new symbol of mine with their definition, not yet in my map. Their
	 * shared symbols which it contains are mine by number, or are imported.
	 */
	Symbol* definition(const Symbol& other)
	{
		auto* copy = duplicate(other).release();
		MapColorMap color_map;
		for (int i = 0; i < theirs.getNumColors(); ++i)
		{
			auto const* their_color = theirs.getColor(i);
			if (other.containsColor(their_color))
				color_map[their_color] = color(their_color);
		}
		copy->replaceColors(color_map);
		for (int i = 0; i < theirs.getNumPublicSymbols(); ++i)
		{
			auto const* part = theirs.getSymbol(i);
			if (part != &other && copy->containsSymbol(part))
				copy->symbolChangedEvent(part, symbol(part));
		}
		return copy;
	}
	
	/** My symbol for theirs: mine by number and type, else theirs imported. */
	const Symbol* symbol(const Symbol* other)
	{
		if (symbols.contains(other))
			return symbols.value(other);
		if (theirs.findSymbolIndex(other) < 0)
			return other;  // one of the undefined symbols, which all maps share
		const Symbol* own = other->isPrivate() ? nullptr : ownSymbol(mine, other);
		if (!own)
		{
			auto* imported = definition(*other);
			mine.addSymbol(imported, mine.getNumSymbols());
			++imported_symbols;
			own = imported;
		}
		symbols.insert(other, own);
		return own;
	}
	
	/** Gives my symbol of the same number their definition, in its place. */
	const Symbol* take(const Symbol& other)
	{
		auto const* own = ownSymbol(mine, &other);
		auto* taken = definition(other);
		mine.setSymbol(taken, mine.findSymbolIndex(own));
		symbols.insert(&other, taken);
		return taken;
	}
	
	int added_colors = 0;
	int imported_symbols = 0;
	
private:
	Map& mine;
	const Map& theirs;
	QHash<const MapColor*, MapColor*> colors;
	QHash<const Symbol*, const Symbol*> symbols;
};


/** The record of the field copy which both maps list, from their side; null when there is none. */
const SyncCopy* copyRecord(const Map& mine, const Map& theirs)
{
	for (auto const& record : theirs.syncCopies())
	{
		for (auto const& own : mine.syncCopies())
		{
			if (own.id == record.id)
				return &record;
		}
	}
	return nullptr;
}

bool inSketch(const Map& map, const Located& located)
{
	return located.object && map.getPart(std::size_t(located.part))->role() == MapPart::Role::Sketch;
}

bool outside(const Object* object, const QRectF& extent)
{
	if (!object || extent.isNull())
		return false;
	object->update();
	return !object->getExtent().intersects(extent);
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

void MergePlan::resolveDefinitions(MergeSymbolRow::Action action)
{
	for (auto& row : symbols)
	{
		if (row.status == MergeSymbolRow::Differs)
			row.action = action;
	}
	for (auto& row : colors)
	{
		if (row.status == MergeColorRow::Differs)
			row.action = action;
	}
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
	
	// A copy of a part of the map knows nothing of what lies outside, and
	// from a review copy only the sketch part comes back.
	auto const* record = copyRecord(mine, theirs);
	auto const extent = record ? record->extent : QRectF{};
	auto const review = record && record->review;
	
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
			if (review && !inSketch(theirs, b))
				continue;
			if (!b.object && outside(a.object, extent))
				continue;
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
			// What they did outside the sketch part of a review copy does not count.
			auto their_object = bool(b.object);
			auto their_state = state_b;
			if (review && !inSketch(theirs, b) && !(!b.object && inSketch(*base, o)))
			{
				their_object = bool(o.object);
				their_state = state_o;
			}
			if (!o.object && !their_object && outside(a.object, extent))
				continue;
			row.mine = sideOf(o.object, a.object, state_o, state_a);
			row.theirs = sideOf(o.object, their_object, state_o, their_state);
			if (!touched(row.mine) && !touched(row.theirs))
				continue;
			if (touched(row.mine) && touched(row.theirs))
			{
				// Both deleted it, or both made the same of it.
				if (bool(a.object) == their_object && state_a == their_state)
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
	auto const colors = matchedColors(mine, theirs);
	std::vector<const Symbol*> listed;
	for (int i = 0; i < theirs.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = theirs.getSymbol(i);
		auto const* own = ownSymbol(mine, symbol);
		if (own ? sameDefinition(*own, *symbol, colors) : !their_symbols.contains(symbol))
			continue;
		listed.push_back(symbol);
		MergeSymbolRow row;
		row.number = symbol->getNumberAsString();
		row.name = symbol->getPlainTextName();
		row.status = own ? MergeSymbolRow::Differs : MergeSymbolRow::New;
		plan.symbols.push_back(row);
	}
	
	// Their colours, in the order of their colour table: those which differ
	// from mine, and those which I lack and a listed symbol uses.
	for (int i = 0; i < theirs.getNumColors(); ++i)
	{
		auto const* color = theirs.getColor(i);
		auto const match = matchingColor(mine, theirs, *color);
		MergeColorRow row;
		row.name = color->getName();
		if (match >= 0)
		{
			auto const* own = mine.getColor(match);
			if (own->equals(*color))
				continue;
			row.own_name = own->getName();
			row.status = MergeColorRow::Differs;
		}
		else if (std::none_of(begin(listed), end(listed), [color](const Symbol* symbol) { return symbol->containsColor(color); }))
		{
			continue;
		}
		plan.colors.push_back(row);
	}
	return plan;
}


bool applyMerge(Map& mine, const Map& theirs, const MergePlan& plan, MergeResult* result, QString* error)
{
	Q_UNUSED(error)  // Nothing below can fail at present.
	MergeResult counts;
	
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
	auto const takes = [](const MergeSymbolRow& row) {
		return row.status == MergeSymbolRow::Differs && row.action == MergeSymbolRow::KeepTheirs;
	};
	auto const takes_color = [](const MergeColorRow& row) {
		return row.status == MergeColorRow::Differs && row.action == MergeSymbolRow::KeepTheirs;
	};
	if (deletions.empty() && replacements.empty() && additions.empty()
	    && std::none_of(begin(plan.symbols), end(plan.symbols), takes)
	    && std::none_of(begin(plan.colors), end(plan.colors), takes_color))
	{
		if (result)
			*result = counts;
		return true;
	}
	
	// Their definitions first, colours before symbols, so that the objects
	// below use them. Then the symbols which the objects need: mine by
	// number, else theirs.
	Adopter adopter(mine, theirs);
	for (auto const& row : plan.colors)
	{
		if (!takes_color(row))
			continue;
		for (int i = 0; i < theirs.getNumColors(); ++i)
		{
			auto const* color = theirs.getColor(i);
			if (color->getName() == row.name)
			{
				adopter.takeColor(*color);
				++counts.definitions;
				break;
			}
		}
	}
	for (auto const& row : plan.symbols)
	{
		if (!takes(row))
			continue;
		if (auto const* other = differingSymbol(mine, theirs, row.number))
		{
			adopter.take(*other);
			++counts.definitions;
		}
	}
	QHash<const Symbol*, const Symbol*> symbols;
	auto const need = [&](const Object* object) {
		auto const* symbol = object->getSymbol();
		if (symbol && !symbols.contains(symbol))
			symbols.insert(symbol, adopter.symbol(symbol));
	};
	for (auto const& entry : replacements)
	{
		for (auto const& replacement : entry.second)
			need(replacement.other);
	}
	for (auto const& addition : additions)
		need(addition.other);
	counts.symbols = adopter.imported_symbols;
	counts.colors = adopter.added_colors;
	if (deletions.empty() && replacements.empty() && additions.empty())
	{
		if (result)
			*result = counts;
		return true;
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
