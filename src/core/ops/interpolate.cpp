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

#include "interpolate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/path_coord.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/// Resampling: at most this far apart (mm), at least this many points.
constexpr double resample_spacing = 0.2;
constexpr std::size_t resample_minimum = 64;
/// Douglas-Peucker tolerance of the result (mm).
constexpr double simplify_tolerance = 0.01;

double distance(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// The flattened outline of the path's first part, without repeated points.
std::vector<MapCoordF> outline(const PathObject* path)
{
	path->update();
	std::vector<MapCoordF> points;
	for (auto const& coord : path->parts().front().path_coords)
	{
		if (points.empty() || points.back() != coord.pos)
			points.push_back(coord.pos);
	}
	return points;
}

double length(const std::vector<MapCoordF>& points)
{
	double total = 0.0;
	for (std::size_t k = 1; k < points.size(); ++k)
		total += distance(points[k - 1], points[k]);
	return total;
}

/// count points at equal arc length along the polyline, from its start to its end.
std::vector<MapCoordF> resample(const std::vector<MapCoordF>& points, std::size_t count)
{
	std::vector<MapCoordF> out;
	auto const total = length(points);
	std::size_t k = 1;
	double travelled = 0.0;
	for (std::size_t i = 0; i < count; ++i)
	{
		auto const target = count > 1 ? total * double(i) / double(count - 1) : 0.0;
		while (k + 1 < points.size() && travelled + distance(points[k - 1], points[k]) < target)
		{
			travelled += distance(points[k - 1], points[k]);
			++k;
		}
		auto const segment = distance(points[k - 1], points[k]);
		auto const t = segment > 0.0 ? std::min(1.0, std::max(0.0, (target - travelled) / segment)) : 0.0;
		out.push_back(points[k - 1] + (points[k] - points[k - 1]) * t);
	}
	return out;
}

double signedArea(const std::vector<MapCoordF>& ring)
{
	double sum = 0.0;
	for (std::size_t k = 0; k < ring.size(); ++k)
	{
		auto const& a = ring[k];
		auto const& b = ring[(k + 1) % ring.size()];
		sum += a.x() * b.y() - b.x() * a.y();
	}
	return sum / 2;
}

void simplify(const std::vector<MapCoordF>& points, std::size_t first, std::size_t last, std::vector<bool>& keep)
{
	if (last <= first + 1)
		return;
	auto const& a = points[first];
	auto const& b = points[last];
	auto const base = distance(a, b);
	double worst = -1.0;
	std::size_t index = first;
	for (auto k = first + 1; k < last; ++k)
	{
		auto const& p = points[k];
		auto const d = base > 0.0 ? std::abs((b.x() - a.x()) * (a.y() - p.y()) - (a.x() - p.x()) * (b.y() - a.y())) / base : distance(a, p);
		if (d > worst)
		{
			worst = d;
			index = k;
		}
	}
	if (worst > simplify_tolerance)
	{
		keep[index] = true;
		simplify(points, first, index, keep);
		simplify(points, index, last, keep);
	}
}

}  // namespace



OpResult interpolate(const Map& /*map*/, const std::vector<const Object*>& in, const InterpolateParams& params)
{
	// the design notes, "Interpolate".
	OpResult result;
	if (in.size() != 2 || in[0]->getType() != Object::Path || in[1]->getType() != Object::Path
	    || in[0]->asPath()->parts().empty() || in[1]->asPath()->parts().empty())
	{
		result.error = QStringLiteral("Interpolate needs exactly two lines or areas.");
		return result;
	}
	if (params.count < 1)
	{
		result.error = QStringLiteral("The count must be at least 1.");
		return result;
	}
	auto const* path_a = in[0]->asPath();
	auto const* path_b = in[1]->asPath();
	auto const closed = path_a->parts().front().isClosed();
	if (closed != path_b->parts().front().isClosed())
	{
		result.error = QStringLiteral("Both objects must be open, or both closed.");
		return result;
	}
	auto a = outline(path_a);
	auto b = outline(path_b);
	if (closed)
	{
		if (a.size() > 1 && a.front() == a.back())
			a.pop_back();
		if (b.size() > 1 && b.front() == b.back())
			b.pop_back();
	}
	if (a.size() < 2 || b.size() < 2)
	{
		result.error = QStringLiteral("Interpolate needs objects of at least two points.");
		return result;
	}
	
	if (closed)
	{
		// The same way round, from B's point nearest A's start.
		if ((signedArea(a) < 0.0) != (signedArea(b) < 0.0))
			std::reverse(b.begin(), b.end());
		auto const nearest = std::min_element(b.begin(), b.end(), [&a](const MapCoordF& p, const MapCoordF& q) {
			return distance(p, a.front()) < distance(q, a.front());
		});
		std::rotate(b.begin(), nearest, b.end());
		a.push_back(a.front());
		b.push_back(b.front());
	}
	else if (distance(a.front(), b.back()) + distance(a.back(), b.front()) < distance(a.front(), b.front()) + distance(a.back(), b.back()))
	{
		std::reverse(b.begin(), b.end());
	}
	
	auto const longest = std::max(length(a), length(b));
	auto const count = std::max(resample_minimum, std::size_t(std::ceil(longest / resample_spacing)) + 1);
	auto const ra = resample(a, count);
	auto const rb = resample(b, count);
	auto const* symbol = params.symbol ? params.symbol : path_a->getSymbol();
	for (int k = 1; k <= params.count; ++k)
	{
		auto const t = double(k) / double(params.count + 1);
		std::vector<MapCoordF> blend;
		for (std::size_t i = 0; i < count; ++i)
			blend.push_back(ra[i] * (1.0 - t) + rb[i] * t);
		std::vector<bool> keep(blend.size(), false);
		keep.front() = keep.back() = true;
		simplify(blend, 0, blend.size() - 1, keep);
		MapCoordVector coords;
		for (std::size_t i = 0; i < blend.size(); ++i)
		{
			if (keep[i] && !(closed && i + 1 == blend.size()))
				coords.push_back(MapCoord(blend[i]));
		}
		auto object = std::make_unique<PathObject>(symbol, coords);
		if (closed)
			object->closeAllParts();
		result.out.push_back(std::move(object));
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
