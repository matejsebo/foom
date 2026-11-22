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

#include "template_raster_effects.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QRgb>

namespace OpenOrienteering {

QImage templateEffectImage(const QImage& source, double dim, bool block_out)
{
	// the design notes, "Block out, dim and colour infrared".
	dim = std::max(0.0, std::min(1.0, dim));
	auto result = source.convertToFormat(QImage::Format_ARGB32);
	auto const lighten = [dim](int c) { return int(std::lround(c + (255 - c) * dim)); };
	for (int y = 0; y < result.height(); ++y)
	{
		auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (int x = 0; x < result.width(); ++x)
		{
			auto const pixel = line[x];
			auto r = lighten(qRed(pixel));
			auto g = lighten(qGreen(pixel));
			auto b = lighten(qBlue(pixel));
			auto a = qAlpha(pixel);
			if (block_out)
			{
				auto const m = std::min({ r, g, b });
				if (m >= 255)
				{
					r = g = b = a = 0;
				}
				else
				{
					a = int(std::lround(a * (255 - m) / 255.0));
					auto const take_out = [m](int c) { return int(std::lround(255.0 * (c - m) / (255 - m))); };
					r = take_out(r);
					g = take_out(g);
					b = take_out(b);
				}
			}
			line[x] = qRgba(r, g, b, a);
		}
	}
	return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}


}  // namespace OpenOrienteering
