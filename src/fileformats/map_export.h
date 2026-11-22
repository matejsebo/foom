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

#ifndef FOOM_MAP_EXPORT_H
#define FOOM_MAP_EXPORT_H

#include <memory>
#include <vector>

#include <QRectF>
#include <QString>

namespace OpenOrienteering {

class Map;
class Object;

namespace ops { struct CropParams; }


/**
 * Writes the area of the map (mm, map coordinates) as an SVG file at the
 * map's scale. Returns false and sets the error on failure.
 *
 * See the design notes, "SVG export".
 */
bool exportSvg(Map& map, const QString& path, const QRectF& area, QString* error = nullptr);


/**
 * Returns a new map with copies of the objects, their symbols and colours,
 * and the scale and georeferencing of the map. With crop, the copies are
 * cropped as ops::crop() does.
 *
 * See the design notes, "Part of the map, and objects by symbol or selection".
 */
std::unique_ptr<Map> extractMap(const Map& map, const std::vector<const Object*>& objects, const ops::CropParams* crop = nullptr);


/**
 * Writes the map in the format of the path's extension. Returns false and
 * sets the error on failure.
 */
bool saveMapFile(const Map& map, const QString& path, QString* error = nullptr);


}  // namespace OpenOrienteering

#endif
