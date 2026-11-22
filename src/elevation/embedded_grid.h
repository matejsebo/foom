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

#ifndef FOOM_ELEVATION_EMBEDDED_GRID_H
#define FOOM_ELEVATION_EMBEDDED_GRID_H

#include <memory>

#include <QByteArray>
#include <QString>

namespace OpenOrienteering {

class ElevationModel;


/**
 * The grid as the text of the map file's embedded grid for course setting:
 * heights in steps of 0.1 m above the grid's lowest whole metre, 16 bits a
 * cell, differences along the rows, compressed with zlib, then base64.
 *
 * See the design notes, "The embedded grid for course setting".
 */
QByteArray encodeEmbeddedGrid(const ElevationModel& grid);

/**
 * The grid of a text written by encodeEmbeddedGrid().
 *
 * Returns nullptr, with a message in error, for a text which is not one.
 */
std::unique_ptr<ElevationModel> decodeEmbeddedGrid(const QByteArray& text, QString* error = nullptr);


}  // namespace OpenOrienteering

#endif
