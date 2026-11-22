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

#include "colour_usage.h"

#include <cstddef>

#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "templates/template.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::ColourUsage", text, nullptr, n);
}

}  // namespace


std::vector<ColourUse> colourUsage(const Map& map)
{
	std::vector<ColourUse> usage(std::size_t(map.getNumColors()));
	for (int c = 0; c < map.getNumColors(); ++c)
	{
		auto& use = usage[std::size_t(c)];
		use.color = map.getColor(c);
		for (int s = 0; s < map.getNumSymbols(); ++s)
		{
			auto const* symbol = map.getSymbol(s);
			if (symbol->getType() != Symbol::NoSymbol && symbol->containsColor(use.color))
				use.symbols.append(symbol->getNumberAndPlainTextName());
		}
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			auto const* part = map.getPart(std::size_t(p));
			for (int i = 0; i < part->getNumObjects(); ++i)
			{
				auto const* symbol = part->getObject(i)->getSymbol();
				if (symbol && symbol->containsColor(use.color))
					++use.objects;
			}
		}
		for (int t = 0; t < map.getNumTemplates(); ++t)
		{
			if (map.getTemplate(t)->separationColor() == use.color)
				++use.templates;
		}
		use.used = !use.symbols.isEmpty() || use.templates > 0;
	}

	// A spot colour is used when a used colour is mixed from it.
	for (int c = 0; c < map.getNumColors(); ++c)
	{
		auto& use = usage[std::size_t(c)];
		for (int other = 0; other < map.getNumColors(); ++other)
		{
			if (other != c && map.getColor(other)->hasSpotColorComponent(use.color))
				use.mixed_into.append(map.getColor(other)->getName());
		}
	}
	for (auto changed = true; changed; )
	{
		changed = false;
		for (int c = 0; c < map.getNumColors(); ++c)
		{
			auto& use = usage[std::size_t(c)];
			if (use.used)
				continue;
			for (int other = 0; other < map.getNumColors() && !use.used; ++other)
			{
				if (usage[std::size_t(other)].used && map.getColor(other)->hasSpotColorComponent(use.color))
					use.used = changed = true;
			}
		}
	}
	return usage;
}

QString colourUsageText(const std::vector<ColourUse>& usage)
{
	QString result;
	for (auto const& use : usage)
	{
		result += use.color->getName();
		result += QLatin1String(": ");
		QStringList parts;
		if (!use.symbols.isEmpty())
			parts.append(tr("%n symbol(s)", int(use.symbols.size())) + QLatin1String(", ") + tr("%n object(s)", use.objects));
		if (!use.mixed_into.isEmpty())
			parts.append(tr("mixed into %1").arg(use.mixed_into.join(QLatin1String(", "))));
		if (use.templates > 0)
			parts.append(tr("plate of %n template(s)", use.templates));
		if (!use.used)
			parts.append(tr("not used"));
		result += parts.join(QLatin1String("; "));
		result += QLatin1Char('\n');
		for (auto const& symbol : use.symbols)
		{
			result += QLatin1String("  ");
			result += symbol;
			result += QLatin1Char('\n');
		}
	}
	return result;
}

QStringList deleteUnusedColours(Map& map)
{
	QStringList deleted;
	auto const usage = colourUsage(map);
	for (int c = int(usage.size()) - 1; c >= 0; --c)
	{
		if (usage[std::size_t(c)].used)
			continue;
		deleted.prepend(map.getColor(c)->getName());
		map.deleteColor(c);
	}
	if (!deleted.isEmpty())
	{
		map.setColorsDirty();
		map.updateAllObjects();
	}
	return deleted;
}


}  // namespace OpenOrienteering
