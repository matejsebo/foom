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


#ifndef FOOM_SKETCH_H
#define FOOM_SKETCH_H

#include <vector>

#include <QtGlobal>
#include <QColor>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;


/**
 * The sketch layer: strokes and notes as ordinary objects in a map part of
 * the role sketch, drawn with private helper symbols, so that they show on
 * screen and stay out of print and export.
 *
 * See the design notes, "Sketch layer".
 */

constexpr int sketch_color_count = 8;
constexpr int sketch_width_count = 3;

/** The name a new sketch part gets. */
QString sketchPartName();

/** The index of the sketch part, or -1. */
int sketchPartIndex(const Map& map);

/** The name of a sketch colour, 0 to 7, as it stands in the colour table. */
QString sketchColorName(int color);

/** The colour of a sketch pen on screen, 0 to 7. */
QColor sketchColorRgb(int color);

/** The width of a sketch pen, 0 to 2, in mm. */
qreal sketchWidth(int width);

/**
 * The symbol of a pen. It is made on demand, together with its colour:
 * a private helper line symbol.
 */
const Symbol* sketchSymbol(Map& map, int color, int width);

/**
 * Adds a stroke through the points to the sketch part, which is created
 * when the map has none. One undo step. Returns the stroke, or null for
 * fewer than two points.
 */
Object* addSketchStroke(Map& map, const std::vector<MapCoordF>& points, int color, int width);

/**
 * Adds a note: a marker at the position and the text beside it, as one
 * object group in the sketch part. One undo step. Returns the group.
 */
int addSketchNote(Map& map, const MapCoordF& position, const QString& text, int color);

/**
 * Erases what lies within the radius of the centre in the sketch part: the
 * parts of strokes inside the circle, which may leave a stroke shorter or in
 * pieces, and notes which the circle touches. One undo step.
 * Returns the number of objects erased or cut.
 */
int eraseSketch(Map& map, const MapCoordF& centre, qreal radius);

/**
 * Erases along a trail of positions as with eraseSketch(), in one undo
 * step. The trail is filled in between its positions, so that positions
 * which lie far apart leave no gaps.
 */
int eraseSketchAlong(Map& map, const std::vector<MapCoordF>& trail, qreal radius);

/** A stroke or a note of the sketch part. */
struct SketchItem
{
	enum Kind { Stroke, Note };
	
	Kind kind = Stroke;
	std::vector<Object*> objects;  ///< The stroke; or the text of a note first, then its marker.
	QString label;                 ///< The note's text, or the stroke's length.
	QRectF extent;
};

/** The strokes and notes of the sketch part, in object order. */
std::vector<SketchItem> sketchItems(const Map& map);

/**
 * Turns a stroke or a note into a map object of the symbol, in the current
 * part or, when that is not a part for map objects, the first such part.
 *
 * A stroke becomes a line, or an area, for which it is closed. A note
 * becomes a text, or a point at its marker with the text as the tag `note`.
 * One undo step. Returns the map object, or null with a message.
 */
Object* convertSketchItem(Map& map, const SketchItem& item, const Symbol* target, QString* error);

/**
 * Draws the sketch part to a PNG with a transparent background, and writes
 * a world file beside it.
 */
bool flattenSketch(const Map& map, const QString& path, double dpi, QString* error);


}  // namespace OpenOrienteering

#endif
