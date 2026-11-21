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


#include "frame.h"

#include <memory>

#include <QCoreApplication>
#include <QRectF>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

OpResult frameAroundMap(const Map& map, const FrameParams& params)
{
	OpResult result;
	auto const tr = [](const char* text) { return QCoreApplication::translate("OpenOrienteering::Frame", text); };
	if (!params.symbol || !(params.symbol->getContainedTypes() & (Symbol::Line | Symbol::Area)))
	{
		result.error = tr("The frame needs a line or an area symbol.");
		return result;
	}
	if (params.margin_mm < 0.0)
	{
		result.error = tr("The margin must not be negative.");
		return result;
	}
	auto const extent = map.calculateExtent(false, false);
	if (!extent.isValid() || extent.isEmpty())
	{
		result.error = tr("The map has no objects to frame.");
		return result;
	}

	auto const m = params.margin_mm;
	auto const box = extent.adjusted(-m, -m, m, m);
	auto* frame = new PathObject(params.symbol);
	frame->addCoordinate(MapCoord(box.topLeft()));
	frame->addCoordinate(MapCoord(box.topRight()));
	frame->addCoordinate(MapCoord(box.bottomRight()));
	frame->addCoordinate(MapCoord(box.bottomLeft()));
	frame->parts().front().setClosed(true, false);
	result.out.emplace_back(frame);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
