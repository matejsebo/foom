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


#ifndef OPENORIENTEERING_CHECK_MAP_CHECK_H
#define OPENORIENTEERING_CHECK_MAP_CHECK_H

#include <vector>

#include "check/issue.h"

namespace OpenOrienteering {

class Map;

namespace rule {

constexpr char text_empty[] = "text-empty";
constexpr char path_degenerate[] = "path-degenerate";
constexpr char symbol_undefined[] = "symbol-undefined";
constexpr char symbol_wrong_type[] = "symbol-wrong-type";
constexpr char object_duplicate[] = "object-duplicate";
constexpr char vertex_duplicate[] = "vertex-duplicate";
constexpr char area_open[] = "area-open";
constexpr char symbol_unused[] = "symbol-unused";
constexpr char color_unused[] = "color-unused";
constexpr char template_missing[] = "template-missing";
constexpr char text_path_overflow[] = "text-path-overflow";

}  // namespace rule


/**
 * Runs the map check over every part and returns the issues.
 *
 * See the design notes, "Map check".
 */
std::vector<Issue> checkMap(const Map& map);

/**
 * Applies the fix of the issue's rule.
 *
 * Object fixes go through ops::applyOp, so each is one undo step; deleting
 * an unused symbol or colour is not undoable. Returns false when nothing
 * was changed, for example because the rule has no fix or the objects are
 * gone.
 */
bool fixIssue(Map& map, const Issue& issue);


}  // namespace OpenOrienteering

#endif
