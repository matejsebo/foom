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


#include "import_points.h"

#include <memory>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QLocale>
#include <QPointF>
#include <QRegularExpression>

#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::ImportPoints", text, nullptr, n);
}

int countOutsideQuotes(const QString& line, QChar delimiter)
{
	auto count = 0;
	auto quoted = false;
	for (auto const c : line)
	{
		if (c == QLatin1Char('"'))
			quoted = !quoted;
		else if (c == delimiter && !quoted)
			++count;
	}
	return count;
}

QStringList nonEmptyLines(const QStringList& lines, int max)
{
	QStringList result;
	for (auto const& line : lines)
	{
		if (line.trimmed().isEmpty())
			continue;
		result.push_back(line);
		if (result.size() == max)
			break;
	}
	return result;
}

double parseNumber(const QString& field, bool* ok)
{
	auto const text = field.trimmed();
	auto value = QLocale::c().toDouble(text, ok);
	if (!*ok && text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('.')))
		value = QLocale::c().toDouble(QString(text).replace(QLatin1Char(','), QLatin1Char('.')), ok);
	return value;
}

bool isNumberAt(const QStringList& fields, int column)
{
	if (column < 0 || column >= fields.size())
		return false;
	bool ok = false;
	parseNumber(fields[column], &ok);
	return ok;
}

const Symbol* symbolForCode(const Map& map, const QString& code)
{
	if (code.isEmpty())
		return nullptr;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Point && symbol->getNumberAsString() == code)
			return symbol;
	}
	return nullptr;
}

QString fieldAt(const QStringList& fields, int column)
{
	return column >= 0 && column < fields.size() ? fields[column] : QString{};
}

}  // namespace



QChar detectDelimiter(const QStringList& lines)
{
	auto const sample = nonEmptyLines(lines, 5);
	for (auto const candidate : { QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char(',') })
	{
		auto expected = -1;
		auto same = !sample.isEmpty();
		for (auto const& line : sample)
		{
			auto const n = countOutsideQuotes(line, candidate);
			if (n == 0 || (expected >= 0 && n != expected))
			{
				same = false;
				break;
			}
			expected = n;
		}
		if (same)
			return candidate;
	}
	return QLatin1Char(' ');
}


QStringList splitDelimited(const QString& line, QChar delimiter)
{
	QStringList fields;
	if (delimiter == QLatin1Char(' '))
	{
		static const QRegularExpression blanks(QStringLiteral("\\s+"));
		for (auto const& field : line.split(blanks, Qt::SkipEmptyParts))
		{
			auto f = field;
			if (f.size() >= 2 && f.startsWith(QLatin1Char('"')) && f.endsWith(QLatin1Char('"')))
				f = f.mid(1, f.size() - 2);
			fields.push_back(f);
		}
		return fields;
	}
	
	QString field;
	auto quoted = false;
	for (int i = 0; i < line.size(); ++i)
	{
		auto const c = line[i];
		if (c == QLatin1Char('"'))
		{
			if (quoted && i + 1 < line.size() && line[i + 1] == QLatin1Char('"'))
			{
				field.append(c);
				++i;
			}
			else
			{
				quoted = !quoted;
			}
		}
		else if (c == delimiter && !quoted)
		{
			fields.push_back(field.trimmed());
			field.clear();
		}
		else
		{
			field.append(c);
		}
	}
	fields.push_back(field.trimmed());
	return fields;
}


bool detectHeader(const QStringList& lines, QChar delimiter, int x_column, int y_column)
{
	auto const sample = nonEmptyLines(lines, 2);
	if (sample.size() < 2)
		return false;
	auto const first = splitDelimited(sample[0], delimiter);
	auto const second = splitDelimited(sample[1], delimiter);
	for (auto column : { x_column, y_column })
	{
		if (isNumberAt(second, column) && !isNumberAt(first, column) && column < first.size())
			return true;
	}
	return false;
}


