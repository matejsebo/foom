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


#include "stairway.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QCoreApplication>

#include "core/objects/object.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

OpResult stairway(const Map& /*map*/, const StairwayGeometry& geometry, const StairwayParams& params)
{
	OpResult result;
	auto const tr = [](const char* text) { return QCoreApplication::translate("OpenOrienteering::Stairway", text); };
	if (!(params.tread_mm > 0.0))
	{
		result.error = tr("The tread depth must be positive.");
		return result;
	}
	if (!params.symbol || !(params.symbol->getContainedTypes() & Symbol::Line))
	{
		result.error = tr("A stairway needs a line symbol.");
		return result;
	}
	auto const tread = geometry.second - geometry.first;
	auto const run = geometry.last - geometry.first;
	if (tread.length() < 1e-6 || run.length() < 1e-6)
	{
		result.error = tr("The footprint of the stairway has no size.");
		return result;
	}

	auto const steps = std::max(1, int(std::lround(run.length() / params.tread_mm)));
	for (int k = 0; k <= steps; ++k)
	{
		auto const offset = run * (double(k) / steps);
		auto* line = new PathObject(params.symbol);
		line->addCoordinate(MapCoord(geometry.first + offset));
		line->addCoordinate(MapCoord(geometry.second + offset));
		line->setGroup(params.group);
		result.out.emplace_back(line);
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
