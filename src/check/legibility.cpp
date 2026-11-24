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


#include "legibility.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include "core/color_group.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/renderables/renderable.h"
#include "core/renderables/renderable_implementation.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"
#include "check/standard_minimums.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Legibility", text);
}

QString mm(double value)
{
	return QString::number(value, 'f', 2);
}

constexpr double flatten_tolerance = 0.05;
constexpr double vertex_tolerance = 0.001;


bool isContourClass(const Symbol* symbol)
{
	auto const number = symbol->getNumberComponent(0);
	return number >= 101 && number <= 103;
}

bool isCliffClass(const Symbol* symbol)
{
	auto const number = symbol->getNumberComponent(0);
	return number == 201 || number == 202;
}

/// The earth bank, which the standard lets stand beside a contour (ISOM 2017-2, 2.11.3, Minimum gaps).
bool isEarthBank(const Symbol* symbol)
{
	return symbol->getNumberComponent(0) == 104;
}

/// The lines whose joins the standard excepts from the minimum gap (the same section): earth walls,
/// watercourses, roads, tracks and paths, power lines, walls and fences; and, proposed, the two
/// boundaries, which end at them in the same way.
bool isNetworkClass(const Symbol* symbol)
{
	if ((symbol->getContainedTypes() & Symbol::Area) != 0)
		return false;
	switch (symbol->getNumberComponent(0))
	{
	case 105: case 106:
	case 304: case 305: case 306:
	case 415: case 416:
	case 502: case 503: case 504: case 505: case 506: case 507: case 508:
	case 510: case 511:
	case 513: case 514: case 515: case 516: case 517: case 518:
		return true;
	default:
		return false;
	}
}


/** Whether a line is drawn in pieces: dashes, or symbols along it. */
bool drawnInPieces(const Symbol* symbol)
{
	if (!symbol)
		return false;
	if (symbol->getType() == Symbol::Line)
	{
		auto const* line = symbol->asLine();
		auto const has = [](const PointSymbol* point) { return point && !point->isEmpty(); };
		return line->isDashed() || has(line->getMidSymbol()) || has(line->getDashSymbol()) || has(line->getStartSymbol()) || has(line->getEndSymbol());
	}
	if (symbol->getType() == Symbol::Combined)
	{
		auto const* combined = symbol->asCombined();
		for (int i = 0; i < combined->getNumParts(); ++i)
		{
			if (drawnInPieces(combined->getPart(i)))
				return true;
		}
	}
	return false;
}


/** The centre line (or boundary) of an object as straight segments. */
struct Outline
{
	const Object* object = nullptr;
	const Symbol* symbol = nullptr;
	std::vector<MapCoordF> points;      ///< The vertices, parts one after the other.
	std::vector<bool> segment_follows;  ///< Whether a segment leads from the vertex to the next.
	/// Half the stroke at each vertex's segment, when the outline is made of what is drawn
	/// (dashes, dots, the shapes of a point symbol); empty when half_width holds for all.
	std::vector<float> widths;
	/// The line as one band along its centre line, when the points are those of what is drawn:
	/// its own vertices for sharesVertex(), and whether it reaches another object.
	std::shared_ptr<Outline> band;
	double half_width = 0.0;            ///< The largest half stroke.
	QRectF extent;                      ///< Of the vertices.
	ColourGroup group = ColourGroup::Other;
	bool contour = false;
	bool cliff = false;
	bool earth_bank = false;
	bool network = false;
	bool without_gap = false;           ///< An area the standard asks no gap around.

	bool build(const Object* object);
	bool buildFromRenderables(const Object* object);
	const PathObject* closedPath() const;
	double halfWidthAt(std::size_t i) const { return widths.empty() ? half_width : double(widths[i]); }
	const std::vector<MapCoordF>& vertices() const { return band ? band->points : points; }
};

