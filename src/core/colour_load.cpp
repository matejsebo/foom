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

#include "colour_load.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>

#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>

#include "core/map.h"
#include "core/map_color.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::ColourLoad", text);
}

QString key(const QString& name)
{
	return name.trimmed().toCaseFolded();
}

/** The source colour's definition, its spot colour components replaced by the map's. */
MapColor definitionFor(const MapColor& source, const std::map<const MapColor*, MapColor*>& mapping)
{
	MapColor definition = source;
	if (source.getSpotColorMethod() == MapColor::CustomColor)
	{
		SpotColorComponents components;
		for (auto const& component : source.getComponents())
		{
			auto const found = mapping.find(component.spot_color);
			if (found != mapping.end())
				components.push_back({ found->second, component.factor });
		}
		definition.setSpotColorComposition(components);
	}
	return definition;
}

}  // namespace


QString ColourLoadReport::text() const
{
	QString result;
	auto const section = [&result](const QString& title, const QStringList& names) {
		if (names.isEmpty())
			return;
		result += title.arg(names.size());
		result += QLatin1Char('\n');
		for (auto const& name : names)
		{
			result += QLatin1String("  ");
			result += name;
			result += QLatin1Char('\n');
		}
	};
	section(tr("Changed: %1"), changed);
	section(tr("Unchanged: %1"), unchanged);
	section(tr("Already in the map: %1"), existing);
	section(tr("Added: %1"), added);
	section(tr("Only in the map: %1"), kept);
	return result;
}

ColourLoadReport loadColoursFrom(Map& map, const Map& source, ColourLoadMode mode)
{
	ColourLoadReport report;

	// Match by name; the first colour of a name counts on either side.
	std::map<const MapColor*, MapColor*> mapping;  // source colour -> map colour
	std::vector<const MapColor*> sources;          // in the source's order, one per name
	std::vector<MapColor*> added;
	std::vector<bool> matched_in_map(std::size_t(map.getNumColors()), false);
	for (int i = 0; i < source.getNumColors(); ++i)
	{
		auto const* color = source.getColor(i);
		auto const color_key = key(color->getName());
		if (std::any_of(begin(sources), end(sources), [&color_key](auto const* other) { return key(other->getName()) == color_key; }))
			continue;
		sources.push_back(color);
		MapColor* target = nullptr;
		for (int j = 0; j < map.getNumColors() && !target; ++j)
		{
			if (key(map.getColor(j)->getName()) == color_key)
			{
				target = map.getMapColor(j);
				matched_in_map[std::size_t(j)] = true;
			}
		}
		if (!target)
		{
			target = new MapColor(color->getName(), map.getNumColors() + int(added.size()));
			added.push_back(target);
		}
		mapping[color] = target;
	}

	auto const is_new = [&added](const MapColor* color) {
		return std::find(begin(added), end(added), color) != end(added);
	};

	// Spot colours first, so that the colours mixed from them see their final values.
	std::vector<const MapColor*> ordered = sources;
	std::stable_partition(begin(ordered), end(ordered), [](auto const* color) {
		return color->getSpotColorMethod() == MapColor::SpotColor;
	});
	for (auto const* color : ordered)
	{
		auto* target = mapping[color];
		if (!is_new(target) && mode == ColourLoadMode::Append)
			continue;
		auto definition = definitionFor(*color, mapping);
		auto const same = target->equals(definition) && target->getCmyk() == definition.getCmyk()
		                  && target->getColourGroup() == definition.getColourGroup();
		auto const priority = target->getPriority();
		*target = definition;
		target->setPriority(priority);
		if (!is_new(target))
			(same ? report.unchanged : report.changed).append(target->getName());  // as the file spells it
	}

	for (auto* color : added)
		map.addColor(color, map.getNumColors());

	if (mode == ColourLoadMode::Replace)
	{
		// The file's colours in its order, then those only in the map.
		std::vector<MapColor*> order;
		for (auto const* color : sources)
			order.push_back(mapping[color]);
		for (int i = 0; i < int(matched_in_map.size()); ++i)
		{
			if (!matched_in_map[std::size_t(i)])
			{
				order.push_back(map.getMapColor(i));
				report.kept.append(map.getColor(i)->getName());
			}
		}
		// Each position at once, so that no colour is lost in between.
		for (int pos = 0; pos < int(order.size()); ++pos)
			map.setColor(order[std::size_t(pos)], pos);
	}
	else
	{
		for (auto const* color : sources)
		{
			if (!is_new(mapping[color]))
				report.existing.append(mapping[color]->getName());
		}
	}
	for (auto const* color : sources)
	{
		if (is_new(mapping[color]))
			report.added.append(color->getName());
	}

	map.setColorsDirty();
	map.updateAllObjects();
	return report;
}


}  // namespace OpenOrienteering