OpResult importPoints(const Map& map, const QString& text, const ImportPointsParams& params)
{
	OpResult result;
	if (!params.default_symbol || params.default_symbol->getType() != Symbol::Point)
	{
		result.error = tr("Choose a point symbol for the points.");
		return result;
	}
	if (params.text_column >= 0 && (!params.text_symbol || params.text_symbol->getType() != Symbol::Text))
	{
		result.error = tr("Choose a text symbol for the texts.");
		return result;
	}
	
	auto const& georef = map.getGeoreferencing();
	if ((params.crs == ImportPointsParams::Wgs84 || params.crs == ImportPointsParams::OtherCrs)
	    && georef.getState() != Georeferencing::Geospatial)
	{
		result.error = tr("The map has no coordinate reference system. Import the coordinates in map coordinates or paper millimetres.");
		return result;
	}
	ProjTransform other;
	if (params.crs == ImportPointsParams::OtherCrs)
	{
		other = ProjTransform(params.crs_spec);
		if (!other.isValid())
		{
			result.error = tr("Unknown coordinate reference system: %1").arg(params.crs_spec);
			return result;
		}
	}
	
	static const QRegularExpression line_break(QStringLiteral("\r\n|\n|\r"));
	auto const lines = text.split(line_break);
	auto header_pending = params.header;
	QStringList skipped;
	auto defaulted = 0;
	std::vector<std::unique_ptr<Object>> texts;
	for (int i = 0; i < lines.size(); ++i)
	{
		auto const& line = lines[i];
		if (line.trimmed().isEmpty())
			continue;
		if (header_pending)
		{
			header_pending = false;
			continue;
		}
		
		auto const fields = splitDelimited(line, params.delimiter);
		bool ok_x = false;
		bool ok_y = false;
		auto const x = params.x_column < fields.size() ? parseNumber(fieldAt(fields, params.x_column), &ok_x) : 0.0;
		auto const y = params.y_column < fields.size() ? parseNumber(fieldAt(fields, params.y_column), &ok_y) : 0.0;
		if (!ok_x || !ok_y)
		{
			skipped.push_back(QString::number(i + 1));
			continue;
		}
		
		MapCoordF position;
		switch (params.crs)
		{
		case ImportPointsParams::MapCrs:
			position = georef.toMapCoordF(QPointF(x, y));
			break;
		case ImportPointsParams::Wgs84:
			position = georef.toMapCoordF(LatLon(y, x));
			break;
		case ImportPointsParams::OtherCrs:
			{
				bool ok = false;
				auto const lat_lon = other.inverse(QPointF(x, y), &ok);
				if (!ok)
				{
					skipped.push_back(QString::number(i + 1));
					continue;
				}
				position = georef.toMapCoordF(lat_lon);
			}
			break;
		case ImportPointsParams::PaperMillimetres:
			position = MapCoordF(x, -y);
			break;
		}
		
		auto const* symbol = symbolForCode(map, fieldAt(fields, params.code_column));
		if (!symbol)
		{
			symbol = params.default_symbol;
			++defaulted;
		}
		auto point = std::make_unique<PointObject>(symbol);
		point->setPosition(position);
		auto const z = fieldAt(fields, params.z_column);
		if (!z.isEmpty())
			point->setTag(QStringLiteral("ele"), z);
		
		auto const label = fieldAt(fields, params.text_column);
		if (!label.isEmpty())
		{
			auto text_object = std::make_unique<TextObject>(params.text_symbol);
			text_object->setAnchorPosition(position);
			text_object->setHorizontalAlignment(TextObject::AlignHCenter);
			text_object->setVerticalAlignment(TextObject::AlignVCenter);
			text_object->setText(label);
			texts.push_back(std::move(text_object));
		}
		result.out.push_back(std::move(point));
	}
	
	if (result.out.empty())
	{
		result.error = tr("No row has coordinates in the chosen columns.");
		return result;
	}
	for (auto& text_object : texts)
		result.out.push_back(std::move(text_object));
	
	QStringList warnings;
	if (params.code_column >= 0 && defaulted > 0)
		warnings.push_back(tr("%n point(s) got the default symbol.", defaulted));
	if (!skipped.isEmpty())
		warnings.push_back(tr("Skipped line(s) without coordinates: %1.", skipped.size()).arg(skipped.join(QLatin1String(", "))));
	result.warning = warnings.join(QLatin1Char('\n'));
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
