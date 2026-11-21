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


#include "sized_shape.h"

#include <cmath>
#include <memory>

#include <QtMath>
#include <QCoreApplication>

#include "core/map.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace ops {

OpResult sizedShape(const Map& map, const SizedShapeParams& params, const MapCoordF& at)
{
	OpResult result;
	auto const tr = [](const char* text) { return QCoreApplication::translate("OpenOrienteering::SizedShape", text); };
	if (!params.symbol || !(params.symbol->getContainedTypes() & (Symbol::Line | Symbol::Area)))
	{
		result.error = tr("The shape needs a line or an area symbol.");
		return result;
	}
	auto const needs_height = params.kind == SizedShapeParams::Ellipse || params.kind == SizedShapeParams::Rectangle;
	if (!(params.width > 0.0) || (needs_height && !(params.height > 0.0)))
	{
		result.error = params.kind == SizedShapeParams::Rectangle && params.width > 0.0
		               ? tr("Height must not be zero.") : tr("Width must not be zero.");
		return result;
	}

	// Sizes in mm on paper.
	auto const factor = params.metres ? 1000.0 / map.getScaleDenominator() : 1.0;
	auto const w = params.width * factor;
	auto const h = params.height * factor;
	// Counter-clockwise as seen on the map, whose y runs downwards.
	auto const angle = qDegreesToRadians(params.angle_deg);
	auto const c = std::cos(angle);
	auto const s = std::sin(angle);
	auto const place = [&at, c, s](double x, double y) {
		return MapCoord(at.x() + x * c + y * s, at.y() - x * s + y * c);
	};

	auto* path = new PathObject(params.symbol);
	switch (params.kind)
	{
	case SizedShapeParams::Line:
		path->addCoordinate(place(0.0, 0.0));
		path->addCoordinate(place(w, 0.0));
		break;
	case SizedShapeParams::Rectangle:
		path->addCoordinate(place(-w / 2, -h / 2));
		path->addCoordinate(place(w / 2, -h / 2));
		path->addCoordinate(place(w / 2, h / 2));
		path->addCoordinate(place(-w / 2, h / 2));
		path->parts().front().setClosed(true, false);
		break;
	case SizedShapeParams::Circle:
	case SizedShapeParams::Ellipse:
	{
		auto const rx = params.kind == SizedShapeParams::Circle ? w : w / 2;
		auto const ry = params.kind == SizedShapeParams::Circle ? w : h / 2;
		auto const kx = BEZIER_KAPPA * rx;
		auto const ky = BEZIER_KAPPA * ry;
		// Four arcs from east through south, west and north (y downwards), as OOM's circle tool draws them.
		auto const curveStart = [&place](double x, double y) {
			auto coord = place(x, y);
			coord.setCurveStart(true);
			return coord;
		};
		path->addCoordinate(curveStart(rx, 0.0));
		path->addCoordinate(place(rx, ky));
		path->addCoordinate(place(kx, ry));
		path->addCoordinate(curveStart(0.0, ry));
		path->addCoordinate(place(-kx, ry));
		path->addCoordinate(place(-rx, ky));
		path->addCoordinate(curveStart(-rx, 0.0));
		path->addCoordinate(place(-rx, -ky));
		path->addCoordinate(place(-kx, -ry));
		path->addCoordinate(curveStart(0.0, -ry));
		path->addCoordinate(place(kx, -ry));
		path->addCoordinate(place(rx, -ky));
		path->addCoordinate(place(rx, 0.0));
		path->parts().front().setClosed(true, true);
		break;
	}
	}
	result.out.emplace_back(path);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
