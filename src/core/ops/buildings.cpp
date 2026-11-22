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


#include "buildings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


namespace OpenOrienteering {

namespace ops {

namespace {

constexpr double flatten_tolerance = 0.05;
constexpr double unchanged_tolerance = 1e-6;

using Ring = std::vector<QPointF>;

/** A closed path's first part as a ring without the close point, plus the dash flags. */
struct Building
{
	const PathObject* path = nullptr;
	Ring ring;
	std::vector<bool> dash;
	MapCoordVector rest;  ///< The coordinates of the other parts, copied unchanged.
	
	bool load(const Object* object);
};

bool Building::load(const Object* object)
{
	if (!object || object->getType() != Object::Path || !object->getSymbol())
		return false;
	if (!(object->getSymbol()->getContainedTypes() & (Symbol::Line | Symbol::Area)))
		return false;
	path = object->asPath();
	if (path->parts().empty() || !path->parts().front().isClosed())
		return false;
	auto const& part = path->parts().front();
	auto const& coords = path->getRawCoordinateVector();
	auto const flat = flattenCoords(MapCoordVector(coords.begin() + part.first_index, coords.begin() + part.last_index + 1), flatten_tolerance);
	ring.clear();
	dash.clear();
	for (std::size_t i = 0; i + 1 < flat.size(); ++i)  // the close point is left out
	{
		if (!ring.empty() && std::abs(ring.back().x() - flat[i].x()) < unchanged_tolerance && std::abs(ring.back().y() - flat[i].y()) < unchanged_tolerance)
			continue;
		ring.emplace_back(flat[i].x(), flat[i].y());
		dash.push_back(flat[i].isDashPoint());
	}
	rest.assign(coords.begin() + part.last_index + 1, coords.end());
	return ring.size() >= 3;
}

double ringArea(const Ring& ring)
{
	auto area = 0.0;
	for (std::size_t i = 0; i < ring.size(); ++i)
	{
		auto const& a = ring[i];
		auto const& b = ring[(i + 1) % ring.size()];
		area += a.x() * b.y() - b.x() * a.y();
	}
	return std::abs(area) / 2;
}

QPointF ringCentroid(const Ring& ring)
{
	auto cx = 0.0;
	auto cy = 0.0;
	auto twice_area = 0.0;
	for (std::size_t i = 0; i < ring.size(); ++i)
	{
		auto const& a = ring[i];
		auto const& b = ring[(i + 1) % ring.size()];
		auto const cross = a.x() * b.y() - b.x() * a.y();
		twice_area += cross;
		cx += (a.x() + b.x()) * cross;
		cy += (a.y() + b.y()) * cross;
	}
	if (std::abs(twice_area) < 1e-12)
	{
		QPointF mean;
		for (auto const& p : ring)
			mean += p;
		return mean / double(ring.size());
	}
	return QPointF(cx / (3 * twice_area), cy / (3 * twice_area));
}

double cross(const QPointF& a, const QPointF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

bool segmentsCross(const QPointF& a1, const QPointF& a2, const QPointF& b1, const QPointF& b2)
{
	auto const d1 = cross(b2 - b1, a1 - b1);
	auto const d2 = cross(b2 - b1, a2 - b1);
	auto const d3 = cross(a2 - a1, b1 - a1);
	auto const d4 = cross(a2 - a1, b2 - a1);
	return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

bool selfIntersects(const Ring& ring)
{
	auto const n = ring.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		for (std::size_t j = i + 2; j < n; ++j)
		{
			if (i == 0 && j == n - 1)
				continue;  // adjacent round the ring
			if (segmentsCross(ring[i], ring[(i + 1) % n], ring[j], ring[(j + 1) % n]))
				return true;
		}
	}
	return false;
}

QPointF rotated(const QPointF& p, double angle_rad)
{
	auto const c = std::cos(angle_rad);
	auto const s = std::sin(angle_rad);
	return QPointF(c * p.x() - s * p.y(), s * p.x() + c * p.y());
}

double length(const QPointF& v)
{
	return std::hypot(v.x(), v.y());
}

/** The principal direction of the ring's edges modulo 90 degrees, in radians. */
double principalDirection(const Ring& ring)
{
	constexpr int bins = 90;
	double histogram[bins] = {};
	auto const n = ring.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		auto const edge = ring[(i + 1) % n] - ring[i];
		auto const len = length(edge);
		if (!(len > 0.0))
			continue;
		auto angle = std::fmod(qRadiansToDegrees(std::atan2(edge.y(), edge.x())), 90.0);
		if (angle < 0.0)
			angle += 90.0;
		histogram[int(angle) % bins] += len;
	}
	auto best = 0;
	auto best_weight = -1.0;
	for (int b = 0; b < bins; ++b)
	{
		auto weight = 0.0;
		for (int k = -2; k <= 2; ++k)
			weight += histogram[(b + k + bins) % bins];
		if (weight > best_weight)
		{
			best_weight = weight;
			best = b;
		}
	}
	// Refine: the length-weighted mean angle of the edges in the peak bin and its neighbours.
	auto sum_sin = 0.0;
	auto sum_cos = 0.0;
	for (std::size_t i = 0; i < n; ++i)
	{
		auto const edge = ring[(i + 1) % n] - ring[i];
		auto const len = length(edge);
		if (!(len > 0.0))
			continue;
		auto angle = std::fmod(qRadiansToDegrees(std::atan2(edge.y(), edge.x())), 90.0);
		if (angle < 0.0)
			angle += 90.0;
		auto const bin = int(angle) % bins;
		auto const distance = std::min(std::abs(bin - best), bins - std::abs(bin - best));
		if (distance > 2)
			continue;
		// Angles modulo 90 live on a circle of 90 degrees: use four times the angle.
		sum_sin += len * std::sin(qDegreesToRadians(4.0 * angle));
		sum_cos += len * std::cos(qDegreesToRadians(4.0 * angle));
	}
	if (sum_sin == 0.0 && sum_cos == 0.0)
		return 0.0;
	return std::atan2(sum_sin, sum_cos) / 4.0;
}

enum class Orientation { Horizontal, Vertical, Free };

struct Edge
{
	Orientation orientation;
	double offset;      ///< y for horizontal, x for vertical edges (rotated frame)
	QPointF start;      ///< The original end points in the rotated frame.
	QPointF end;
};

/**
 * Squares a ring: see the design notes, "Make rectangular".
 * Returns false with a reason when the ring is rejected.
 */
bool rectangularize(Ring& ring, double tolerance_deg, const double* reference_angle, bool place_at_distance, double distance, const QPointF& reference_point, QString& reason)
{
	auto const theta = reference_angle ? *reference_angle : principalDirection(ring);
	Ring r;
	r.reserve(ring.size());
	for (auto const& p : ring)
		r.push_back(rotated(p, -theta));
	
	// Classify the edges.
	auto const n = r.size();
	std::vector<Edge> edges;
	edges.reserve(n);
	auto any_snapped = false;
	for (std::size_t i = 0; i < n; ++i)
	{
		auto const& a = r[i];
		auto const& b = r[(i + 1) % n];
		auto angle = std::fmod(qRadiansToDegrees(std::atan2(b.y() - a.y(), b.x() - a.x())), 180.0);
		if (angle < 0.0)
			angle += 180.0;
		Edge edge;
		edge.start = a;
		edge.end = b;
		auto const len = length(b - a);
		if (angle <= tolerance_deg || angle >= 180.0 - tolerance_deg)
		{
			edge.orientation = Orientation::Horizontal;
			edge.offset = (a.y() + b.y()) / 2;
		}
		else if (std::abs(angle - 90.0) <= tolerance_deg)
		{
			edge.orientation = Orientation::Vertical;
			edge.offset = (a.x() + b.x()) / 2;
		}
		else
		{
			edge.orientation = Orientation::Free;
			edge.offset = 0.0;
		}
		any_snapped = any_snapped || edge.orientation != Orientation::Free;
		Q_UNUSED(len);
		edges.push_back(edge);
	}
	if (!any_snapped)
		return true;  // nothing to do
	
	// Start the cyclic list at an orientation change, then merge the runs.
	std::size_t start = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		auto const& previous = edges[(i + n - 1) % n];
		if (edges[i].orientation == Orientation::Free || previous.orientation != edges[i].orientation)
		{
			start = i;
			break;
		}
	}
	std::vector<Edge> merged;
	for (std::size_t k = 0; k < n; ++k)
	{
		auto const& edge = edges[(start + k) % n];
		if (!merged.empty() && edge.orientation != Orientation::Free && merged.back().orientation == edge.orientation)
		{
			auto& run = merged.back();
			auto const run_len = length(run.end - run.start);
			auto const len = length(edge.end - edge.start);
			if (run_len + len > 0.0)
				run.offset = (run.offset * run_len + edge.offset * len) / (run_len + len);
			run.end = edge.end;
			continue;
		}
		merged.push_back(edge);
	}
	if (merged.size() < 3)
	{
		reason = QStringLiteral("fewer than three edges remain");
		return false;
	}
	
