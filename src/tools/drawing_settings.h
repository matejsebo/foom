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


#ifndef OPENORIENTEERING_DRAWING_SETTINGS_H
#define OPENORIENTEERING_DRAWING_SETTINGS_H

namespace OpenOrienteering {

/*
 * foom: settings of the drawing tools.
 *
 * See the design notes, "Freehand smoothing" and "Join while drawing".
 */

/** 0 off, 1 slight, 2 medium (the default), 3 strong: the setting Editor/freehand_smoothing. */
int freehandSmoothingLevel();
void setFreehandSmoothingLevel(int level);

/** The tolerance of the freehand smoothing in mm for a level; 0 for off. */
double freehandSmoothingTolerance(int level);

/** Whether finished lines join others of their symbol: the setting Editor/auto_join, off by default. */
bool autoJoinEnabled();
void setAutoJoinEnabled(bool enabled);

/** How near the ends must be, in mm on paper: Editor/auto_join_tolerance_mm, 0.25 by default. */
double autoJoinToleranceMm();


}  // namespace OpenOrienteering

#endif
