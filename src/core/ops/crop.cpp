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


#include "crop.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <QString>

#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/boolean_tool.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/// The bounding box of the coordinates, which encloses curves as well.
QRectF coordinateBounds(const Object& object)
{
	auto const& coords = object.getRawCoordinateVector();
	if (coords.empty())
		return {};
	// Not rectInclude(): it starts anew while the box has no width or height.
	auto const first = QPointF(coords.front());
	auto left = first.x(), right = first.x(), top = first.y(), bottom = first.y();
	for (auto const& coord : coords)
	{
		auto const point = QPointF(coord);
		left = std::min(left, point.x());
		right = std::max(right, point.x());
		top = std::min(top, point.y());
		bottom = std::max(bottom, point.y());
	}
	return QRectF(QPointF(left, top), QPointF(right, bottom));
}

/// Whether the boxes overlap or touch; unlike QRectF::intersects(), true for boxes of no height or width.
bool overlaps(const QRectF& a, const QRectF& b)
{
	return a.left() <= b.right() && b.left() <= a.right() && a.top() <= b.bottom() && b.top() <= a.bottom();
}

bool isArea(const Object& object)
{
	return object.getSymbol() && (object.getSymbol()->getContainedTypes() & Symbol::Area);
}

/// The coordinates of one part, as a path of its own.
MapCoordVector partCoords(const PathObject& path, const PathPart& part)
{
	auto const& coords = path.getRawCoordinateVector();
	MapCoordVector out(coords.begin() + part.first_index, coords.begin() + part.last_index + 1);
	out.back().setHolePoint(false);
	return out;
}

}  // namespace


OpResult crop(const Map& /*map*/, const std::vector<const Object*>& in, const CropParams& params)
{
	// the design notes, "Crop".
	OpResult result;
	
	// The shapes, closed, with a symbol of their own so that they can be updated.
	// AreaRenderable reads the colour's priority, so the symbol needs one.
	MapColor shape_color;
	AreaSymbol shape_symbol;
	shape_symbol.setColor(&shape_color);
	std::vector<std::unique_ptr<PathObject>> shapes;
	for (auto const* shape : params.shapes)
	{
		if (!shape || shape->parts().empty())
			continue;
		auto copy = std::unique_ptr<PathObject>(new PathObject(&shape_symbol, shape->getRawCoordinateVector()));
		copy->closeAllParts();
		copy->update();
		shapes.push_back(std::move(copy));
	}
	if (shapes.empty() && params.rect.isValid() && params.rect.width() > 0 && params.rect.height() > 0)
	{
		auto const& r = params.rect;
		auto copy = std::unique_ptr<PathObject>(new PathObject(&shape_symbol, MapCoordVector{
		    MapCoord(r.left(), r.top()), MapCoord(r.right(), r.top()),
		    MapCoord(r.right(), r.bottom()), MapCoord(r.left(), r.bottom()) }));
		copy->closeAllParts();
		copy->update();
		shapes.push_back(std::move(copy));
	}
	if (shapes.empty())
	{
		result.error = QStringLiteral("Select a line or area as the cropping shape.");
		return result;
	}
	
	QRectF region_bounds;
	BooleanTool::PathObjects shape_list;
	for (auto const& shape : shapes)
	{
		auto const bounds = coordinateBounds(*shape);
		region_bounds = shape_list.empty() ? bounds : QRectF(QPointF(std::min(region_bounds.left(), bounds.left()), std::min(region_bounds.top(), bounds.top())),
		                                                     QPointF(std::max(region_bounds.right(), bounds.right()), std::max(region_bounds.bottom(), bounds.bottom())));
		shape_list.push_back(shape.get());
	}
	
	// The union of the shapes as one path, for lines and points: its parts
	// are outlines and holes, so that the even-odd rule tells inside.
	std::unique_ptr<PathObject> region;
	{
		BooleanTool::PathObjects pieces;
		if (shape_list.size() == 1 || !BooleanTool(BooleanTool::Union, nullptr).executeForObjects(shape_list.front(), shape_list, pieces) || pieces.empty())
		{
			for (auto* piece : pieces)
				delete piece;
			pieces.clear();
			region.reset(shape_list.front()->duplicate());
		}
		else
		{
			MapCoordVector coords;
			for (auto* piece : pieces)
			{
				for (auto const& part : piece->parts())
				{
					if (!coords.empty())
						coords.back().setHolePoint(true);
					auto const part_coords = partCoords(*piece, part);
					coords.insert(coords.end(), part_coords.begin(), part_coords.end());
				}
				delete piece;
			}
			region.reset(new PathObject(&shape_symbol, coords));
		}
		region->update();
	}
	
	auto const is_shape = [&params](const Object* object) {
		return std::find(params.shapes.begin(), params.shapes.end(), object) != params.shapes.end();
	};
	auto const keep_inside = !params.hole;
	BooleanTool const clip_tool(params.hole ? BooleanTool::Difference : BooleanTool::Intersection, nullptr);
	
	for (auto const* object : in)
	{
		if (!object || is_shape(object))
			continue;
		
		if (object->getType() != Object::Path)
		{
			auto const& coords = object->getRawCoordinateVector();
			if (coords.empty())
				continue;
			if (region->isPointInsideArea(MapCoordF(coords.front())) != keep_inside)
				result.consumed.push_back(object);
			continue;
		}
		
		auto const* path = object->asPath();
		if (path->parts().empty())
			continue;
		if (!overlaps(coordinateBounds(*path), region_bounds))
		{
			if (keep_inside)
				result.consumed.push_back(object);
			continue;
		}
		
		path->update();
		BooleanTool::PathObjects pieces;
		if (isArea(*path))
		{
			BooleanTool::PathObjects inputs = { const_cast<PathObject*>(path) };
			inputs.insert(inputs.end(), shape_list.begin(), shape_list.end());
			if (!clip_tool.executeForObjects(path, inputs, pieces))
				continue;  // Clipper failed: leave the object as it is
		}
		else
		{
			for (auto const& part : path->parts())
			{
				std::unique_ptr<PathObject> single(path->duplicate());
				single->clearCoordinates();
				single->setSymbol(path->getSymbol(), true);
				for (auto const& coord : partCoords(*path, part))
					single->addCoordinate(coord);
				single->update();
				clip_tool.executeForLine(region.get(), single.get(), pieces);
			}
		}
		result.consumed.push_back(object);
		for (auto* piece : pieces)
		{
			piece->update();  // so that lengths and areas can be read before the map takes it
			result.out.push_back(std::unique_ptr<Object>(piece));
		}
	}
	
	if (params.delete_shapes)
	{
		for (auto const* shape : params.shapes)
		{
			if (shape && std::find(result.consumed.begin(), result.consumed.end(), shape) == result.consumed.end())
				result.consumed.push_back(shape);
		}
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
