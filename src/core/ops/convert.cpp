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

#include "convert.h"

#include <cmath>
#include <cstddef>
#include <memory>

#include <QString>

#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/// The centroid of a path: of the outer ring's surface for an area, of the outline's length for a line.
MapCoordF centroid(const PathObject* path)
{
	path->update();
	std::vector<MapCoordF> points;
	for (auto const& coord : path->parts().front().path_coords)
	{
		if (points.empty() || points.back() != coord.pos)
			points.push_back(coord.pos);
	}
	if (points.size() == 1)
		return points.front();
	auto const* symbol = path->getSymbol();
	auto const area = symbol && (symbol->getContainedTypes() & Symbol::Area) && points.size() >= 3;
	if (area)
	{
		double sum = 0.0, cx = 0.0, cy = 0.0;
		for (std::size_t k = 0; k < points.size(); ++k)
		{
			auto const& a = points[k];
			auto const& b = points[(k + 1) % points.size()];
			auto const cross = a.x() * b.y() - b.x() * a.y();
			sum += cross;
			cx += (a.x() + b.x()) * cross;
			cy += (a.y() + b.y()) * cross;
		}
		if (std::abs(sum) > 1e-12)
			return MapCoordF(cx / (3 * sum), cy / (3 * sum));
	}
	double length = 0.0, cx = 0.0, cy = 0.0;
	for (std::size_t k = 1; k < points.size(); ++k)
	{
		auto const& a = points[k - 1];
		auto const& b = points[k];
		auto const segment = std::hypot(b.x() - a.x(), b.y() - a.y());
		length += segment;
		cx += (a.x() + b.x()) / 2 * segment;
		cy += (a.y() + b.y()) / 2 * segment;
	}
	return length > 0.0 ? MapCoordF(cx / length, cy / length) : points.front();
}

}  // namespace



OpResult convertToPoints(const Map& /*map*/, const std::vector<const Object*>& in, const ConvertToPointsParams& params)
{
	// the design notes, "Convert objects".
	OpResult result;
	if (!params.symbol)
	{
		result.error = QStringLiteral("Choose a point symbol.");
		return result;
	}
	int skipped = 0;
	for (auto const* object : in)
	{
		MapCoordF position;
		if (object->getType() == Object::Text)
		{
			position = MapCoordF(object->getRawCoordinateVector().front()) + params.text_offset;
		}
		else if (object->getType() == Object::Path && !object->asPath()->parts().empty())
		{
			position = centroid(object->asPath());
		}
		else
		{
			++skipped;
			continue;
		}
		auto point = std::make_unique<PointObject>(params.symbol);
		point->setPosition(position);
		point->setTags(object->tags());
		point->setGroup(object->group());
		point->setIdentity(object->uuid(), object->createdTime(), object->modifiedTime());
		result.out.push_back(std::move(point));
		result.consumed.push_back(object);
	}
	if (skipped > 0)
		result.warning = QStringLiteral("%1 point object(s) were left alone.").arg(skipped);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
