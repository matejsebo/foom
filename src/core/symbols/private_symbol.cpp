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


#include "private_symbol.h"

#include <cstddef>
#include <vector>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/symbol.h"
#include "undo/object_undo.h"
#include "undo/undo_manager.h"


namespace OpenOrienteering {

namespace {

/** Replaces the public parts by private duplicates, in nested combined symbols too. */
void detachParts(CombinedSymbol& combined)
{
	for (int i = 0; i < combined.getNumParts(); ++i)
	{
		auto const* part = combined.getPart(i);
		if (!part)
			continue;
		if (!combined.isPartPrivate(i))
		{
			auto copy = duplicate(*part);
			copy->setPrivate(false);
			combined.setPart(i, copy.release(), true);
			part = combined.getPart(i);
		}
		if (part->getType() == Symbol::Combined)
			detachParts(*const_cast<Symbol*>(part)->asCombined());  // a private part is owned by the symbol
	}
}

int countUsers(const Map& map, const Symbol* symbol)
{
	auto count = 0;
	map.applyOnAllObjects([&count, symbol](const Object* object) {
		count += object->getSymbol() == symbol;
	});
	return count;
}

}  // namespace


std::unique_ptr<Symbol> makePrivateCopy(const Symbol& symbol)
{
	auto copy = duplicate(symbol);
	copy->setPrivate(true);
	if (copy->getType() == Symbol::Combined)
		detachParts(*copy->asCombined());
	return copy;
}

int convertToGraphic(Map& map)
{
	auto* part = map.getCurrentPart();
	std::vector<Object*> objects;
	for (auto* object : map.selectedObjects())
	{
		auto const* symbol = object->getSymbol();
		if (!symbol || map.findSymbolIndex(symbol) < 0)
			continue;  // the undefined symbols
		if (symbol->isPrivate() && countUsers(map, symbol) == 1)
			continue;
		objects.push_back(object);
	}
	if (objects.empty())
		return 0;
	
	auto* undo = new SwitchSymbolUndoStep(&map);
	for (auto* object : objects)
	{
		auto* copy = makePrivateCopy(*object->getSymbol()).release();
		map.addSymbol(copy, map.getNumSymbols());
		undo->addObject(part->findObjectIndex(object), object->getSymbol());
		object->setSymbol(copy, true);
		object->update();
	}
	map.setObjectsDirty();
	map.push(undo);
	map.emitSelectionEdited();
	map.emitSelectionChanged();  // the symbols of the selected objects changed
	return int(objects.size());
}

const Symbol* sharedPrivateSymbol(const Map& map)
{
	const Symbol* shared = nullptr;
	for (auto const* object : map.selectedObjects())
	{
		auto const* symbol = object->getSymbol();
		if (!symbol || !symbol->isPrivate() || (shared && shared != symbol))
			return nullptr;
		shared = symbol;
	}
	return shared;
}

int purgePrivateSymbols(Map& map)
{
	std::vector<bool> in_use;
	map.determineSymbolsInUse(in_use);
	auto removed = 0;
	for (auto i = map.getNumSymbols(); i > 0; )
	{
		--i;
		auto const* symbol = map.getSymbol(i);
		if (!symbol->isPrivate() || in_use[std::size_t(i)] || map.undoManager().usesSymbol(symbol))
			continue;
		map.deleteSymbol(i);
		++removed;
	}
	return removed;
}


}  // namespace OpenOrienteering
