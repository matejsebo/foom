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

#include "colour_swatch.h"

#include <algorithm>
#include <cstddef>

#include <QtGlobal>
#include <QChar>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QSet>

#include "core/map.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::ColourSwatch", text);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

QString key(const QString& name)
{
	return name.trimmed().toCaseFolded();
}

/** A percentage, or a negative value when the cell is not a number. */
double percent(QString cell, QChar separator)
{
	cell = cell.trimmed();
	if (separator != QLatin1Char(','))
		cell.replace(QLatin1Char(','), QLatin1Char('.'));
	bool ok = false;
	auto const value = cell.toDouble(&ok);
	if (!ok)
		return -1;
	return qBound(0.0, value, 100.0);
}

}  // namespace


std::vector<SwatchEntry> readColourSwatch(const QByteArray& data, QString* error)
{
	std::vector<SwatchEntry> entries;

	auto text = QString::fromUtf8(data);
	if (text.contains(QChar::ReplacementCharacter))
		text = QString::fromLatin1(data);
	if (text.startsWith(QChar(0xfeff)))
		text.remove(0, 1);

	auto const lines = text.split(QLatin1Char('\n'));
	QChar separator;
	int name_column = -1;
	int columns = 0;
	for (auto line : lines)
	{
		if (line.endsWith(QLatin1Char('\r')))
			line.chop(1);
		if (name_column < 0)
		{
			// The header: the first row with a cell "Name".
			for (auto candidate : { QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char(',') })
			{
				if (!line.contains(candidate))
					continue;
				auto const cells = line.split(candidate);
				for (int i = 0; i < cells.size(); ++i)
				{
					if (cells[i].trimmed().compare(QLatin1String("name"), Qt::CaseInsensitive) == 0)
					{
						separator = candidate;
						name_column = i;
						columns = cells.size();
						break;
					}
				}
				break;  // The first separator in the row decides.
			}
			continue;
		}

		auto const cells = line.split(separator);
		auto const cell = [&cells](int i) { return i >= 0 && i < cells.size() ? cells[i] : QString(); };
		SwatchEntry entry;
		double values[4];
		auto valid = true;
		for (int i = 0; i < 4 && valid; ++i)
		{
			values[i] = percent(cell(name_column + 1 + i), separator);
			valid = values[i] >= 0;
		}
		if (!valid)
			continue;  // a title or an empty row
		entry.cmyk = MapColorCmyk(float(values[0] / 100), float(values[1] / 100), float(values[2] / 100), float(values[3] / 100));
		entry.name = cell(name_column).trimmed();
		if (name_column > 0)
		{
			bool ok = false;
			auto const number = cell(name_column - 1).trimmed().toInt(&ok);
			if (ok)
				entry.number = number;
		}
		if (columns > name_column + 5)
		{
			auto const value = percent(cell(name_column + 5), separator);
			if (value >= 0)
				entry.overprint = value > 0 ? 1 : 0;
		}
		if (columns > name_column + 6)
		{
			auto const value = percent(cell(name_column + 6), separator);
			if (value >= 0)
				entry.opacity = value / 100;
		}
		entries.push_back(entry);
	}

	if (name_column < 0)
		setError(error, tr("The file has no header row with a column \"Name\"."));
	else if (entries.empty())
		setError(error, tr("The file has no colours."));
	return entries;
}

std::vector<SwatchEntry> readColourSwatchFile(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		setError(error, tr("Cannot read %1: %2").arg(path, file.errorString()));
		return {};
	}
	return readColourSwatch(file.readAll(), error);
}


QString SwatchReport::text() const
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
	section(tr("Updated: %1"), updated);
	section(tr("Unchanged: %1"), unchanged);
	section(tr("Added: %1"), added);
	section(tr("Not in the map: %1"), not_found);
	return result;
}

SwatchReport applyColourSwatch(Map& map, const std::vector<SwatchEntry>& entries, const SwatchOptions& options)
{
	SwatchReport report;
	QSet<QString> seen;
	auto any_changed = false;
	for (auto const& entry : entries)
	{
		auto const entry_key = key(entry.name);
		if (entry_key.isEmpty() || seen.contains(entry_key))
			continue;
		seen.insert(entry_key);

		int index = -1;
		for (int i = 0; i < map.getNumColors() && index < 0; ++i)
		{
			if (key(map.getColor(i)->getName()) == entry_key)
				index = i;
		}
		if (index < 0)
		{
			if (!options.add_missing)
			{
				report.not_found.append(entry.name);
				continue;
			}
			auto* color = new MapColor(entry.name, map.getNumColors());
			color->setCmyk(entry.cmyk);
			if (options.opacity && entry.opacity >= 0)
				color->setOpacity(float(entry.opacity));
			map.addColor(color, map.getNumColors());
			report.added.append(entry.name);
			continue;
		}

		auto* color = map.getMapColor(index);
		MapColor before = *color;
		color->setCmyk(entry.cmyk);
		if (options.opacity && entry.opacity >= 0)
			color->setOpacity(float(entry.opacity));
		if (options.overprint && entry.overprint >= 0 && color->getSpotColorMethod() != MapColor::UndefinedMethod)
			color->setKnockout(entry.overprint == 0);
		if (color->equals(before) && color->getCmyk() == before.getCmyk())
		{
			report.unchanged.append(color->getName());
			continue;
		}
		map.setColor(color, index);  // signals the change
		report.updated.append(color->getName());
		any_changed = true;
	}
	if (any_changed || !report.added.isEmpty())
	{
		map.setColorsDirty();
		map.updateAllObjects();
	}
	return report;
}


}  // namespace OpenOrienteering
