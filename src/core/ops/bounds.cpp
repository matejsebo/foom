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


#include "bounds.h"

#include <algorithm>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"


namespace OpenOrienteering {

namespace ops {

QRectF boundingRect(const std::vector<const Object*>& in)
{
	auto left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
	auto first = true;
	auto include = [&](const QPointF& point) {
		if (first)
		{
			left = right = point.x();
			top = bottom = point.y();
			first = false;
		}
		else
		{
			left = std::min(left, point.x());
			right = std::max(right, point.x());
			top = std::min(top, point.y());
			bottom = std::max(bottom, point.y());
		}
	};
	for (auto const* object : in)
	{
		if (!object)
			continue;
		auto const& coords = object->getRawCoordinateVector();
		if (object->getType() == Object::Text)
		{
			auto const* text = object->asText();
			if (coords.empty())
				continue;
			auto const anchor = QPointF(text->getAnchorCoordF());
			if (text->followsPath())
			{
				for (auto const& coord : coords)
					include(QPointF(coord.x(), coord.y()));
			}
			else if (text->hasSingleAnchor())
			{
				include(anchor);
			}
			else
			{
				auto const half_size = QPointF(text->getBoxSize()) / 2;
				include(anchor - half_size);
				include(anchor + half_size);
			}
		}
		else
		{
			for (auto const& coord : coords)
				include(QPointF(coord));
		}
	}
	return first ? QRectF{} : QRectF(QPointF(left, top), QPointF(right, bottom));
}

MapCoordF boundingCentre(const std::vector<const Object*>& in)
{
	return MapCoordF(boundingRect(in).center());
}


}  // namespace ops

}  // namespace OpenOrienteering
