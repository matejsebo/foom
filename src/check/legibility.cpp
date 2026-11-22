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
#include <unordered_map>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QRectF>
#include <QString>

#include "core/color_group.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"


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


/** The centre line (or boundary) of an object as straight segments. */
struct Outline
{
	const Object* object = nullptr;
	const Symbol* symbol = nullptr;
	std::vector<MapCoordF> points;      ///< The vertices, parts one after the other.
	std::vector<bool> segment_follows;  ///< Whether a segment leads from the vertex to the next.
	double half_width = 0.0;
	QRectF extent;                      ///< Of the vertices.
	ColourGroup group = ColourGroup::Other;
	bool contour = false;
	bool cliff = false;
	
	bool build(const Object* object);
	const PathObject* closedPath() const;
};

bool Outline::build(const Object* input)
{
	object = input;
	symbol = input->getSymbol();
	half_width = strokeHalfWidth(symbol);
	group = colourGroupOf(symbol->guessDominantColor());
	contour = isContourClass(symbol);
	cliff = isCliffClass(symbol);
	points.clear();
	segment_follows.clear();
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
		}
		break;
	case Object::Point:
		{
			input->update();
			auto const rect = input->getExtent();
			if (rect.isEmpty())
				return false;
			for (auto const& corner : { rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(), rect.topLeft() })
				points.emplace_back(corner);
			segment_follows.assign(5, true);
			segment_follows.back() = false;
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
	if (segmentsIntersect(a1, a2, b1, b2))
		return 0.0;
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
	return best;
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
	for (auto const& p : a.points)
	{
		for (auto const& q : b.points)
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
	auto best = bound;
	MapCoordF pa, pb;
	for (std::size_t i = 0; i + 1 < a.points.size(); ++i)
	{
		if (!a.segment_follows[i])
			continue;
		auto const& a1 = a.points[i];
		auto const& a2 = a.points[i + 1];
		auto const min_ax = std::min(a1.x(), a2.x());
		auto const max_ax = std::max(a1.x(), a2.x());
		auto const min_ay = std::min(a1.y(), a2.y());
		auto const max_ay = std::max(a1.y(), a2.y());
		if (boxDistance(min_ax, min_ay, max_ax, max_ay, b.extent.left(), b.extent.top(), b.extent.right(), b.extent.bottom()) >= best)
			continue;
		for (std::size_t j = 0; j + 1 < b.points.size(); ++j)
		{
			if (!b.segment_follows[j])
				continue;
			auto const& b1 = b.points[j];
			auto const& b2 = b.points[j + 1];
			if (boxDistance(min_ax, min_ay, max_ax, max_ay, std::min(b1.x(), b2.x()), std::min(b1.y(), b2.y()), std::max(b1.x(), b2.x()), std::max(b1.y(), b2.y())) >= best)
				continue;
			auto const d = segmentDistance(a1, a2, b1, b2, pa, pb);
			if (d < best)
			{
				best = d;
				where = MapCoordF((pa.x() + pb.x()) / 2, (pa.y() + pb.y()) / 2);
				on_a = pa;  // foom: for the line marker
				on_b = pb;
				if (best <= 0.0)
					return 0.0;
			}
		}
	}
	return best;
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

void checkSizes(const Object& object, const LegibilityParams& params, std::vector<Issue>& issues)
{
	if (object.getType() != Object::Path)
		return;
	auto const* path = object.asPath();
	auto const* symbol = object.getSymbol();
	auto const types = symbol->getContainedTypes();
	path->update();
	if (params.minimum_length && (types & Symbol::Line))
	{
		auto const minimum = minimumLineLength(symbol);
		if (minimum > 0.0)
		{
			auto length = 0.0;
			for (auto const& part : path->parts())
				length += part.length();
			if (length < minimum)
				issues.push_back(objectIssue(rule::line_short, object, length, minimum, tr("The line is %1 mm long; the minimum of symbol %2 is %3 mm.").arg(mm(length), symbol->getNumberAsString(), mm(minimum))));
		}
	}
	else if (params.minimum_area && (types & Symbol::Area))
	{
		auto const minimum = minimumAreaSize(symbol);
		if (minimum > 0.0 && !path->parts().empty())
		{
			auto area = std::abs(path->parts().front().calculateArea());
			for (std::size_t i = 1; i < path->parts().size(); ++i)
				area -= std::abs(path->parts()[i].calculateArea());
			area = std::max(0.0, area);
			if (area < minimum)
				issues.push_back(objectIssue(rule::area_small, object, area, minimum, tr("The area is %1 mm²; the minimum of symbol %2 is %3 mm².").arg(mm(area), symbol->getNumberAsString(), mm(minimum))));
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

void checkPart(const MapPart& part, const LegibilityParams& params, std::vector<Issue>& issues)
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
		checkSizes(*object, params, issues);
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
			// the design notes, "Legibility": the larger gap of the two symbols.
			auto const limit = std::max(a_gap, symbolGap(b.symbol, params));
			auto const bound = limit + a.half_width + b.half_width;
			if (boxDistance(a.extent.left(), a.extent.top(), a.extent.right(), a.extent.bottom(), b.extent.left(), b.extent.top(), b.extent.right(), b.extent.bottom()) >= bound)
				continue;
			if (sharesVertex(a, b))
				continue;
			MapCoordF where, on_a, on_b;
			auto const distance = outlineDistance(a, b, bound, where, on_a, on_b);
			if (distance >= bound)
				continue;
			auto const reduced = distance - a.half_width - b.half_width;
			if (reduced <= 0.0)
				continue;  // the outlines touch or overlap
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
	return qRound(100.0 * 0.15 * 15000.0 / scale) / 100.0;
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

std::vector<Issue> checkLegibility(const Map& map, const LegibilityParams& params)
{
	// the design notes, "Legibility".
	std::vector<Issue> issues;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const role = map.getPart(p)->role();
		// the design notes; the markers: the design notes, "Legibility"
		if (role != MapPart::Role::Layout && role != MapPart::Role::Issues)
			checkPart(*map.getPart(p), params, issues);
	}
	return issues;
}


}  // namespace OpenOrienteering
