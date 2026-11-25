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
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QTransform>

#include "core/georeferencing.h"
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

/**
 * Whether the coordinates stay within the bounds which a map file keeps when
 * it is read again (MapCoord::isRegular: 100 m of paper around the origin).
 * Seen on 2026-11-22: objects placed out of them by georeferencing were
 * dropped as irregular when the saved map was opened again.
 */
bool withinRegularBounds(const QRectF& rect)
{
	auto const limit = 99999.0;  // mm, MapCoord::isRegular() less a margin
	return rect.left() > -limit && rect.right() < limit && rect.top() > -limit && rect.bottom() < limit;
}

/// Where the imported objects' coordinates go by the placement.
QRectF placedBounds(const Map& map, const Map& imported, const MapImportPolicy& policy)
{
	auto const extent = coordinateBounds(imported);
	switch (policy.placement)
	{
	case MapImportPolicy::ByOffset:
		return extent.translated(policy.offset_mm);
	case MapImportPolicy::AtPoint:
		return extent.translated(policy.offset_mm - extent.center());
	case MapImportPolicy::ByGeoreferencing:
		break;
	}
	// As Map::importMap: the corners through both georeferencings
	auto const& georef = map.getGeoreferencing();
	auto const& other = imported.getGeoreferencing();
	QRectF result;
	for (auto const& corner : { extent.topLeft(), extent.topRight(), extent.bottomLeft(), extent.bottomRight() })
	{
		auto ok = false;
		auto const placed = QPointF(georef.toMapCoordF(&other, MapCoordF(corner), &ok));
		if (!ok)
			return extent;  // Map::importMap then leaves the coordinates as they are
		result = result.isNull() ? QRectF(placed, placed) : result.united(QRectF(placed, placed));
	}
	return result;
}

}  // namespace



std::vector<const Symbol*> mapSymbols(const Map& map)
{
	std::vector<const Symbol*> symbols;
	symbols.reserve(std::size_t(map.getNumSymbols()));
	for (int i = 0; i < map.getNumSymbols(); ++i)
		symbols.push_back(map.getSymbol(i));
	return symbols;
}

std::vector<const Symbol*> mapSymbolsAddedSince(const Map& map, const std::vector<const Symbol*>& before)
{
	std::vector<const Symbol*> added;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (std::find(before.begin(), before.end(), map.getSymbol(i)) == before.end())
			added.push_back(map.getSymbol(i));
	}
	return added;
}

MapImportResult importMapWithPolicy(Map& map, Map& imported, const MapImportPolicy& policy)
{
	return importMapWithPolicy(map, imported, policy, {});
}

MapImportResult importMapWithPolicy(Map& map, Map& imported, const MapImportPolicy& policy, const std::vector<const Symbol*>& map_symbols)
{
	MapImportResult result;
	if (imported.getNumColors() == 0 && imported.getNumSymbols() == 0 && imported.getNumObjects() == 0)
	{
		result.error = tr("Nothing to import.");
		return result;
	}

	if (policy.rescale && imported.getScaleDenominator() != map.getScaleDenominator())
	{
		// foom: the copies of the map's symbols keep their size (seen on 2026-11-24: scaled with the others,
		// they came back as smaller duplicates, and under "All symbols" replaced the map's own)
		auto const factor = imported.getScaleDenominator() / double(map.getScaleDenominator());
		imported.changeScale(map.getScaleDenominator(), 1.0, MapCoord(0, 0), map_symbols.empty(), true, true, true);
		if (!map_symbols.empty())
		{
			for (int i = 0; i < imported.getNumSymbols(); ++i)
			{
				if (std::find(map_symbols.begin(), map_symbols.end(), imported.getSymbol(i)) == map_symbols.end())
					imported.getSymbol(i)->scale(factor);
			}
			imported.updateAllObjects();
		}
	}

	// foom: nothing which the saved map would lose
	if (imported.getNumObjects() > 0 && !withinRegularBounds(placedBounds(map, imported, policy)))
	{
		result.error = tr("The objects would lie too far from the map (more than 100 m of paper from its origin), "
		                  "where they would be lost when the map is saved. Place them by offset or by mouse.");
		return result;
	}

	// foom: by mouse, the objects arrive selected, which needs them in one part, the current one
	if (policy.placement == MapImportPolicy::AtPoint)
	{
		while (imported.getNumParts() > 1)
			imported.mergeParts(1, 0);
	}

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

	// foom: under "All symbols and colours" the file's colours come as a
	// block of their own, none shared with the map's. For the import their names carry a mark with their place in
	// the file, so that OOM's import finds none of them equal to a colour of the map; the mark goes afterwards.
	auto const all_symbols = policy.symbols == MapImportPolicy::AllSymbols;
	auto const block_mark = QChar(0x1f);
	if (all_symbols)
	{
		for (int i = 0; i < imported.getNumColors(); ++i)
		{
			auto* color = imported.getMapColor(i);
			color->setName(QString(color->getName() + block_mark + QString::number(i)));
		}
	}

	// The import: OOM's, which shares symbols and colours equal to the map's
	auto const mode = all_symbols ? Map::ObjectImport : Map::MinimalObjectImport;
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

	// What the import made of the copies of the map's own symbols: under "All symbols" they are new symbols too,
	// since their colours are the block's, and go back to the map's symbols of their numbers.
	QSet<const Symbol*> map_symbols_added;
	for (auto const* copy : map_symbols)
	{
		if (auto const* added = symbol_map.value(copy, nullptr))
			map_symbols_added.insert(added);
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
		    && (policy.symbols == MapImportPolicy::NoSymbols || policy.symbols == MapImportPolicy::AbsentSymbols
		        || map_symbols_added.contains(symbol)))
		{
			mergeSymbolInto(map, symbol, own);
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

	// New colours which no symbol uses any longer go again. The others stay where OOM's import put them, at
	// their place in the file's order among the colours shared with the map; under "All symbols" they are a
	// block in the file's order at the top or the bottom.
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
	if (all_symbols)
	{
		auto const place = [block_mark](const MapColor* color) { return color->getName().section(block_mark, -1).toInt(); };
		std::stable_sort(new_order.begin(), new_order.end(), [&place](const MapColor* a, const MapColor* b) { return place(a) < place(b); });
		for (auto* color : new_order)
		{
			if (color->getName().contains(block_mark))
				color->setName(color->getName().section(block_mark, 0, -2));
		}
	}
	auto order = policy.colours_above ? new_order : old_order;
	auto const& rest = policy.colours_above ? old_order : new_order;
	order.insert(order.end(), rest.begin(), rest.end());
	auto changed = false;
	for (int i = 0; all_symbols && i < int(order.size()); ++i)
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