/// the design notes, "The distance on what is drawn".
bool Outline::buildFromRenderables(const Object* input)
{
	input->update();
	half_width = 0.0;
	auto const add = [this](const QPolygonF& polygon, double half, bool close) {
		if (polygon.isEmpty())
			return;
		auto const n = polygon.size();
		for (int k = 0; k < n; ++k)
		{
			points.emplace_back(polygon[k].x() / 100.0, polygon[k].y() / 100.0);
			segment_follows.push_back(k + 1 < n);
			widths.push_back(float(half));
		}
		if (close && n > 2 && polygon.first() != polygon.last())
		{
			segment_follows.back() = true;
			points.emplace_back(polygon.first().x() / 100.0, polygon.first().y() / 100.0);
			segment_follows.push_back(false);
			widths.push_back(float(half));
		}
		half_width = std::max(half_width, half);
	};
	// A dot is a segment of no length to speak of, as wide as the dot.
	auto const add_disc = [&add](const QRectF& extent) {
		if (extent.isEmpty())
			return;
		auto const centre = extent.center();
		QPolygonF polygon;
		polygon << QPointF(100 * centre.x() - 0.01, 100 * centre.y()) << QPointF(100 * centre.x() + 0.01, 100 * centre.y());
		add(polygon, extent.width() / 2, false);
	};
	// Curves are flattened in hundredths of a millimetre.
	auto const scale = QTransform::fromScale(100.0, 100.0);
	input->renderables().forEach([&](const Renderable& renderable) {
		if (auto const* line = dynamic_cast<const LineRenderable*>(&renderable))
		{
			for (auto const& polygon : line->painterPath().toSubpathPolygons(scale))
				add(polygon, line->lineWidth() / 2, false);
		}
		else if (auto const* area = dynamic_cast<const AreaRenderable*>(&renderable))
		{
			if (area->painterPath())
			{
				for (auto const& polygon : area->painterPath()->toSubpathPolygons(scale))
					add(polygon, 0.0, true);
			}
		}
		else if (dynamic_cast<const DotRenderable*>(&renderable) || dynamic_cast<const CircleRenderable*>(&renderable))
		{
			add_disc(renderable.getExtent());
		}
	});
	return !points.empty();
}

bool Outline::build(const Object* input)
{
	object = input;
	symbol = input->getSymbol();
	half_width = strokeHalfWidth(symbol);
	group = colourGroupOf(symbol->guessDominantColor());
	contour = isContourClass(symbol);
	cliff = isCliffClass(symbol);
	earth_bank = isEarthBank(symbol);
	network = isNetworkClass(symbol);
	without_gap = isom2017AreaWithoutGap(*symbol);
	points.clear();
	segment_follows.clear();
	widths.clear();
	band.reset();
	switch (input->getType())
	{
	case Object::Path:
		{
			auto const coords = ops::flattenCoords(input->asPath()->getRawCoordinateVector(), flatten_tolerance);
			points.reserve(coords.size());
			segment_follows.reserve(coords.size());
			for (std::size_t i = 0; i < coords.size(); ++i)
			{
				points.emplace_back(coords[i]);
				segment_follows.push_back(i + 1 < coords.size() && !coords[i].isHolePoint());
			}
			// A line drawn in dashes or dots is measured by its dashes and dots.
			if ((symbol->getContainedTypes() & Symbol::Area) == 0 && drawnInPieces(symbol))
			{
				auto centre_line = std::move(points);
				auto follows = std::move(segment_follows);
				points.clear();
				segment_follows.clear();
				if (buildFromRenderables(input))
				{
					band = std::make_shared<Outline>();
					band->object = object;
					band->symbol = symbol;
					band->points = std::move(centre_line);
					band->segment_follows = std::move(follows);
					band->half_width = strokeHalfWidth(symbol);
				}
				else
				{
					points = std::move(centre_line);
					segment_follows = std::move(follows);
					widths.clear();
					half_width = strokeHalfWidth(symbol);
				}
}
		}
		break;
	case Object::Point:
		{
			// The shapes of the point symbol; its box when it draws nothing the scan can read.
			if (!buildFromRenderables(input))
			{
				auto const rect = input->getExtent();
				if (rect.isEmpty())
					return false;
				widths.clear();
				for (auto const& corner : { rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(), rect.topLeft() })
					points.emplace_back(corner);
				segment_follows.assign(5, true);
				segment_follows.back() = false;
			}
		}
		break;
	default:
		return false;
	}
	if (points.empty())
		return false;
	extent = QRectF(points.front(), points.front());
	for (auto const& point : points)
	{
		extent.setLeft(std::min(extent.left(), point.x()));
		extent.setRight(std::max(extent.right(), point.x()));
		extent.setTop(std::min(extent.top(), point.y()));
		extent.setBottom(std::max(extent.bottom(), point.y()));
	}
	if (band && !band->points.empty())
	{
		band->extent = QRectF(band->points.front(), band->points.front());
		for (auto const& point : band->points)
		{
			band->extent.setLeft(std::min(band->extent.left(), point.x()));
			band->extent.setRight(std::max(band->extent.right(), point.x()));
			band->extent.setTop(std::min(band->extent.top(), point.y()));
			band->extent.setBottom(std::max(band->extent.bottom(), point.y()));
		}
	}
	return true;
}

