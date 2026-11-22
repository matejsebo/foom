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

#include "map_import_policy.h"

#include <algorithm>
#include <array>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QTransform>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::MapImportPolicy", text, nullptr, n);
}

using Number = std::array<int, 3>;

Number numberOf(const Symbol* symbol)
{
	return { symbol->getNumberComponent(0), symbol->getNumberComponent(1), symbol->getNumberComponent(2) };
}

/// The symbol of the map with this number, nullptr when none.
const Symbol* symbolNumbered(const Map& map, const QString& number, const Symbol* except = nullptr)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol != except && symbol->getNumberAsString() == number)
			return symbol;
	}
	return nullptr;
}

bool numberTaken(const Map& map, const Number& number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (numberOf(map.getSymbol(i)) == number)
			return true;
	}
	return false;
}

/// Gives the symbol the first free number below its own: 102 -> 102.1, 102.2, ...; 102.1 -> 102.1.1; 102.1.1 -> 102.1.2.
void renumber(Map& map, Symbol* symbol)
{
	auto number = numberOf(symbol);
	std::size_t used = 0;
	while (used < number.size() && number[used] >= 0)
		++used;
	auto const index = used < number.size() ? used : number.size() - 1;
	auto candidate = number;
	candidate[index] = used < number.size() ? 1 : number[index] + 1;
	while (numberTaken(map, candidate))
		++candidate[index];
	symbol->setNumberComponent(int(index), candidate[index]);
}

/// Moves the objects and the references of other symbols from one symbol to another, and deletes the first.
void mergeSymbolInto(Map& map, const Symbol* from, const Symbol* to)
{
	map.changeSymbolForAllObjects(from, to);
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (symbol != from && symbol->symbolChangedEvent(from, to))
			map.updateAllObjectsWithSymbol(symbol);
	}
	map.deleteSymbol(map.findSymbolIndex(from));
}

/// Whether another colour of the map uses this one as a spot colour component.
bool isSpotComponent(const Map& map, const MapColor* color)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* other = map.getColor(i);
		if (other == color || other->getSpotColorMethod() != MapColor::CustomColor)
			continue;
		for (auto const& component : other->getComponents())
		{
			if (component.spot_color == color)
				return true;
		}
	}
	return false;
}

/// The bounding box of the objects' coordinates, without symbol extents.
QRectF coordinateBounds(const Map& map)
{
	auto first = true;
	double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			for (auto const& coord : part->getObject(i)->getRawCoordinateVector())
			{
				min_x = first ? coord.x() : std::min(min_x, coord.x());
				min_y = first ? coord.y() : std::min(min_y, coord.y());
				max_x = first ? coord.x() : std::max(max_x, coord.x());
				max_y = first ? coord.y() : std::max(max_y, coord.y());
				first = false;
			}
		}
	}
	if (first)
		return {};
	return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
}

}  // namespace



