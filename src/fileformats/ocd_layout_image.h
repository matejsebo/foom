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


#ifndef FOOM_OCD_LAYOUT_IMAGE_H
#define FOOM_OCD_LAYOUT_IMAGE_H

#include <memory>

#include <QString>

namespace OpenOrienteering {

class ImageObject;
class MapCoord;


/**
 * OCD parameter strings of type 27, the embedded layout images of OCD 2018.
 *
 * The string holds the file name, then the fields F (base64 of the image
 * file), r, s, u and v (the size of a pixel in mm), x and y (the centre of the
 * image in mm, y up).
 *
 * See the design notes, "OCD embedded images".
 */

/** The OCD parameter string type of embedded layout images. */
constexpr int ocd_layout_image_string_type = 27;

/**
 * Creates the image object for a string of type 27.
 *
 * Returns nullptr when the string holds no readable image.
 * A message about what was not imported is put in warning.
 */
std::unique_ptr<ImageObject> ocdLayoutImageFromString(const QString& param_string, QString* warning);

/**
 * Returns the string of type 27 for an embedded image.
 *
 * Returns an empty string for an image which cannot be written.
 * A message about what was not written is put in warning.
 */
QString ocdLayoutImageToString(const ImageObject& image, const MapCoord& area_offset, QString* warning);


}  // namespace OpenOrienteering

#endif