const PathObject* Outline::closedPath() const
{
	if (object->getType() != Object::Path)
		return nullptr;
	auto const* path = object->asPath();
	if (path->parts().empty() || !path->parts().front().isClosed())
		return nullptr;
	return path;
}


double cross(const MapCoordF& a, const MapCoordF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

bool onSegment(const MapCoordF& p, const MapCoordF& a, const MapCoordF& b)
{
	return p.x() >= std::min(a.x(), b.x()) - 1e-12 && p.x() <= std::max(a.x(), b.x()) + 1e-12
	       && p.y() >= std::min(a.y(), b.y()) - 1e-12 && p.y() <= std::max(a.y(), b.y()) + 1e-12;
}

bool segmentsIntersect(const MapCoordF& a1, const MapCoordF& a2, const MapCoordF& b1, const MapCoordF& b2)
{
	auto const d1 = cross(b2 - b1, a1 - b1);
	auto const d2 = cross(b2 - b1, a2 - b1);
	auto const d3 = cross(a2 - a1, b1 - a1);
	auto const d4 = cross(a2 - a1, b2 - a1);
	if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
		return true;
	return (d1 == 0 && onSegment(a1, b1, b2)) || (d2 == 0 && onSegment(a2, b1, b2))
	       || (d3 == 0 && onSegment(b1, a1, a2)) || (d4 == 0 && onSegment(b2, a1, a2));
}

double pointSegmentDistance(const MapCoordF& p, const MapCoordF& a, const MapCoordF& b, MapCoordF& closest)
{
	auto const ab = b - a;
	auto const len2 = ab.x() * ab.x() + ab.y() * ab.y();
	auto t = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0.0;
	t = qBound(0.0, t, 1.0);
	closest = MapCoordF(a.x() + t * ab.x(), a.y() + t * ab.y());
	return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}

/** The distance of two segments and the closest points; 0 when they touch or cross. */
double segmentDistance(const MapCoordF& a1, const MapCoordF& a2, const MapCoordF& b1, const MapCoordF& b2, MapCoordF& pa, MapCoordF& pb)
{
	auto const intersect = segmentsIntersect(a1, a2, b1, b2);
	if (intersect)
	{
		// The place where they cross; segments along one another touch at an end, found below.
		auto const rx = a2.x() - a1.x();
		auto const ry = a2.y() - a1.y();
		auto const sx = b2.x() - b1.x();
		auto const sy = b2.y() - b1.y();
		auto const denominator = rx * sy - ry * sx;
		if (std::abs(denominator) > 1e-12)
		{
			auto const t = std::max(0.0, std::min(1.0, ((b1.x() - a1.x()) * sy - (b1.y() - a1.y()) * sx) / denominator));
			pa = MapCoordF(a1.x() + rx * t, a1.y() + ry * t);
			pb = pa;
			return 0.0;
		}
	}
	MapCoordF closest;
	auto best = pointSegmentDistance(a1, b1, b2, closest);
	pa = a1;
	pb = closest;
	auto d = pointSegmentDistance(a2, b1, b2, closest);
	if (d < best)
	{
		best = d;
		pa = a2;
		pb = closest;
	}
	d = pointSegmentDistance(b1, a1, a2, closest);
	if (d < best)
	{
		best = d;
		pa = closest;
		pb = b1;
	}
	d = pointSegmentDistance(b2, a1, a2, closest);
	if (d < best)
	{
		best = d;
		pa = closest;
		pb = b2;
	}
	return intersect ? 0.0 : best;
}

/** The distance of two axis-aligned boxes, 0 when they overlap. */
double boxDistance(double min_ax, double min_ay, double max_ax, double max_ay, double min_bx, double min_by, double max_bx, double max_by)
{
	auto const dx = std::max({ 0.0, min_ax - max_bx, min_bx - max_ax });
	auto const dy = std::max({ 0.0, min_ay - max_by, min_by - max_ay });
	return std::hypot(dx, dy);
}

bool sharesVertex(const Outline& a, const Outline& b)
{
	for (auto const& p : a.vertices())
	{
		for (auto const& q : b.vertices())
		{
			if (std::abs(p.x() - q.x()) <= vertex_tolerance && std::abs(p.y() - q.y()) <= vertex_tolerance)
				return true;
		}
	}
	return false;
}

/**
 * The smallest distance between the centre lines of two outlines which is
 * below the bound; returns the bound when there is none. Zero means the
 * outlines touch or cross.
 */
double outlineDistance(const Outline& a, const Outline& b, double bound, MapCoordF& where, MapCoordF& on_a, MapCoordF& on_b)
{
	// The gap between the strokes: the distance of two segments less half the stroke of each.
	// The bound and the result are gaps too; a result of 0 or less means the strokes touch.
	auto best = bound;
	MapCoordF pa, pb;
	for (std::size_t i = 0; i + 1 < a.points.size(); ++i)
	{
		if (!a.segment_follows[i])
			continue;
		auto const& a1 = a.points[i];
		auto const& a2 = a.points[i + 1];
		auto const wa = a.halfWidthAt(i);
		auto const min_ax = std::min(a1.x(), a2.x());
		auto const max_ax = std::max(a1.x(), a2.x());
		auto const min_ay = std::min(a1.y(), a2.y());
		auto const max_ay = std::max(a1.y(), a2.y());
		if (boxDistance(min_ax, min_ay, max_ax, max_ay, b.extent.left(), b.extent.top(), b.extent.right(), b.extent.bottom()) - wa - b.half_width >= best)
			continue;
		for (std::size_t j = 0; j + 1 < b.points.size(); ++j)
		{
			if (!b.segment_follows[j])
				continue;
			auto const& b1 = b.points[j];
			auto const& b2 = b.points[j + 1];
			auto const wb = b.halfWidthAt(j);
			if (boxDistance(min_ax, min_ay, max_ax, max_ay, std::min(b1.x(), b2.x()), std::min(b1.y(), b2.y()), std::max(b1.x(), b2.x()), std::max(b1.y(), b2.y())) - wa - wb >= best)
				continue;
			auto const d = segmentDistance(a1, a2, b1, b2, pa, pb) - wa - wb;
			if (d < best)
			{
				best = d;
				where = MapCoordF((pa.x() + pb.x()) / 2, (pa.y() + pb.y()) / 2);
				on_a = pa;  // foom: for the line marker
				on_b = pb;
				if (best <= 0.0)
					return best;
			}
		}
	}
	return best;
}

/**
 * Whether a place on a line lies at an end of the line which points at the
 * other place: the line ends there before the other object, as a boundary of
 * dots before a road. The band is the line's centre line.
 */
/// How far around a join, in mm, two joined lines may be nearer to one another than the gap.
constexpr double join_radius = 1.0;

/**
 * outlineDistance() of what lies outside a circle: the parts of the first
 * outline farther than the radius from the centre, against the second.
 */
double outlineDistanceOutside(const Outline& a, const Outline& b, double bound, const MapCoordF& centre, double radius, MapCoordF& where, MapCoordF& on_a, MapCoordF& on_b)
{
	auto best = bound;
	MapCoordF pa, pb;
	for (std::size_t i = 0; i + 1 < a.points.size(); ++i)
	{
		if (!a.segment_follows[i])
			continue;
		auto const& p = a.points[i];
		auto const& q = a.points[i + 1];
		auto const wa = a.halfWidthAt(i);
		// The parameters at which the segment enters and leaves the circle.
		auto const dx = q.x() - p.x();
		auto const dy = q.y() - p.y();
		auto const fx = p.x() - centre.x();
		auto const fy = p.y() - centre.y();
		auto const qa = dx * dx + dy * dy;
		auto const qb = 2 * (fx * dx + fy * dy);
		auto const qc = fx * fx + fy * fy - radius * radius;
		double ranges[2][2] = { { 0.0, 1.0 }, { 1.0, 1.0 } };
		auto count = 1;
		if (qa < 1e-18)
		{
			if (qc < 0.0)
				count = 0;
		}
		else
		{
			auto const discriminant = qb * qb - 4 * qa * qc;
			if (discriminant > 0.0)
			{
				auto const root = std::sqrt(discriminant);
				auto const t1 = (-qb - root) / (2 * qa);
				auto const t2 = (-qb + root) / (2 * qa);
				count = 0;
				if (t1 > 0.0)
				{
					ranges[count][0] = 0.0;
					ranges[count][1] = std::min(t1, 1.0);
					++count;
				}
				if (t2 < 1.0)
				{
					ranges[count][0] = std::max(t2, 0.0);
					ranges[count][1] = 1.0;
					++count;
				}
			}
		}
		for (auto r = 0; r < count; ++r)
		{
			auto const a1 = MapCoordF(p.x() + dx * ranges[r][0], p.y() + dy * ranges[r][0]);
			auto const a2 = MapCoordF(p.x() + dx * ranges[r][1], p.y() + dy * ranges[r][1]);
			for (std::size_t j = 0; j + 1 < b.points.size(); ++j)
			{
				if (!b.segment_follows[j])
					continue;
				auto const d = segmentDistance(a1, a2, b.points[j], b.points[j + 1], pa, pb) - wa - b.halfWidthAt(j);
				if (d < best)
				{
					best = d;
					where = MapCoordF((pa.x() + pb.x()) / 2, (pa.y() + pb.y()) / 2);
					on_a = pa;
					on_b = pb;
				}
			}
		}
	}
	return best;
}

bool endsToward(const Outline& band, double reach, const MapCoordF& on_line, const MapCoordF& on_other)
{
	auto const check = [&](const MapCoordF& end, const MapCoordF& before) {
		if (std::hypot(on_line.x() - end.x(), on_line.y() - end.y()) > reach)
			return false;
		auto const dx = end.x() - before.x();
		auto const dy = end.y() - before.y();
		auto const length = std::hypot(dx, dy);
		if (length < 1e-9)
			return false;
		auto const vx = on_other.x() - end.x();
		auto const vy = on_other.y() - end.y();
		// Ahead of the end rather than beside it.
		return (vx * dx + vy * dy) / length > std::abs(vx * dy - vy * dx) / length;
	};
	std::size_t start = 0;
	for (std::size_t i = 0; i < band.points.size(); ++i)
	{
		if (band.segment_follows[i])
			continue;
		if (i > start)
		{
			auto const& first = band.points[start];
			auto const& last = band.points[i];
			auto const closed = std::abs(first.x() - last.x()) <= vertex_tolerance && std::abs(first.y() - last.y()) <= vertex_tolerance;
			if (!closed && (check(first, band.points[start + 1]) || check(last, band.points[i - 1])))
				return true;
		}
		start = i + 1;
	}
	return false;
}

/** The distance from a point to the centre line of an outline. */
double pointOutlineDistance(const MapCoordF& p, const Outline& outline)
{
	auto best = std::numeric_limits<double>::max();
	MapCoordF closest;
	for (std::size_t i = 0; i + 1 < outline.points.size(); ++i)
	{
		if (outline.segment_follows[i])
			best = std::min(best, pointSegmentDistance(p, outline.points[i], outline.points[i + 1], closest));
	}
	return best;
}

bool contains(const Outline& outer, const Outline& inner)
{
	auto const* path = outer.closedPath();
	return path && !inner.points.empty() && path->isPointInsideArea(inner.points.front());
}


Issue objectIssue(const char* rule, const Object& object, double measured, double limit, const QString& message)
{
	Issue issue;
	issue.rule = rule;
	issue.severity = Issue::Warning;
	issue.objects.append(object.uuid());
	object.update();
	issue.where = MapCoordF(object.getExtent().center());
	issue.measured = measured;
	issue.limit = limit;
	issue.message = message;
	return issue;
}

/// A point 0.02 mm inside the outline of a hole.
bool pointInside(const PathPart& hole, MapCoordF& inside)
{
	auto const& coords = hole.path_coords;
	for (std::size_t i = 0; i + 1 < coords.size(); ++i)
	{
		auto const a = coords[i].pos;
		auto const b = coords[i + 1].pos;
		auto const length = std::hypot(b.x() - a.x(), b.y() - a.y());
		if (length < 1e-6)
			continue;
		for (auto const side : { 1.0, -1.0 })
		{
			MapCoordF const p((a.x() + b.x()) / 2 - side * 0.02 * (b.y() - a.y()) / length,
			                  (a.y() + b.y()) / 2 + side * 0.02 * (b.x() - a.x()) / length);
			if (hole.isPointInside(p))
			{
				inside = p;
				return true;
			}
		}
	}
	return false;
}

/// Whether a hole of the object is an opening: nothing but white lies in it.
bool isOpening(const MapPart& part, const Object& object, const PathPart& hole)
{
	MapCoordF inside;
	if (!pointInside(hole, inside))
		return false;
	std::vector<Object*> candidates;
	part.query(QRectF(inside.x() - 0.01, inside.y() - 0.01, 0.02, 0.02), candidates);
	for (auto const* candidate : candidates)
	{
		if (candidate == &object || candidate->getType() != Object::Path)
			continue;
		auto const* symbol = candidate->getSymbol();
		if (!symbol || symbol->isHidden() || (symbol->getContainedTypes() & Symbol::Area) == 0)
			continue;
		auto const* color = symbol->guessDominantColor();
		if (color && color->isWhite())
			continue;
		if (candidate->asPath()->isPointInsideArea(inside))
			return false;
	}
	return true;
}

void checkSizes(const MapPart& part, const Object& object, const LegibilityParams& params, MapStandard standard, double enlargement, std::vector<Issue>& issues)
{
	if (object.getType() != Object::Path)
		return;
	auto const* path = object.asPath();
	auto const* symbol = object.getSymbol();
	auto const types = symbol->getContainedTypes();
	path->update();
	// A symbol with an area is an area, also when it has an outline (a combined symbol of an area and
	// a line, as an OCD file gives a building): its size is checked, not its length.
	auto const is_area = (types & Symbol::Area) != 0;
	if (params.minimum_length && !is_area && (types & Symbol::Line))
	{
		auto const minimum = minimumLineLength(symbol, enlargement, standard);
		if (minimum > 0.0)
		{
			auto length = 0.0;
			for (auto const& part : path->parts())
				length += part.length();
			if (length < minimum)
				issues.push_back(objectIssue(rule::line_short, object, length, minimum, tr("The line is %1 mm long; the minimum of symbol %2 is %3 mm.").arg(mm(length), symbol->getNumberAsString(), mm(minimum))));
		}
	}
	else if (params.minimum_area && is_area)
	{
		auto const minimum = minimumAreaSize(symbol, enlargement, standard);
		if (minimum > 0.0 && !path->parts().empty())
		{
			auto area = std::abs(path->parts().front().calculateArea());
			for (std::size_t i = 1; i < path->parts().size(); ++i)
				area -= std::abs(path->parts()[i].calculateArea());
			area = std::max(0.0, area);
			if (area < minimum)
			{
				issues.push_back(objectIssue(rule::area_small, object, area, minimum, tr("The area is %1 mm²; the minimum of symbol %2 is %3 mm².").arg(mm(area), symbol->getNumberAsString(), mm(minimum))));
				return;
			}
		}
		// the design notes, "Openings": a hole with nothing in it which is smaller
		// than the standard allows an opening in this screen to be. The smallest one is reported.
		auto const opening_minimum = enlargement > 0.0 ? standardOpeningMinimum(standard, *symbol) * enlargement * enlargement : 0.0;
		if (opening_minimum > 0.0 && path->parts().size() > 1)
		{
			auto smallest = opening_minimum;
			auto found = false;
			for (std::size_t i = 1; i < path->parts().size(); ++i)
			{
				auto const opening = std::abs(path->parts()[i].calculateArea());
				if (opening < smallest && isOpening(part, object, path->parts()[i]))
				{
					smallest = opening;
					found = true;
				}
			}
			if (found)
				issues.push_back(objectIssue(rule::area_small, object, smallest, opening_minimum, tr("An opening in the area is %1 mm²; the minimum in symbol %2 is %3 mm².").arg(mm(smallest), symbol->getNumberAsString(), mm(opening_minimum))));
		}
	}
}

bool inScope(const Object& object, const LegibilityParams& params)
{
	auto const* symbol = object.getSymbol();
	if (!symbol || symbol->isHidden())
		return false;
	if (!params.symbols.empty() && std::find(params.symbols.begin(), params.symbols.end(), symbol) == params.symbols.end())
		return false;
	if (!params.extent.isNull())
	{
		object.update();
		if (!params.extent.intersects(object.getExtent()))
			return false;
	}
	return true;
}

/** The gap the symbol asks for, or the scan's gap when it asks for none. */
double symbolGap(const Symbol* symbol, const LegibilityParams& params)
{
	return (symbol && symbol->minimumGap() > 0) ? 0.001 * symbol->minimumGap() : params.gap_mm;
}

void checkPart(const MapPart& part, const LegibilityParams& params, MapStandard standard, double enlargement, std::vector<Issue>& issues)
{
	std::vector<Outline> outlines;
	std::vector<Outline> cliffs;
	std::unordered_map<const Object*, std::size_t> index_of;
	auto widest = 0.0;
	auto largest_gap = params.gap_mm;
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		auto const* object = part.getObject(i);
		if (!inScope(*object, params))
		{
			if (params.gap && params.between_contours && object->getSymbol() && isCliffClass(object->getSymbol()))
			{
				Outline cliff;
				if (cliff.build(object))
					cliffs.push_back(std::move(cliff));
			}
			continue;
		}
		checkSizes(part, *object, params, standard, enlargement, issues);
		if (!params.gap)
			continue;
		Outline outline;
		if (!outline.build(object))
			continue;
		widest = std::max(widest, outline.half_width);
		largest_gap = std::max(largest_gap, symbolGap(outline.symbol, params));
		index_of[object] = outlines.size();
		if (outline.cliff)
			cliffs.push_back(outline);
		outlines.push_back(std::move(outline));
	}
	if (!params.gap)
		return;
	
	std::vector<Object*> candidates;
	for (std::size_t i = 0; i < outlines.size(); ++i)
	{
		auto const& a = outlines[i];
		auto const a_gap = symbolGap(a.symbol, params);
		auto const grow = largest_gap + a.half_width + widest;
		candidates.clear();
		part.query(a.extent.adjusted(-grow, -grow, grow, grow), candidates);
		for (auto const* candidate : candidates)
		{
			auto const found = index_of.find(candidate);
			if (found == index_of.end() || found->second <= i)
				continue;
			auto const& b = outlines[found->second];
			if (params.same_colour_group_only && a.group != b.group)
				continue;
			auto const contour_pair = a.contour && b.contour;
			if (contour_pair && !params.between_contours)
				continue;
			// The standard's own exception: a contour and an earth bank may lie side by side.
			if ((a.contour && b.earth_bank) || (b.contour && a.earth_bank))
				continue;
			// And areas without an outline, the screens of open land and vegetation, need no gap: in a
			// forest map towards anything, in a sprint map, whose standard names gaps of lines only,
			// towards one another.
			if ((standard == MapStandard::Isom2017 && (a.without_gap || b.without_gap))
			    || (standard == MapStandard::Issprom2019 && a.without_gap && b.without_gap))
				continue;
			// Point objects of one symbol set side by side are a pattern the mapper made, as the dots
			// of an orchard.
			if (a.symbol == b.symbol && a.object->getType() == Object::Point && b.object->getType() == Object::Point)
				continue;
			// the design notes, "Legibility": the larger gap of the two symbols.
			auto const limit = std::max(a_gap, symbolGap(b.symbol, params));
			auto const bound = limit + a.half_width + b.half_width;
			if (boxDistance(a.extent.left(), a.extent.top(), a.extent.right(), a.extent.bottom(), b.extent.left(), b.extent.top(), b.extent.right(), b.extent.bottom()) >= bound)
				continue;
			if (sharesVertex(a, b))
				continue;
			MapCoordF where, on_a, on_b;
			auto reduced = outlineDistance(a, b, limit, where, on_a, on_b);
			if (reduced >= limit)
				continue;
			if (reduced <= 0.0)
				continue;  // the outlines touch or overlap
			// Joins. A line drawn in pieces which reaches the other object is joined to it, as a boundary
			// of dots which ends at a road, though its last dot stops short. And the standard's exception
			// for network symbols: a road, a path, a fence or a boundary which ends just before another of
			// them, pointing at it, is joined to it.
			auto joined = false;
			MapCoordF join;
			if (a.band || b.band)
			{
				MapCoordF band_a, band_b;
				joined = outlineDistance(a.band ? *a.band : a, b.band ? *b.band : b, limit, join, band_a, band_b) <= 0.0;
			}
			if (!joined && a.network && b.network && a.object->getType() == Object::Path && b.object->getType() == Object::Path
			    && (endsToward(a.band ? *a.band : a, a.half_width + limit, on_a, on_b)
			        || endsToward(b.band ? *b.band : b, b.half_width + limit, on_b, on_a)))
			{
				joined = true;
				join = where;
			}
			if (joined)
			{
				// A join excuses its own neighbourhood only: a fence which runs along the road it ends at
				// leaves a sliver of paper, and that is a conflict.
				reduced = outlineDistanceOutside(a, b, limit, join, join_radius, where, on_a, on_b);
				reduced = outlineDistanceOutside(b, a, reduced, join, join_radius, where, on_b, on_a);
				if (reduced >= limit || reduced <= 0.0)
					continue;
			}
			if (contains(a, b) || contains(b, a))
				continue;
			if (contour_pair)
			{
				auto covered = false;
				for (auto const& cliff : cliffs)
				{
					if (cliff.object != a.object && cliff.object != b.object && pointOutlineDistance(where, cliff) < cliff.half_width + limit)
					{
						covered = true;
						break;
					}
				}
				if (covered)
					continue;
			}
			Issue issue;
			issue.rule = rule::gap;
			issue.severity = Issue::Warning;
			issue.objects.append(a.object->uuid());
			issue.objects.append(b.object->uuid());
			issue.where = where;
			issue.points = { on_a, on_b };
			issue.measured = reduced;
			issue.limit = limit;
			issue.message = tr("%1 mm between objects of symbols %2 and %3; the minimum is %4 mm.").arg(mm(reduced), a.symbol->getNumberAsString(), b.symbol->getNumberAsString(), mm(limit));
			issues.push_back(issue);
		}
	}
}

}  // namespace


