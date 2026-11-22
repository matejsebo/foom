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

#include "dxf_crt.h"

#include <QChar>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringList>


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::DxfCrt", text);
}

/** Splits a line at spaces and tabs; double quotes hold a field with spaces. Returns false for an unclosed quote. */
bool splitFields(const QString& line, QStringList& fields)
{
	QString field;
	bool in_field = false;
	bool quoted = false;
	for (auto const c : line)
	{
		if (quoted)
		{
			if (c == QLatin1Char('"'))
			{
				quoted = false;
				fields.append(field);
				field.clear();
				in_field = false;
			}
			else
			{
				field.append(c);
			}
		}
		else if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
		{
			if (in_field)
			{
				fields.append(field);
				field.clear();
				in_field = false;
			}
		}
		else if (c == QLatin1Char('"') && !in_field)
		{
			quoted = true;
		}
		else
		{
			field.append(c);
			in_field = true;
		}
	}
	if (quoted)
		return false;
	if (in_field)
		fields.append(field);
	return true;
}

bool isSymbolNumber(const QString& text)
{
	static const QRegularExpression number(QStringLiteral("^[0-9]+(\\.[0-9]+)*$"));
	return number.match(text).hasMatch();
}

}  // namespace


QString normalisedSymbolNumber(const QString& number)
{
	auto parts = number.split(QLatin1Char('.'));
	while (parts.size() > 1 && parts.back().toInt() == 0 && !parts.back().isEmpty())
		parts.removeLast();
	for (auto& part : parts)
		part = QString::number(part.toInt());
	return parts.join(QLatin1Char('.'));
}

const DxfCrtRow* DxfCrt::find(const QString& symbol_number) const
{
	auto const wanted = normalisedSymbolNumber(symbol_number);
	for (auto const& row : rows)
	{
		if (normalisedSymbolNumber(row.symbol) == wanted)
			return &row;
	}
	return nullptr;
}

DxfCrt readDxfCrt(const QByteArray& data, QString* message)
{
	DxfCrt crt;
	auto const lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
	for (auto line : lines)
	{
		line = line.trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
			continue;

		QStringList fields;
		auto ok = splitFields(line, fields) && (fields.size() == 2 || fields.size() == 3)
		          && isSymbolNumber(fields[0]) && !fields[1].isEmpty();
		DxfCrtRow row;
		if (ok)
		{
			row.symbol = fields[0];
			row.layer = fields[1];
			if (fields.size() == 3)
			{
				row.colour = fields[2].toInt(&ok);
				ok = ok && row.colour >= 1 && row.colour <= 255;
			}
		}
		if (ok && crt.find(row.symbol))
			ok = false;  // The first row of a number counts.
		if (!ok)
		{
			++crt.skipped;
			continue;
		}
		crt.rows.push_back(row);
	}

	if (message)
	{
		if (crt.rows.empty())
			*message = tr("The cross-reference table has no row.");
		else if (crt.skipped > 0)
			*message = tr("%1 lines of the cross-reference table were skipped.").arg(crt.skipped);
		else
			message->clear();
	}
	return crt;
}

DxfCrt readDxfCrtFile(const QString& path, QString* message)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (message)
			*message = tr("Cannot read the cross-reference table \"%1\".").arg(path);
		return {};
	}
	return readDxfCrt(file.readAll(), message);
}

bool writeDxfCrt(const QString& path, const DxfCrt& crt, QString* error)
{
	QByteArray text;
	for (auto const& row : crt.rows)
	{
		auto layer = row.layer;
		layer.replace(QLatin1Char('"'), QLatin1Char('\''));
		if (layer.contains(QLatin1Char(' ')) || layer.contains(QLatin1Char('\t')) || layer.startsWith(QLatin1Char(';')))
			layer = QLatin1Char('"') + layer + QLatin1Char('"');
		text += row.symbol.toUtf8();
		text += '\t';
		text += layer.toUtf8();
		if (row.colour > 0)
		{
			text += '\t';
			text += QByteArray::number(row.colour);
		}
		text += '\n';
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text) != text.size())
	{
		if (error)
			*error = tr("Cannot write the cross-reference table \"%1\".").arg(path);
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
