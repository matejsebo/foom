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


#ifndef OPENORIENTEERING_CRT_RULES_H
#define OPENORIENTEERING_CRT_RULES_H

class QString;

namespace OpenOrienteering {

class Map;
class SymbolRuleSet;


/**
 * foom: Loads a CRT file without any dialog.
 *
 * The replacement symbols come from symbol_set, the patterns are matched
 * against object_map's symbols, as in SymbolReplacement. On failure, error
 * holds a message and the function returns false.
 */
bool loadCrtRules(const QString& path, const Map& symbol_set, const Map& object_map, SymbolRuleSet& rules, QString& error);


}  // namespace OpenOrienteering

#endif