double defaultGap(const Map& map)
{
	auto const scale = map.getScaleDenominator();
	if (scale == 0)
		return 0.15;
	// 0.15 mm at the base scale of the map's standard: 1:4,000 for a sprint map, 1:15,000 otherwise.
	auto const base = mapStandard(map) == MapStandard::Issprom2019 ? 4000.0 : 15000.0;
	return qRound(100.0 * 0.15 * base / scale) / 100.0;
}

double strokeHalfWidth(const Symbol* symbol)
{
	if (!symbol)
		return 0.0;
	switch (symbol->getType())
	{
	case Symbol::Line:
		{
			auto const* line = symbol->asLine();
			auto const main = line->getColor() ? 0.0005 * line->getLineWidth() : 0.0;
			auto widest = main;
			if (line->hasBorder())
			{
				for (auto const* border : { &line->getBorder(), &line->getRightBorder() })
				{
					if (border->isVisible())
						widest = std::max(widest, 0.0005 * line->getLineWidth() + 0.001 * border->shift + 0.0005 * border->width);
				}
			}
			return widest;
		}
	case Symbol::Combined:
		{
			auto const* combined = symbol->asCombined();
			auto widest = 0.0;
			for (int i = 0; i < combined->getNumParts(); ++i)
				widest = std::max(widest, strokeHalfWidth(combined->getPart(i)));
			return widest;
		}
	default:
		return 0.0;
	}
}

