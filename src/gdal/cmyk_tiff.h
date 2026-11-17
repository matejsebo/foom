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


#ifndef FOOM_CMYK_TIFF_H
#define FOOM_CMYK_TIFF_H

#include <vector>

#include <QString>

#include "core/print_output.h"

namespace OpenOrienteering {

/**
 * Writes the plates cyan, magenta, yellow and black as one TIFF: four bands
 * of 8 bit, photometric interpretation CMYK, LZW, with the resolution tags.
 *
 * See the design notes, "Print and raster output".
 */
bool writeCmykTiff(const QString& path, const std::vector<RasterPlate>& plates, double dpi, QString* error);


}  // namespace OpenOrienteering

#endif
