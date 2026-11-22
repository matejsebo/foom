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

#include <QChar>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRegularExpression>

#include "core/map.h"
#include "core/map_color.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::ColourLoad", text, nullptr, n);
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

/** Reads a colour number of a table: an integer, or one with only zeros after a decimal point. */
bool crtNumber(const QString& text, int* number)
{
	static const QRegularExpression pattern(QStringLiteral("^(-?[0-9]+)(\\.0*)?$"));
	auto const match = pattern.match(text);
	if (!match.hasMatch())
		return false;
	bool ok = false;
	*number = match.captured(1).toInt(&ok);
	return ok;
}

}  // namespace


bool ColourCrt::find(int source, int* target) const
{
	auto const row = std::find_if(begin(rows), end(rows), [source](auto const& r) { return r.first == source; });
	if (row == end(rows))
		return false;
	if (target)
		*target = row->second;
	return true;
}

ColourCrt readColourCrt(const QByteArray& data, QString* message)
{
	// the design notes, "Loading through a cross-reference table".
	ColourCrt crt;
	auto text = QString::fromUtf8(data);
	if (text.startsWith(QChar(0xfeff)))
		text.remove(0, 1);
	static const QRegularExpression separators(QStringLiteral("[\\t ;,]+"));
	for (auto const& raw_line : text.split(QLatin1Char('\n')))
	{
		auto const line = raw_line.trimmed();
		if (line.isEmpty())
			continue;
		auto const cells = line.split(separators, Qt::SkipEmptyParts);
		int source = 0, target = 0;
		if (cells.size() < 2 || !crtNumber(cells[0], &source) || !crtNumber(cells[1], &target))
		{
			++crt.skipped;
			continue;
		}
		if (!crt.find(source, nullptr))
			crt.rows.emplace_back(source, target);
	}
	if (message)
	{
		if (crt.rows.empty())
			*message = tr("The cross-reference table has no row of two colour numbers.");
		else if (crt.skipped > 0)
			*message = tr("%n line(s) of the cross-reference table are not two colour numbers and were skipped.", crt.skipped);
	}
	return crt;
}

ColourCrt readColourCrtFile(const QString& path, QString* message)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (message)
			*message = tr("Cannot read %1.").arg(path);
		return {};
	}
	return readColourCrt(file.readAll(), message);
}


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
	section(tr("Left out: %1"), left_out);
	return result;
}

ColourLoadReport loadColoursFrom(Map& map, const Map& source, ColourLoadMode mode, const ColourCrt* crt)
{
	ColourLoadReport report;

	// Match by number through the table for the colours it lists, else by name;
	// the first colour of a name counts on either side, and a map colour takes one colour of the file.
	std::map<const MapColor*, MapColor*> mapping;  // source colour -> map colour
	std::vector<const MapColor*> sources;          // in the source's order, one per target
	std::vector<const MapColor*> by_name;          // the sources matched by name, for the first-of-a-name rule
	std::vector<MapColor*> added;
	std::map<const MapColor*, int> added_numbers;  // added colour -> its number
	std::vector<bool> matched_in_map(std::size_t(map.getNumColors()), false);
	auto const is_taken = [&mapping](const MapColor* target) {
		return std::any_of(begin(mapping), end(mapping), [target](auto const& entry) { return entry.second == target; });
	};
	auto const number_in_map = [&map, &added_numbers](int number) {
		if (number < 0)
			return false;
		for (int j = 0; j < map.getNumColors(); ++j)
		{
			if (map.getColor(j)->getNumber() == number)
				return true;
		}
		return std::any_of(begin(added_numbers), end(added_numbers), [number](auto const& entry) { return entry.second == number; });
	};
	for (int i = 0; i < source.getNumColors(); ++i)
	{
		auto const* color = source.getColor(i);
		MapColor* target = nullptr;
		int target_number = -1;
		auto const through_table = crt && color->getNumber() >= 0 && crt->find(color->getNumber(), &target_number);
		if (through_table)
		{
			if (target_number < 0)
			{
				report.left_out.append(color->getName());
				continue;
			}
			for (int j = 0; j < map.getNumColors() && !target; ++j)
			{
				if (map.getColor(j)->getNumber() == target_number)
					target = map.getMapColor(j);
			}
			if (!target)
			{
				// A second colour of the file for a target number which is added already.
				auto const twin = std::find_if(begin(added_numbers), end(added_numbers), [target_number](auto const& entry) { return entry.second == target_number; });
				if (twin != end(added_numbers))
				{
					report.left_out.append(color->getName());
					continue;
				}
			}
		}
		else
		{
			auto const color_key = key(color->getName());
			if (std::any_of(begin(by_name), end(by_name), [&color_key](auto const* other) { return key(other->getName()) == color_key; }))
				continue;
			by_name.push_back(color);
			for (int j = 0; j < map.getNumColors() && !target; ++j)
			{
				if (key(map.getColor(j)->getName()) == color_key)
					target = map.getMapColor(j);
			}
		}
		if (target && is_taken(target))
		{
			report.left_out.append(color->getName());
			continue;
		}
		if (target)
		{
			matched_in_map[std::size_t(map.findColorIndex(target))] = true;
		}
		else
		{
			target = new MapColor(color->getName(), map.getNumColors() + int(added.size()));
			auto const number = through_table ? target_number
			                    : number_in_map(color->getNumber()) ? -1 : color->getNumber();
			added.push_back(target);
			added_numbers[target] = number;
		}
		sources.push_back(color);
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
		auto const number = is_new(target) ? added_numbers[target] : target->getNumber();
		*target = definition;
		target->setPriority(priority);
		target->setNumber(number);
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
