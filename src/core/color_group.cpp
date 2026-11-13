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


#include "color_group.h"

#include <algorithm>

#include <QtGlobal>
#include <QCoreApplication>
#include <QLatin1String>
#include <QString>

#include "core/map_color.h"


namespace OpenOrienteering {

namespace {

struct NamedGroup
{
	const char* word;
	ColourGroup group;
};

constexpr NamedGroup named_groups[] = {
	{ "black", ColourGroup::Black },
	{ "brown", ColourGroup::Brown },
	{ "blue", ColourGroup::Blue },
	{ "green", ColourGroup::Green },
	{ "yellow", ColourGroup::Yellow },
	{ "purple", ColourGroup::Purple },
	{ "violet", ColourGroup::Purple },
	{ "magenta", ColourGroup::Purple },
};

constexpr NamedGroup keys[] = {
	{ "black", ColourGroup::Black },
	{ "brown", ColourGroup::Brown },
	{ "blue", ColourGroup::Blue },
	{ "green", ColourGroup::Green },
	{ "yellow", ColourGroup::Yellow },
	{ "purple", ColourGroup::Purple },
	{ "other", ColourGroup::Other },
};

}  // namespace


ColourGroup guessColourGroup(const MapColor& color)
{
	auto const name = color.getName().toLower();
	for (auto const& named : named_groups)
	{
		if (name.contains(QLatin1String(named.word)))
			return named.group;
	}
	
	auto const& cmyk = color.getCmyk();
	auto const c = double(cmyk.c);
	auto const m = double(cmyk.m);
	auto const y = double(cmyk.y);
	auto const k = double(cmyk.k);
	auto const mx = std::max({ c, m, y });
	if (mx <= 0.05 && k <= 0.05)
		return ColourGroup::Other;  // white
	if (k >= 0.5 && k >= mx)
		return ColourGroup::Black;
	if (mx < 0.05)
		return k >= 0.3 ? ColourGroup::Black : ColourGroup::Other;  // grey
	if (c >= mx / 2 && y >= mx / 2 && m < mx / 2)
		return ColourGroup::Green;
	if (c >= m && c >= y)
		return ColourGroup::Blue;
	if (y >= m)
		return m >= 0.4 * y ? ColourGroup::Brown : ColourGroup::Yellow;
	return ColourGroup::Purple;
}

ColourGroup colourGroupOf(const MapColor* color)
{
	if (!color)
		return ColourGroup::Other;
	auto const stored = color->getColourGroup();
	return stored == ColourGroup::Undefined ? guessColourGroup(*color) : stored;
}

QString colourGroupKey(ColourGroup group)
{
	for (auto const& key : keys)
	{
		if (key.group == group)
			return QString::fromLatin1(key.word);
	}
	return {};
}

ColourGroup colourGroupFromKey(const QString& key)
{
	for (auto const& candidate : keys)
	{
		if (key == QLatin1String(candidate.word))
			return candidate.group;
	}
	return ColourGroup::Undefined;
}

QString colourGroupName(ColourGroup group)
{
	switch (group)
	{
	case ColourGroup::Black:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Black");
	case ColourGroup::Brown:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Brown");
	case ColourGroup::Blue:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Blue");
	case ColourGroup::Green:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Green");
	case ColourGroup::Yellow:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Yellow");
	case ColourGroup::Purple:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Purple");
	case ColourGroup::Other:
		return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Other");
	case ColourGroup::Undefined:
		break;
	}
	return QCoreApplication::translate("OpenOrienteering::ColourGroup", "Automatic");
}


}  // namespace OpenOrienteering
