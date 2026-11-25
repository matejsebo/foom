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

#ifndef FOOM_OCD_SKETCH_H
#define FOOM_OCD_SKETCH_H

#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QColor>
#include <QPoint>
#include <QString>

namespace OpenOrienteering {


/**
 * The sketch strokes, as OCD 2018 files hold them.
 *
 * The strokes are no objects. Byte 52 of the file header has the place of a
 * sketch header of 22 bytes, whose last four bytes are the place of the
 * first layer. A layer: the place of the next layer, four bytes, the name in
 * 256 UTF-16 characters filled with spaces, two dates, and at byte 546 the
 * place of its first stroke index. An index: the place of the next index,
 * four bytes, the number of strokes, then the place and the size of each.
 * A stroke: red, green, blue and a zero, the width in 1/100 mm, two dates,
 * the box as four coordinates, the number of points, twelve bytes, and the
 * points as the coordinates of an object.
 *
 * See the design notes, "Sketch strokes in OCD files".
 */
struct OcdSketchStroke
{
	/** The name of the stroke's layer. */
	QString layer;
	QColor color;
	/** The width of the pen in 1/100 mm. */
	int width = 0;
	/** The points as the file has them: 1/100 mm shifted left by eight bits, y up. */
	std::vector<QPoint> points;
	/** When the stroke was drawn and changed, in Delphi's days as an object's dates; 0 when not known. */
	double created = 0;
	double modified = 0;
};

/** The place of the sketch header in the file header. */
constexpr int ocd_sketch_header_field = 52;

/**
 * Reads the strokes of all layers. A file without a sketch, or one whose
 * sketch block points outside the file, gives what could be read; `damaged`
 * is set when a place lay outside the file or the lists did not end.
 */
std::vector<OcdSketchStroke> readOcdSketch(const QByteArray& file, bool* damaged = nullptr);

/**
 * Appends a sketch block of one layer with the strokes to the bytes of an
 * OCD file, as the format has it, and names its place in the file header. The
 * layer has the name of the first stroke's layer, `Sketch-1` without one.
 * Does nothing without strokes.
 */
void appendOcdSketch(QByteArray& file, const std::vector<OcdSketchStroke>& strokes);


}  // namespace OpenOrienteering

#endif
