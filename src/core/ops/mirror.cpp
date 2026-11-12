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

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * Returns the centre of the bounding box of the objects' coordinates.
 * 
 * For text objects, only the anchor counts (the second coordinate of a
 * boxed text holds the box size).
 */
MapCoordF boundingCentre(const std::vector<const Object*>& in)
{
	auto left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
	auto first = true;
	auto include = [&](const MapCoord& coord) {
		if (first)
		{
			left = right = coord.x();
			top = bottom = coord.y();
			first = false;
		}
		else
		{
			left = std::min(left, coord.x());
			right = std::max(right, coord.x());
			top = std::min(top, coord.y());
			bottom = std::max(bottom, coord.y());
		}
	};
	for (auto const* object : in)
	{
		if (!object)
			continue;
		auto const& coords = object->getRawCoordinateVector();
		if (object->getType() == Object::Text)
		{
			if (!coords.empty())
				include(coords.front());
		}
		else
		{
			for (auto const& coord : coords)
				include(coord);
		}
	}
	return MapCoordF((left + right) / 2, (top + bottom) / 2);
}
	
}  // namespace
	
	
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
					path->setCoordinate(i, reflect(path->getCoordinate(i)));
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
