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

#ifndef FOOM_GDAL_DXF_COLOURS_H
#define FOOM_GDAL_DXF_COLOURS_H

#include <QByteArray>
#include <QRgb>

namespace OpenOrienteering {


/**
 * The RGB which GDAL's DXF driver gives for an AutoCAD colour index.
 *
 * Read once from a small DXF in GDAL's memory files, so that GDAL's writer,
 * which looks for the nearest index, writes exactly this index.
 * Returns false for an index outside 1..255.
 *
 * See the design notes, "DXF export through a cross-reference table".
 */
bool dxfAciColour(int index, QRgb* rgb);

/** The colour as "#rrggbb" for an OGR style string, or empty for an index outside 1..255. */
QByteArray dxfAciStyleColour(int index);


}  // namespace OpenOrienteering

#endif
