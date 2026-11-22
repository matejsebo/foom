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

#include "legibility_results.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

#include <QByteArray>
#include <QLatin1String>
#include <QString>
#include <QUuid>

#include "check/legibility.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/saved_selections.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

namespace OpenOrienteering {

namespace {

// Data in the map, not translated (the design notes, "Legibility").
const QLatin1String color_name("Legibility conflict");
const QLatin1String dot_name("Legibility conflict (dot)");
const QLatin1String line_name("Legibility conflict (line)");
const QLatin1String part_name("Legibility markers");


/** Replaces or removes the saved selection of the check's issues; returns true when it changed. */
bool recordSelection(std::vector<SavedSelection>& selections, const QString& name, const char* rule, const std::vector<Issue>& issues)
{
	SavedSelection selection { name, {} };
	for (auto const& issue : issues)
	{
		if (issue.rule != rule)
			continue;
		for (auto const& uuid : issue.objects)
		{
			if (std::find(begin(selection.uuids), end(selection.uuids), uuid) == end(selection.uuids))
				selection.uuids.push_back(uuid);
		}
	}
	auto found = std::find_if(begin(selections), end(selections), [&name](auto const& s) { return s.name == name; });
	if (selection.uuids.empty())
	{
		if (found == end(selections))
			return false;
		selections.erase(found);
		return true;
	}
	if (found == end(selections))
	{
		selections.push_back(std::move(selection));
		return true;
	}
	if (*found == selection)
		return false;
	*found = std::move(selection);
	return true;
}

const Symbol* findSymbol(const Map& map, const QLatin1String& name, Symbol::Type type)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == type && symbol->getName() == name)
			return symbol;
	}
	return nullptr;
}

const MapColor* findColor(const Map& map)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == color_name)
			return map.getColor(i);
	}
	return nullptr;
}

/** Adds the colour and the two marker symbols where they are missing. */
void ensureMarkerSymbols(Map& map, const Symbol*& dot, const Symbol*& line)
{
	auto const* red = findColor(map);
	if (!red)
	{
		auto* color = new MapColor(color_name, 0);
		color->setCmyk(MapColorCmyk(0.0f, 1.0f, 1.0f, 0.0f));
		color->setRgb(MapColorRgb(1.0f, 0.0f, 0.0f));
		map.addColor(color, 0);
		red = color;
	}
	auto number = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		number = std::max(number, map.getSymbol(i)->getNumberComponent(0));
	++number;
	if (!dot)
	{
		auto* symbol = new PointSymbol();
		symbol->setName(dot_name);
		symbol->setNumberComponent(0, number);
		symbol->setNumberComponent(1, 1);
		symbol->setInnerRadius(400);
		symbol->setInnerColor(nullptr);
		symbol->setOuterWidth(100);
		symbol->setOuterColor(red);
		map.addSymbol(symbol, map.getNumSymbols());
		dot = symbol;
	}
	if (!line)
	{
		auto* symbol = new LineSymbol();
		symbol->setName(line_name);
		symbol->setNumberComponent(0, number);
		symbol->setNumberComponent(1, 2);
		symbol->setLineWidth(0.1);
		symbol->setColor(red);
		map.addSymbol(symbol, map.getNumSymbols());
		line = symbol;
	}
}

/** Replaces the markers in the parts with the role issues, as one undo step. */
void recordMarkers(Map& map, const std::vector<Issue>& issues)
{
	std::vector<const Issue*> gaps;
	for (auto const& issue : issues)
	{
		if (issue.rule == rule::gap)
			gaps.push_back(&issue);
	}
	auto const* dot = findSymbol(map, dot_name, Symbol::Point);
	auto const* line = findSymbol(map, line_name, Symbol::Line);
	auto part_index = map.findPartIndexWithRole(MapPart::Role::Issues);
	if (gaps.empty() && (part_index < 0 || (!dot && !line)))
		return;  // nothing to remove, nothing to add

	if (!gaps.empty())
	{
		ensureMarkerSymbols(map, dot, line);
		if (part_index < 0)
		{
			auto* part = new MapPart(part_name, &map);
			part->setRole(MapPart::Role::Issues);
			map.addPart(part, std::size_t(map.getNumParts()));
			part_index = map.getNumParts() - 1;
		}
	}

	auto undo_step = std::make_unique<CombinedUndoStep>(&map);

	// Remove the old markers from every part with the role issues.
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(std::size_t(p));
		if (part->role() != MapPart::Role::Issues)
			continue;
		auto add_step = std::make_unique<AddObjectsUndoStep>(&map);
		add_step->setPartIndex(p);
		std::vector<Object*> removed;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			if (object->getSymbol() && (object->getSymbol() == dot || object->getSymbol() == line))
			{
				add_step->addObject(i, object);
				removed.push_back(object);
			}
		}
		for (auto* object : removed)
		{
			if (map.isObjectSelected(object))
				map.removeObjectFromSelection(object, false);
			part->releaseObject(object);
			object->setMap(&map);  // so that the undo step saves it correctly
		}
		if (!add_step->isEmpty())
			undo_step->push(add_step.release());
	}

	// Draw the new ones.
	if (!gaps.empty())
	{
		auto* part = map.getPart(std::size_t(part_index));
		auto delete_step = std::make_unique<DeleteObjectsUndoStep>(&map);
		delete_step->setPartIndex(part_index);
		for (auto const* gap : gaps)
		{
			auto* point = new PointObject(dot);
			point->setPosition(MapCoord(gap->where));
			part->addObject(point);
			delete_step->addObject(part->getNumObjects() - 1);
			if (gap->points.size() == 2)
			{
				auto* path = new PathObject(line, { MapCoord(gap->points[0]), MapCoord(gap->points[1]) });
				part->addObject(path);
				delete_step->addObject(part->getNumObjects() - 1);
			}
		}
		if (!delete_step->isEmpty())
			undo_step->push(delete_step.release());
	}

	if (undo_step->getNumSubSteps() > 0)
	{
		map.push(undo_step.release());
		map.setObjectsDirty();
	}
}

}  // namespace


void recordLegibilityResults(Map& map, const LegibilityParams& params, const std::vector<Issue>& issues)
{
	// the design notes, "Legibility", the results as selections and markers.
	auto selections = map.savedSelections();
	auto changed = false;
	if (params.minimum_length)
		changed |= recordSelection(selections, QStringLiteral("Legibility: short lines"), rule::line_short, issues);
	if (params.minimum_area)
		changed |= recordSelection(selections, QStringLiteral("Legibility: small areas"), rule::area_small, issues);
	if (params.gap)
		changed |= recordSelection(selections, QStringLiteral("Legibility: gaps"), rule::gap, issues);
	if (changed)
	{
		map.setSavedSelections(std::move(selections));
		map.setHasUnsavedChanges(true);
	}
	if (params.gap)
		recordMarkers(map, issues);
}


}  // namespace OpenOrienteering
