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


#include "crt_rules.h"

#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTextStream>

#include "core/objects/symbol_rule_set.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

bool loadCrtRules(const QString& path, const Map& symbol_set, const Map& object_map, SymbolRuleSet& rules, QString& error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		error = QString::fromLatin1("cannot read the CRT file \"%1\": %2").arg(path, file.errorString());
		return false;
	}
	
	QTextStream stream(&file);
	rules = SymbolRuleSet::loadCrt(stream, symbol_set);
	if (stream.status() != QTextStream::Ok)
	{
		error = QString::fromLatin1("cannot parse the CRT file \"%1\"").arg(path);
		return false;
	}
	
	rules.recognizeSymbolPatterns(object_map);
	if (auto const* ambiguous = rules.findDuplicateSymbolPattern())
	{
		error = QString::fromLatin1("the CRT file \"%1\" has multiple replacements for symbol %2").arg(path, ambiguous->getNumberAsString());
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
