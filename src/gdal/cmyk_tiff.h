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


#ifndef FOOM_CMYK_TIFF_H
#define FOOM_CMYK_TIFF_H

#include <vector>

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>

#include "core/print_output.h"

namespace OpenOrienteering {

class Map;


/**
 * Where the pixels of a plate lie: a GDAL geotransform (the top left corner
 * of the first pixel, then the steps per column and per row) and the
 * coordinate reference system as WKT.
 *
 * See the design notes, "Raster plates and CMYK raster".
 */
struct PlateGeoreference
{
	bool valid = false;
	double transform[6] = { 0, 1, 0, 0, 0, 1 };
	QByteArray wkt;
};

/**
 * The georeference of plates of the area at the resolution, for a map
 * with a geospatial georeferencing; not valid otherwise.
 */
PlateGeoreference plateGeoreference(const Map& map, const QRectF& area, double dpi, double scale_adjustment = 1);

/**
 * Writes the plates cyan, magenta, yellow and black as one TIFF: four bands
 * of 8 bit, photometric interpretation CMYK, LZW, with the resolution tags.
 *
 * See the design notes, "Print and raster output".
 */
bool writeCmykTiff(const QString& path, const std::vector<RasterPlate>& plates, double dpi, QString* error,
                   const PlateGeoreference* georeference = nullptr);


/** The compression of a single plate's TIFF. */
enum class PlateCompression
{
	Lzw,  ///< Any image.
	Fax,  ///< CCITT group 4, for 1-bit images only.
};

/**
 * Writes one plate image as a TIFF through GDAL: an 8-bit grey image
 * (QImage::Format_Grayscale8, ink dark) or a 1-bit image
 * (QImage::Format_Mono, bit 1 for ink, written with 0 as white),
 * with the resolution tags and, when valid, the georeference.
 */
bool writePlateTiff(const QString& path, const QImage& image, double dpi, PlateCompression compression,
                    const PlateGeoreference* georeference, QString* error);


}  // namespace OpenOrienteering

#endif