	// Rebuild the vertices between consecutive edges.
	Ring out;
	auto const m = merged.size();
	for (std::size_t k = 0; k < m; ++k)
	{
		auto const& before = merged[(k + m - 1) % m];
		auto const& after = merged[k];
		auto const original = after.start;  // the original vertex between them
		QPointF vertex = original;
		if (before.orientation == Orientation::Horizontal && after.orientation == Orientation::Vertical)
			vertex = QPointF(after.offset, before.offset);
		else if (before.orientation == Orientation::Vertical && after.orientation == Orientation::Horizontal)
			vertex = QPointF(before.offset, after.offset);
		else if (before.orientation == Orientation::Horizontal)
			vertex = QPointF(original.x(), before.offset);
		else if (before.orientation == Orientation::Vertical)
			vertex = QPointF(before.offset, original.y());
		else if (after.orientation == Orientation::Horizontal)
			vertex = QPointF(original.x(), after.offset);
		else if (after.orientation == Orientation::Vertical)
			vertex = QPointF(after.offset, original.y());
		out.push_back(vertex);
	}
	
	if (place_at_distance)
	{
		auto const reference_y = rotated(reference_point, -theta).y();
		auto best = std::numeric_limits<double>::max();
		auto best_signed = 0.0;
		for (auto const& edge : merged)
		{
			if (edge.orientation != Orientation::Horizontal)
				continue;
			auto const d = edge.offset - reference_y;
			if (std::abs(d) < best)
			{
				best = std::abs(d);
				best_signed = d;
			}
		}
		if (best < std::numeric_limits<double>::max())
		{
			auto const shift = (best_signed < 0 ? -distance : distance) - best_signed;
			for (auto& p : out)
				p.setY(p.y() + shift);
		}
	}
	
