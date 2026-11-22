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

#ifndef FOOM_CHECK_LEGIBILITY_RESULTS_H
#define FOOM_CHECK_LEGIBILITY_RESULTS_H

#include <vector>

#include "check/issue.h"

namespace OpenOrienteering {

class Map;
struct LegibilityParams;


/**
 * Keeps the results of a legibility scan in the map: a saved selection per
 * check which ran, and for the gap check red marker objects in a part with
 * the role issues, under two symbols appended to the symbol set.
 *
 * See the design notes, "Legibility", the results as
 * selections and markers.
 */
void recordLegibilityResults(Map& map, const LegibilityParams& params, const std::vector<Issue>& issues);


}  // namespace OpenOrienteering

#endif
