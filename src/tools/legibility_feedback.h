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


#ifndef OPENORIENTEERING_LEGIBILITY_FEEDBACK_H
#define OPENORIENTEERING_LEGIBILITY_FEEDBACK_H

#include <QString>

class QPainter;

namespace OpenOrienteering {

class Map;
class MapWidget;
class PathObject;
class Symbol;


/**
 * Returns the status bar readout of the path being drawn against the
 * symbol's minimum length or area, as HTML; empty when there is nothing
 * to show or the setting is off.
 *
 * See the design notes, "Legibility", drawing-time feedback.
 */
QString legibilityReadout(const Map& map, const Symbol* symbol, const PathObject& preview);

/**
 * Returns whether the clearance halo is to be drawn (the setting).
 */
bool legibilityHaloEnabled();

/**
 * Returns the halo distance in mm: the setting, or the map's default gap
 * while the setting is zero ("per specification").
 */
double legibilityHaloDistance(const Map& map);

/**
 * Returns how far the halo reaches out from the preview's centre line, in
 * mm: half the symbol's widest stroke plus the distance. Drawing tools grow
 * their dirty rectangle by this.
 */
double legibilityHaloMargin(const Map& map, const Symbol* symbol);

/**
 * Draws the clearance halo under the preview: a translucent stroke along
 * the preview's centre line, as wide as the symbol's widest stroke plus
 * twice the distance. The painter is in viewport coordinates.
 */
void drawLegibilityHalo(QPainter* painter, const MapWidget* widget, const PathObject& preview, const Symbol* symbol, double distance_mm);


}  // namespace OpenOrienteering

#endif