	for (auto& p : out)
		p = rotated(p, theta);
	
	if (selfIntersects(out))
	{
		reason = QStringLiteral("the result would intersect itself");
		return false;
	}
	auto const area_in = ringArea(ring);
	auto const area_out = ringArea(out);
	if (area_in > 0.0 && std::abs(area_out - area_in) > 0.2 * area_in)
	{
		reason = QStringLiteral("the area would change by more than 20 percent");
		return false;
	}
	ring = std::move(out);
	return true;
}

bool sameRing(const Ring& a, const Ring& b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (std::abs(a[i].x() - b[i].x()) > unchanged_tolerance || std::abs(a[i].y() - b[i].y()) > unchanged_tolerance)
			return false;
	}
	return true;
}

/** A new path from the ring (closed) and the rest of the parts, with the symbol, tags and pattern of the original; dash flags per vertex when given. */
std::unique_ptr<PathObject> pathFromRing(const PathObject& original, const Ring& ring, const MapCoordVector& rest, const std::vector<bool>* dash = nullptr)
{
	auto output = std::unique_ptr<PathObject>(new PathObject(original.getSymbol()));
	for (std::size_t i = 0; i < ring.size(); ++i)
	{
		MapCoord coord(ring[i].x(), ring[i].y());
		if (dash && i < dash->size())
			coord.setDashPoint((*dash)[i]);
		output->addCoordinate(coord);
	}
	output->parts().front().setClosed(true);
	auto first = true;
	for (auto const& coord : rest)
	{
		output->addCoordinate(coord, first);
		first = false;
	}
	output->setTags(original.tags());
	output->setPatternRotation(original.getPatternRotation());
	output->setPatternOrigin(original.getPatternOrigin());
	return output;
}

std::unique_ptr<PathObject> replacementFromRing(const PathObject& original, const Ring& ring, const MapCoordVector& rest, const std::vector<bool>* dash = nullptr)
{
	auto output = pathFromRing(original, ring, rest, dash);
	output->setIdentity(original.uuid(), original.createdTime(), original.modifiedTime());
	output->setDerivedFrom(original.derivedFrom());
	return output;
}

std::vector<Building> loadBuildings(const std::vector<const Object*>& in, const Object* skip)
{
	std::vector<Building> buildings;
	for (auto const* object : in)
	{
		if (object == skip)
			continue;
		Building building;
		if (building.load(object))
			buildings.push_back(std::move(building));
	}
	return buildings;
}

Ring convexHull(Ring points)
{
	std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
	points.erase(std::unique(points.begin(), points.end()), points.end());
	if (points.size() < 3)
		return points;
	Ring hull(2 * points.size());
	std::size_t k = 0;
	for (auto const& p : points)
	{
		while (k >= 2 && cross(hull[k - 1] - hull[k - 2], p - hull[k - 2]) <= 0)
			--k;
		hull[k++] = p;
	}
	for (auto i = points.size() - 1, t = k + 1; i > 0; --i)
	{
		auto const& p = points[i - 1];
		while (k >= t && cross(hull[k - 1] - hull[k - 2], p - hull[k - 2]) <= 0)
			--k;
		hull[k++] = p;
	}
	hull.resize(k - 1);
	return hull;
}

/** The minimum-area bounding rectangle of the points, as four corners in order. */
Ring minimumRectangle(const Ring& points)
{
	auto const hull = convexHull(points);
	if (hull.size() < 3)
		return {};
	auto best_area = std::numeric_limits<double>::max();
	Ring best;
	for (std::size_t i = 0; i < hull.size(); ++i)
	{
		auto const edge = hull[(i + 1) % hull.size()] - hull[i];
		auto const len = length(edge);
		if (!(len > 0.0))
			continue;
		auto const u = edge / len;
		auto const v = QPointF(-u.y(), u.x());
		auto min_u = std::numeric_limits<double>::max(), max_u = -min_u, min_v = min_u, max_v = -min_u;
		for (auto const& p : hull)
		{
			auto const pu = p.x() * u.x() + p.y() * u.y();
			auto const pv = p.x() * v.x() + p.y() * v.y();
			min_u = std::min(min_u, pu);
			max_u = std::max(max_u, pu);
			min_v = std::min(min_v, pv);
			max_v = std::max(max_v, pv);
		}
		auto const area = (max_u - min_u) * (max_v - min_v);
		if (area < best_area)
		{
			best_area = area;
			best = { u * min_u + v * min_v, u * max_u + v * min_v, u * max_u + v * max_v, u * min_u + v * max_v };
		}
	}
	return best;
}

void scaleAbout(Ring& ring, const QPointF& centre, double factor)
{
	for (auto& p : ring)
		p = centre + (p - centre) * factor;
}

/** The area of the part of the ring inside the axis-parallel box (Sutherland-Hodgman). */
double clippedArea(const Ring& ring, double x0, double x1, double y0, double y1)
{
	auto clipped = ring;
	auto const clip = [&clipped](auto inside, auto cut) {
		Ring out;
		for (std::size_t i = 0; i < clipped.size(); ++i)
		{
			auto const& a = clipped[i];
			auto const& b = clipped[(i + 1) % clipped.size()];
			auto const a_in = inside(a);
			auto const b_in = inside(b);
			if (a_in)
				out.push_back(a);
			if (a_in != b_in)
				out.push_back(cut(a, b));
		}
		clipped = std::move(out);
	};
	auto const at_x = [](double x) {
		return [x](const QPointF& a, const QPointF& b) { auto const t = (x - a.x()) / (b.x() - a.x()); return QPointF(x, a.y() + t * (b.y() - a.y())); };
	};
	auto const at_y = [](double y) {
		return [y](const QPointF& a, const QPointF& b) { auto const t = (y - a.y()) / (b.y() - a.y()); return QPointF(a.x() + t * (b.x() - a.x()), y); };
	};
	clip([x0](const QPointF& p) { return p.x() >= x0; }, at_x(x0));
	clip([x1](const QPointF& p) { return p.x() <= x1; }, at_x(x1));
	clip([y0](const QPointF& p) { return p.y() >= y0; }, at_y(y0));
	clip([y1](const QPointF& p) { return p.y() <= y1; }, at_y(y1));
	return clipped.size() < 3 ? 0.0 : std::abs(ringArea(clipped));
}

/**
 * The L-shaped generalization of the ring: its minimum rectangle with the
 * rectangular notch at one corner which leaves the smallest symmetric
 * difference to the ring, six corners in order; the rectangle itself when
 * no notch makes the difference smaller. Notch sizes are taken from the
 * ring's vertices in the rectangle's frame.
 */
Ring cornerRectangle(const Ring& ring)
{
	auto const rectangle = minimumRectangle(ring);
	if (rectangle.size() != 4)
		return {};
	auto const origin = rectangle[0];
	auto const u = rectangle[1] - rectangle[0];
	auto const v = rectangle[3] - rectangle[0];
	auto const lu = length(u);
	auto const lv = length(v);
	if (!(lu > 0.0) || !(lv > 0.0))
		return rectangle;
	auto const eu = u / lu;
	auto const ev = v / lv;
	Ring local;
	local.reserve(ring.size());
	std::vector<double> xs, ys;
	constexpr double margin = 0.01;  // mm: notches thinner than this are noise
	for (auto const& p : ring)
	{
		auto const d = p - origin;
		local.emplace_back(d.x() * eu.x() + d.y() * eu.y(), d.x() * ev.x() + d.y() * ev.y());
		if (local.back().x() > margin && local.back().x() < lu - margin)
			xs.push_back(local.back().x());
		if (local.back().y() > margin && local.back().y() < lv - margin)
			ys.push_back(local.back().y());
	}
	auto const area = std::abs(ringArea(local));
	auto best_error = lu * lv - area;  // the rectangle without a notch
	auto best_corner = -1;
	auto best_x = 0.0, best_y = 0.0;
	// Corners in order: (0,0), (lu,0), (lu,lv), (0,lv).
	QPointF const corners[4] = { { 0.0, 0.0 }, { lu, 0.0 }, { lu, lv }, { 0.0, lv } };
	for (int c = 0; c < 4; ++c)
	{
		for (auto const x : xs)
		{
			auto const x0 = corners[c].x() > 0.0 ? x : 0.0;
			auto const x1 = corners[c].x() > 0.0 ? lu : x;
			for (auto const y : ys)
			{
				auto const y0 = corners[c].y() > 0.0 ? y : 0.0;
				auto const y1 = corners[c].y() > 0.0 ? lv : y;
				auto const notch = (x1 - x0) * (y1 - y0);
				auto const error = lu * lv - area - notch + 2.0 * clippedArea(local, x0, x1, y0, y1);
				if (error < best_error - 0.001)
				{
					best_error = error;
					best_corner = c;
					best_x = x;
					best_y = y;
				}
			}
		}
	}
	if (best_corner < 0)
		return rectangle;
	Ring shape;
	for (int c = 0; c < 4; ++c)
	{
		auto const& corner = corners[c];
		if (c != best_corner)
		{
			shape.push_back(corner);
			continue;
		}
		// Coming from the previous corner, corners 0 and 2 lie at the end of an
		// edge along y, corners 1 and 3 at the end of an edge along x.
		QPointF const inner(best_x, best_y);
		if (c % 2 == 0)
			shape.insert(shape.end(), { QPointF(corner.x(), best_y), inner, QPointF(best_x, corner.y()) });
		else
			shape.insert(shape.end(), { QPointF(best_x, corner.y()), inner, QPointF(corner.x(), best_y) });
	}
	for (auto& p : shape)
		p = origin + eu * p.x() + ev * p.y();
	return shape;
}

QPointF ringMean(const Ring& ring)
{
	QPointF mean;
	for (auto const& p : ring)
		mean += p;
	return mean / double(ring.size());
}

}  // namespace


