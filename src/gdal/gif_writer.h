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

#ifndef FOOM_GDAL_GIF_WRITER_H
#define FOOM_GDAL_GIF_WRITER_H

class QImage;
class QString;

namespace OpenOrienteering {


/**
 * Writes an indexed or grey 8-bit image as GIF through GDAL's GIF driver.
 * Other formats are converted to indexed first. Returns false with a message.
 *
 * See the design notes, "Print and raster output", fourth change.
 */
bool writeGif(const QString& path, const QImage& image, QString* error);


}  // namespace OpenOrienteering

#endif
