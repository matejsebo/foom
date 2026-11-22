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


#ifndef OPENORIENTEERING_CLI_BACKGROUND_STEPS_H
#define OPENORIENTEERING_CLI_BACKGROUND_STEPS_H

#include <QString>

#include "steps.h"

namespace OpenOrienteering {

class Map;

namespace Cli {

class Job;
struct Step;


/**
 * The steps for background maps, point clouds, canvases and GNSS files:
 * add-template, remove-template, georeference-template, point-cloud,
 * load-canvases and import-gnss.
 *
 * See the design notes, "Headless runner" and "Background maps
 * and point clouds in the runner".
 */
bool isBackgroundStep(const QString& name);

/// @throws JobError on a problem.
void validateBackgroundStep(const Step& step, bool map_open);

StepResult runBackgroundStep(Map& map, const QString& map_path, const Job& job, const Step& step);


}  // namespace Cli

}  // namespace OpenOrienteering

#endif