OpResult makeRectangular(const Map& /*map*/, const std::vector<const Object*>& in, const RectangularParams& params)
{
	OpResult result;
	if (params.tolerance_deg < 0.0 || params.tolerance_deg > 90.0)
	{
		result.error = QStringLiteral("The tolerance must be between 0 and 90 degrees.");
		return result;
	}
	double reference_angle = 0.0;
	QPointF reference_point;
	if (params.reference)
	{
		if (params.reference->getType() != Object::Path || params.reference->asPath()->getCoordinateCount() < 2)
		{
			result.error = QStringLiteral("The reference must be a line object.");
			return result;
		}
		auto const& coords = params.reference->asPath()->getRawCoordinateVector();
		auto const chord = QPointF(coords.back().x() - coords.front().x(), coords.back().y() - coords.front().y());
		reference_angle = std::atan2(chord.y(), chord.x());
		reference_point = QPointF(coords.front().x(), coords.front().y());
	}
	auto const buildings = loadBuildings(in, params.reference);
	if (buildings.empty())
	{
		result.error = QStringLiteral("Select closed line or area objects.");
		return result;
	}
	QStringList skipped;
	for (auto const& building : buildings)
	{
		auto ring = building.ring;
		QString reason;
		if (!rectangularize(ring, params.tolerance_deg, params.reference ? &reference_angle : nullptr, params.reference && params.place_at_distance, params.distance_mm, reference_point, reason))
		{
			skipped.append(QStringLiteral("%1 (%2)").arg(building.path->getSymbol()->getNumberAsString(), reason));
			continue;
		}
		if (sameRing(ring, building.ring))
			continue;
		result.out.push_back(replacementFromRing(*building.path, ring, building.rest));
		result.consumed.push_back(building.path);
	}
	if (!skipped.isEmpty())
	{
		QString const message = QStringLiteral("Skipped: ") + skipped.join(QStringLiteral("; "));
		if (result.out.empty())
			result.error = message;
		else
			result.warning = message;
	}
	return result;
}

