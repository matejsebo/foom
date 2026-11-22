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

#ifndef FOOM_TEMPLATES_TEMPLATE_RASTER_EFFECTS_H
#define FOOM_TEMPLATES_TEMPLATE_RASTER_EFFECTS_H

#include <QImage>

namespace OpenOrienteering {

/**
 * Returns the image as a raster template draws it with the dim (0 to 1) and
 * block out: every colour channel moved towards white by the dim, then, for
 * block out, the white taken out into transparency.
 *
 * See the design notes, "Block out, dim and colour
 * infrared". The result is premultiplied ARGB.
 */
QImage templateEffectImage(const QImage& source, double dim, bool block_out);


}  // namespace OpenOrienteering

#endif
