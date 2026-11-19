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


#include "extent_printing.h"

#ifdef QT_PRINTSUPPORT_LIB

#include <vector>

#include <QPageSize>
#include <QRectF>
#include <QSizeF>

#include "core/extents.h"
#include "core/map.h"
#include "core/map_printer.h"


namespace OpenOrienteering {

namespace {

/** The rectangle less the margin, which is in mm on paper. */
QRectF printArea(const Extent& extent, unsigned int print_scale, unsigned int map_scale)
{
	auto const inset = extent.margin_mm * print_scale / qMax(1u, map_scale);  // in map mm
	auto const area = extent.rect_mm.adjusted(inset, inset, -inset, -inset);
	return (area.width() > 0 && area.height() > 0) ? area : extent.rect_mm;
}

}  // namespace


int applyExtentToPrinter(MapPrinter& printer, const Map& map, const Extent& extent)
{
	if (extent.scale > 0)
		printer.setScale(extent.scale);
	auto const print_scale = printer.getOptions().scale;
	auto const map_scale = map.getScaleDenominator();
	
	auto const paper = extent.paper.isEmpty() ? QPageSize::Custom : pageSizeFromKey(extent.paper);
	if (paper != QPageSize::Custom)
	{
		printer.setPageSize(paper);
		printer.setPageOrientation(extent.rect_mm.width() > extent.rect_mm.height() ? MapPrinterPageFormat::Landscape : MapPrinterPageFormat::Portrait);
	}
	else
	{
		printer.setCustomPageSize(extent.rect_mm.size() * (qreal(map_scale) / print_scale));
	}
	printer.setPrintArea(printArea(extent, print_scale, map_scale));
	
	std::vector<MapPrinter::BackSide> backs;
	for (auto const& other : map.extents())
	{
		if (other.back_of == extent.name && other.name != extent.name)
			backs.push_back({ printArea(other, other.scale > 0 ? other.scale : print_scale, map_scale), other.scale });
	}
	auto const sides = 1 + int(backs.size());
	printer.setBackSides(std::move(backs));
	return sides;
}


}  // namespace OpenOrienteering

#endif