OpResult simplifyBuildings(const Map& /*map*/, const std::vector<const Object*>& in, const SimplifyBuildingParams& params)
{
	OpResult result;
	auto const buildings = loadBuildings(in, nullptr);
	if (buildings.empty())
	{
		result.error = QStringLiteral("Select closed line or area objects.");
		return result;
	}
	for (auto const& building : buildings)
	{
		auto ring = building.ring;
		auto dash = building.dash;
		auto kept = building.dash;
		if (!params.keep_corners)
			kept.assign(ring.size(), false);
		auto const area_in = ringArea(ring);
		while (ring.size() > 3)
		{
			auto best = ring.size();
			auto best_offset = std::numeric_limits<double>::max();
			auto const n = ring.size();
			for (std::size_t i = 0; i < n; ++i)
			{
				if (kept[i])
					continue;
				auto const& previous = ring[(i + n - 1) % n];
				auto const& next = ring[(i + 1) % n];
				auto const incoming = ring[i] - previous;
				auto const outgoing = next - ring[i];
				auto const chord = next - previous;
				auto const chord_len = length(chord);
				auto const offset = chord_len > 0.0 ? std::abs(cross(chord, ring[i] - previous)) / chord_len : length(incoming);
				auto turn = 180.0;
				if (length(incoming) > 0.0 && length(outgoing) > 0.0)
					turn = std::abs(qRadiansToDegrees(std::atan2(cross(incoming, outgoing), incoming.x() * outgoing.x() + incoming.y() * outgoing.y())));
				if ((turn < params.angle_deg || offset < params.offset_mm) && offset < best_offset)
				{
					best_offset = offset;
					best = i;
				}
			}
			if (best == ring.size())
				break;
			ring.erase(ring.begin() + best);
			kept.erase(kept.begin() + best);
			dash.erase(dash.begin() + best);
		}
		if (params.compensate_area)
		{
			auto const area_out = ringArea(ring);
			if (area_out > 0.0 && area_in > 0.0)
				scaleAbout(ring, ringCentroid(ring), std::sqrt(area_in / area_out));
		}
		if (sameRing(ring, building.ring))
			continue;
		result.out.push_back(replacementFromRing(*building.path, ring, building.rest, &dash));
		result.consumed.push_back(building.path);
	}
	return result;
}