MapImportResult importMapWithPolicy(Map& map, Map& imported, const MapImportPolicy& policy)
{
	MapImportResult result;
	if (imported.getNumColors() == 0 && imported.getNumSymbols() == 0 && imported.getNumObjects() == 0)
	{
		result.error = tr("Nothing to import.");
		return result;
	}

	if (policy.rescale && imported.getScaleDenominator() != map.getScaleDenominator())
		imported.changeScale(map.getScaleDenominator(), 1.0, MapCoord(0, 0), true, true, true, true);

	// Without the symbols: the objects of symbols which the map lacks are left out.
	if (policy.symbols == MapImportPolicy::NoSymbols)
	{
		for (int p = 0; p < imported.getNumParts(); ++p)
		{
			auto* part = imported.getPart(std::size_t(p));
			for (int i = part->getNumObjects() - 1; i >= 0; --i)
			{
				auto const* symbol = part->getObject(i)->getSymbol();
				auto const* own = symbol ? symbolNumbered(map, symbol->getNumberAsString()) : nullptr;
				if (!own || own->getType() != symbol->getType())
				{
					part->deleteObject(i);
					++result.objects_dropped;
				}
			}
		}
		if (result.objects_dropped > 0)
			result.warnings.append(tr("%n object(s) left out: the map has no symbol of their number.", result.objects_dropped));
	}

	// What the map has before
	QSet<const Symbol*> old_symbols;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		old_symbols.insert(map.getSymbol(i));
	QSet<const MapColor*> old_colors;
	for (int i = 0; i < map.getNumColors(); ++i)
		old_colors.insert(map.getColor(i));
	auto const objects_before = map.getNumObjects();

	// The import: OOM's, which shares symbols and colours equal to the map's
	auto const mode = policy.symbols == MapImportPolicy::AllSymbols ? Map::ObjectImport : Map::MinimalObjectImport;
	QHash<const Symbol*, Symbol*> symbol_map;
	switch (policy.placement)
	{
	case MapImportPolicy::ByGeoreferencing:
		symbol_map = map.importMap(imported, mode | Map::GeorefImport);
		break;
	case MapImportPolicy::ByOffset:
		symbol_map = map.importMap(imported, mode, QTransform::fromTranslate(policy.offset_mm.x(), policy.offset_mm.y()));
		break;
	case MapImportPolicy::AtPoint:
		{
			auto const extent = coordinateBounds(imported);
			auto const shift = policy.offset_mm - extent.center();  // an empty map has a null extent at the origin
			symbol_map = map.importMap(imported, mode, QTransform::fromTranslate(shift.x(), shift.y()));
		}
		break;
	}

	// The new symbols, in the order of the imported map
	std::vector<Symbol*> new_symbols;
	for (int i = 0; i < imported.getNumSymbols(); ++i)
	{
		auto* added = symbol_map.value(imported.getSymbol(i), nullptr);
		if (added && !old_symbols.contains(added)
		    && std::find(new_symbols.begin(), new_symbols.end(), added) == new_symbols.end())
			new_symbols.push_back(added);
	}

	for (auto* symbol : new_symbols)
	{
		auto const* own = symbolNumbered(map, symbol->getNumberAsString(), symbol);
		if (own && !old_symbols.contains(own))
			own = nullptr;  // another new symbol, already handled
		if (!own)
		{
			if (policy.protect_symbols)
				symbol->setProtected(true);
			++result.symbols_added;
			continue;
		}

		auto const same_type = own->getType() == symbol->getType();
		if (same_type
		    && (policy.symbols == MapImportPolicy::NoSymbols || policy.symbols == MapImportPolicy::AbsentSymbols))
		{
			mergeSymbolInto(map, symbol, own);
		}
		else if (same_type && policy.symbols == MapImportPolicy::AllSymbols)
		{
			auto const position = map.findSymbolIndex(own);
			mergeSymbolInto(map, own, symbol);
			map.moveSymbol(map.findSymbolIndex(symbol), position);
			if (policy.protect_symbols)
				symbol->setProtected(true);
			++result.symbols_replaced;
		}
		else
		{
			renumber(map, symbol);
			if (policy.protect_symbols)
				symbol->setProtected(true);
			++result.symbols_added;
			++result.symbols_renumbered;
		}
	}
	if (result.symbols_renumbered > 0)
		result.warnings.append(tr("%n symbol(s) renumbered: their number was taken by a different symbol.", result.symbols_renumbered));

	// New colours which no symbol uses any longer go again; the others go to the top or the bottom.
	for (int i = map.getNumColors() - 1; i >= 0; --i)
	{
		auto const* color = map.getColor(i);
		if (!old_colors.contains(color) && !map.isColorUsedByASymbol(color) && !isSpotComponent(map, color))
			map.deleteColor(i);
	}
	std::vector<MapColor*> old_order, new_order;
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto* color = map.getMapColor(i);
		(old_colors.contains(color) ? old_order : new_order).push_back(color);
	}
	auto order = policy.colours_above ? new_order : old_order;
	auto const& rest = policy.colours_above ? old_order : new_order;
	order.insert(order.end(), rest.begin(), rest.end());
	auto changed = false;
	for (int i = 0; i < int(order.size()); ++i)
		changed = changed || map.getMapColor(i) != order[std::size_t(i)];
	if (changed)
	{
		for (int i = 0; i < int(order.size()); ++i)
			map.setColor(order[std::size_t(i)], i);
		map.setColorsDirty();
		map.updateAllObjects();
	}
	map.setSymbolsDirty();

	result.objects = map.getNumObjects() - objects_before;
	result.ok = true;
	return result;
}


}  // namespace OpenOrienteering
