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


#include "mirror.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/bounds.h"


namespace OpenOrienteering {

namespace ops {

OpResult mirror(const Map& /*map*/, const std::vector<const Object*>& in, const MirrorParams& params)
{
	OpResult result;
	if (in.empty())
	{
		result.error = QStringLiteral("No objects to mirror.");
		return result;
	}
	
	auto const centre = boundingCentre(in);
	auto const angle = qDegreesToRadians(params.axis_angle);
	auto const ux = std::cos(angle);
	auto const uy = -std::sin(angle);  // map y grows downwards
	auto const reflect = [&centre, ux, uy](MapCoord coord) {
		auto const dx = coord.x() - centre.x();
		auto const dy = coord.y() - centre.y();
		auto const dot = dx * ux + dy * uy;
		coord.setX(centre.x() + 2 * dot * ux - dx);
		coord.setY(centre.y() + 2 * dot * uy - dy);
		return coord;
	};
	
	for (auto const* object : in)
	{
		if (!object)
			continue;
		auto output = std::unique_ptr<Object>(object->duplicate());
		switch (object->getType())
		{
		case Object::Path:
			{
				auto* path = output->asPath();
				for (MapCoordVector::size_type i = 0; i < path->getCoordinateCount(); ++i)
				{
					// setCoordinate() keeps the close point of a closed part equal to its
					// first point, so the close point is already done when it comes round.
					if (!path->getCoordinate(i).isClosePoint())
						path->setCoordinate(i, reflect(path->getCoordinate(i)));
				}
				path->reverse();
				path->setPatternOrigin(reflect(path->getPatternOrigin()));
				path->setPatternRotation(2 * angle - path->getPatternRotation());
			}
			break;
		case Object::Point:
			{
				auto* point = output->asPoint();
				point->setPosition(reflect(point->getCoord()));
				point->setRotation(2 * angle - point->getRotation());
			}
			break;
		case Object::Text:
			{
				auto* text = output->asText();
				text->setAnchorPosition(reflect(MapCoord(text->getAnchorCoordF())));
				text->setRotation(2 * angle - text->getRotation());
			}
			break;
		default:
			continue;
		}
		result.out.push_back(std::move(output));
		if (!params.duplicate)
			result.consumed.push_back(object);
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