OpResult buildingRectangles(const Map& /*map*/, const std::vector<const Object*>& in, const RectangleParams& params)
{
	OpResult result;
	if (!(params.area_factor > 0.0))
	{
		result.error = QStringLiteral("The area factor must be positive.");
		return result;
	}
	if (params.mode == RectangleParams::Series && params.count < 1)
	{
		result.error = QStringLiteral("The series needs at least one rectangle.");
		return result;
	}
	auto const buildings = loadBuildings(in, nullptr);
	if (buildings.empty())
	{
		result.error = QStringLiteral("Select closed line or area objects.");
		return result;
	}
	
	if (params.mode == RectangleParams::Corner)
	{
		for (auto const& building : buildings)
		{
			auto shape = cornerRectangle(building.ring);
			if (shape.size() < 4)
				continue;
			auto const area = std::abs(ringArea(shape));
			if (area > 0.0)
				scaleAbout(shape, ringCentroid(shape), std::sqrt(params.area_factor * std::abs(ringArea(building.ring)) / area));
			if (sameRing(shape, building.ring))
				continue;
			result.out.push_back(replacementFromRing(*building.path, shape, {}));
			result.consumed.push_back(building.path);
		}
		return result;
	}
	
	if (params.mode == RectangleParams::Each)
	{
		for (auto const& building : buildings)
		{
			auto rectangle = minimumRectangle(building.ring);
			if (rectangle.size() != 4)
				continue;
			auto const area = ringArea(rectangle);
			if (area > 0.0)
				scaleAbout(rectangle, ringMean(rectangle), std::sqrt(params.area_factor * ringArea(building.ring) / area));
			result.out.push_back(replacementFromRing(*building.path, rectangle, {}));
			result.consumed.push_back(building.path);
		}
		return result;
	}
	
	Ring all_points;
	auto total_area = 0.0;
	for (auto const& building : buildings)
	{
		all_points.insert(all_points.end(), building.ring.begin(), building.ring.end());
		total_area += ringArea(building.ring);
	}
	auto rectangle = minimumRectangle(all_points);
	if (rectangle.size() != 4 || !(ringArea(rectangle) > 0.0))
	{
		result.error = QStringLiteral("The objects do not span a rectangle.");
		return result;
	}
	auto const& principal = buildings.front();
	if (params.mode == RectangleParams::All)
	{
		scaleAbout(rectangle, ringMean(rectangle), std::sqrt(params.area_factor * total_area / ringArea(rectangle)));
		result.out.push_back(replacementFromRing(*principal.path, rectangle, {}));
	}
	else  // Series
	{
		// Split along the long axis into count equal rectangles, each with its share of the area.
		auto u = rectangle[1] - rectangle[0];
		auto v = rectangle[3] - rectangle[0];
		auto origin = rectangle[0];
		if (length(u) < length(v))
		{
			std::swap(u, v);
			origin = rectangle[0];
		}
		auto const long_len = length(u);
		auto const short_len = length(v);
		auto const unit_u = u / long_len;
		auto const unit_v = v / short_len;
		auto const piece_len = long_len / params.count;
		auto const piece_area = params.area_factor * total_area / params.count;
		auto const piece_height = piece_area / piece_len;
		auto const centre_v = short_len / 2;
		for (int k = 0; k < params.count; ++k)
		{
			auto const a = unit_u * (k * piece_len);
			auto const b = unit_u * ((k + 1) * piece_len);
			auto const low = unit_v * (centre_v - piece_height / 2);
			auto const high = unit_v * (centre_v + piece_height / 2);
			Ring piece = { origin + a + low, origin + b + low, origin + b + high, origin + a + high };
			auto output = pathFromRing(*principal.path, piece, {});
			if (k == 0)
			{
				output->setIdentity(principal.path->uuid(), principal.path->createdTime(), principal.path->modifiedTime());
				output->setDerivedFrom(principal.path->derivedFrom());
			}
			else
			{
				output->setDerivedFrom(principal.path->uuid());
			}
			result.out.push_back(std::move(output));
		}
	}
	for (auto const& building : buildings)
		result.consumed.push_back(building.path);
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
