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

#include "glyph_code.h"

#include <QChar>
#include <QCoreApplication>
#include <QLatin1String>


namespace OpenOrienteering {

QString glyphFromHex(const QString& text, QString* error)
{
	auto const fail = [error](const char* reason) {
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::InsertGlyphDialog", reason);
		return QString{};
	};
	auto digits = text.trimmed();
	for (auto const* prefix : { "U+", "u+", "0x", "0X", "\\u", "\\U" })
	{
		if (digits.startsWith(QLatin1String(prefix)))
		{
			digits = digits.mid(int(qstrlen(prefix)));
			break;
		}
	}
	if (digits.isEmpty())
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "Enter a character code."));
	if (digits.size() > 8)
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "The code is above 10FFFF."));
	auto ok = false;
	auto const code = digits.toUInt(&ok, 16);
	if (!ok)
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "The code is not hexadecimal."));
	if (code == 0)
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "The code 0 is no character."));
	if (code > 0x10FFFF)
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "The code is above 10FFFF."));
	if (code >= 0xD800 && code <= 0xDFFF)
		return fail(QT_TRANSLATE_NOOP("OpenOrienteering::InsertGlyphDialog", "The codes D800 to DFFF are no characters."));
	if (error)
		error->clear();
	if (QChar::requiresSurrogates(code))
	{
		QChar const pair[] = { QChar(QChar::highSurrogate(code)), QChar(QChar::lowSurrogate(code)) };
		return QString(pair, 2);
	}
	return QString(QChar(code));
}


}  // namespace OpenOrienteering