double minimumLineLength(const Symbol* symbol)
{
	auto minimum = 0.001 * symbol->getMinimumLength();
	if (symbol->getType() == Symbol::Line)
	{
		auto const* line = symbol->asLine();
		if (line->isDashed() && line->getDashLength() > 0 && line->getBreakLength() > 0)
		{
			auto const group = line->getDashesInGroup() * line->getDashLength() + (line->getDashesInGroup() - 1) * line->getInGroupBreakLength();
			minimum = std::max(minimum, 0.001 * (1.5 * group + 0.5 * line->getBreakLength()));
		}
	}
	return minimum;
}

double minimumAreaSize(const Symbol* symbol)
{
	return 0.001 * symbol->getMinimumArea();
}

double minimumLineLength(const Symbol* symbol, double enlargement, MapStandard standard)
{
	// the design notes, "The standard's minimum sizes".
	if (symbol->getMinimumLength() > 0 || !(enlargement > 0.0))
		return minimumLineLength(symbol);
	auto const stated = standardMinimum(standard, *symbol).length * enlargement;
	return stated > 0.0 ? stated : minimumLineLength(symbol);
}

double minimumAreaSize(const Symbol* symbol, double enlargement, MapStandard standard)
{
	if (symbol->getMinimumArea() > 0 || !(enlargement > 0.0))
		return minimumAreaSize(symbol);
	return standardMinimum(standard, *symbol).area * enlargement * enlargement;
}

std::vector<Issue> checkLegibility(const Map& map, const LegibilityParams& params)
{
	// the design notes, "Legibility".
	std::vector<Issue> issues;
	auto const standard = params.standard_minimums ? mapStandard(map) : MapStandard::None;
	auto const enlargement = standard != MapStandard::None ? standardEnlargement(map) : 0.0;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const role = map.getPart(p)->role();
		// the design notes; the markers: the design notes, "Legibility"
		if (role != MapPart::Role::Layout && role != MapPart::Role::Issues)
			checkPart(*map.getPart(p), params, standard, enlargement, issues);
	}
	return issues;
}


}  // namespace OpenOrienteering
