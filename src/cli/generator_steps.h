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


#ifndef FOOM_CLI_GENERATOR_STEPS_H
#define FOOM_CLI_GENERATOR_STEPS_H

#include <QString>

#include "steps.h"

namespace OpenOrienteering {

class Map;

namespace Cli {

struct Step;


/**
 * Returns true for the steps "generate" and "regenerate".
 *
 * See "Headless runner" in the design notes and the third change
 * of "Generators" in the design notes.
 */
bool isGeneratorStep(const QString& name);

/**
 * Validates a step "generate" or "regenerate"; throws a JobError.
 */
void validateGeneratorStep(const Step& step, bool map_open);

/**
 * Runs a step "generate" or "regenerate" on the map.
 */
StepResult runGeneratorStep(Map& map, const Step& step);


}  // namespace Cli

}  // namespace OpenOrienteering

#endif
