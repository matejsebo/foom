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


#include "align.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/ops/bounds.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/**
 * Returns the inputs without null entries.
 */
std::vector<const Object*> withoutNull(const std::vector<const Object*>& in)
{
	std::vector<const Object*> objects;
	objects.reserve(in.size());
	std::copy_if(begin(in), end(in), std::back_inserter(objects), [](const Object* object) { return object != nullptr; });
	return objects;
}

/**
 * Returns the position of the edge or centre line along its axis.
 */
double edgePosition(const QRectF& extent, AlignParams::Edge edge)
{
	switch (edge)
	{
	case AlignParams::Left:
		return extent.left();
	case AlignParams::HCentre:
		return extent.center().x();
	case AlignParams::Right:
		return extent.right();
	case AlignParams::Top:
		return extent.top();
	case AlignParams::VCentre:
		return extent.center().y();
	case AlignParams::Bottom:
		return extent.bottom();
	}
	Q_UNREACHABLE();
	return 0.0;
}

/**
 * Adds a copy of the object, moved by the offset along one axis, to the
 * result; an object already in place is left alone.
 */
void moveAlongAxis(OpResult& result, const Object* object, double offset, bool horizontal)
{
	if (qFuzzyIsNull(offset))
		return;
	auto output = std::unique_ptr<Object>(object->duplicate());
	output->move(horizontal ? MapCoord(offset, 0.0) : MapCoord(0.0, offset));
	result.out.push_back(std::move(output));
	result.consumed.push_back(object);
}

}  // namespace


OpResult align(const Map& /*map*/, const std::vector<const Object*>& in, const AlignParams& params)
{
	OpResult result;
	auto const objects = withoutNull(in);
	if (objects.size() < 2)
	{
		result.error = QStringLiteral("Select at least two objects.");
		return result;
	}
	auto const horizontal = params.edge <= AlignParams::Right;
	auto const reference = edgePosition(boundingRect({ objects.front() }), params.edge);
	for (auto it = begin(objects) + 1; it != end(objects); ++it)
		moveAlongAxis(result, *it, reference - edgePosition(boundingRect({ *it }), params.edge), horizontal);
	return result;
}

OpResult distribute(const Map& /*map*/, const std::vector<const Object*>& in, const DistributeParams& params)
{
	OpResult result;
	auto const objects = withoutNull(in);
	if (objects.size() < 3)
	{
		result.error = QStringLiteral("Select at least three objects.");
		return result;
	}
	auto const horizontal = params.direction == DistributeParams::Horizontal;
	std::vector<std::pair<double, const Object*>> sorted;
	sorted.reserve(objects.size());
	for (auto const* object : objects)
	{
		auto const centre = boundingRect({ object }).center();
		sorted.emplace_back(horizontal ? centre.x() : centre.y(), object);
	}
	std::stable_sort(begin(sorted), end(sorted), [](const std::pair<double, const Object*>& a, const std::pair<double, const Object*>& b) { return a.first < b.first; });
	auto const first = sorted.front().first;
	auto const step = (sorted.back().first - first) / double(sorted.size() - 1);
	for (std::size_t i = 1; i + 1 < sorted.size(); ++i)
		moveAlongAxis(result, sorted[i].second, first + double(i) * step - sorted[i].first, horizontal);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
