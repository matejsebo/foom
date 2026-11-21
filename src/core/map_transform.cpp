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

#include "map_transform.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

#include <Qt>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QStringList>
#include <QXmlStreamReader>

#include "core/bookmarks.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "util/util.h"
#include "course/course_data.h"
#include "templates/template.h"
#include "undo/map_data_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"


namespace OpenOrienteering {

namespace {

/// The template's placement as a QTransform from template to map coordinates,
/// as Template::updateTransformationMatrices() builds it.
QTransform toQTransform(const TemplateTransform& t)
{
	auto const c = std::cos(-t.template_rotation);
	auto const s = std::sin(-t.template_rotation);
	auto const sx = t.template_scale_x;
	auto const sy = t.template_scale_y;
	auto const h = t.template_shear;
	return QTransform(sx * c - h * s, sx * s + h * c,
	                  h * c - sy * s, h * s + sy * c,
	                  t.template_x / 1000.0, t.template_y / 1000.0);
}

void transformTemplate(Template& temp, const QTransform& transform)
{
	TemplateTransform placement;
	temp.getTransform(placement);
	temp.setTransform(TemplateTransform::fromQTransform(toQTransform(placement) * transform));
	temp.getOtherTransform(placement);
	temp.setOtherTransform(TemplateTransform::fromQTransform(toQTransform(placement) * transform));
}

MapCoordF mapped(const QTransform& transform, const MapCoordF& point)
{
	return MapCoordF(transform.map(QPointF(point)));
}

}  // namespace



bool transformMap(Map& map, const QTransform& transform, const MapTransformOptions& options)
{
	// the design notes, "Move, stretch and mirror".
	if (!transform.isInvertible() || !transform.isAffine())
		return false;
	
	// The selection's drawing shares the objects' output, which the change replaces:
	// the selection is cleared.
	auto const had_selection = map.getNumSelectedObjects() > 0;
	map.clearObjectSelection(false);
	auto undo_step = std::make_unique<CombinedUndoStep>(&map);
	auto const save = [&map, &undo_step](MapDataUndoStep::Section section) {
		undo_step->push(new MapDataUndoStep(&map, section, MapDataUndoStep::currentXml(map, section)));
	};
	if (options.carry)
	{
		save(MapDataUndoStep::Extents);
		save(MapDataUndoStep::Bookmarks);
		save(MapDataUndoStep::Templates);
	}
	if (!map.courseData().routes.isEmpty())
		save(MapDataUndoStep::Courses);
	if (options.georeferencing)
		save(MapDataUndoStep::Georef);
	
	// The objects, part by part.
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (part->getNumObjects() == 0 || ((!options.carry || options.keep_layout) && part->role() == MapPart::Role::Layout))
			continue;
		// The rotation of the x axis, cf. Object::rotate().
		auto const angle = std::atan2(-transform.m12(), transform.m11());
		auto* step = new ReplaceObjectsUndoStep(&map);
		step->setPartIndex(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			auto const* symbol = object->getSymbol();
			if (std::find(options.fixed_symbols.begin(), options.fixed_symbols.end(), symbol) != options.fixed_symbols.end())
				continue;
			step->addObject(i, object->duplicate());
			object->transform(transform);
			if (options.rotate_orientations && symbol && (symbol->hasRotatableFillPattern() || symbol->isRotatable()))
				object->setRotation(object->getRotation() + angle);
		}
		undo_step->push(step);
	}
	
	if (options.carry)
	{
		auto extents = map.extents();
		for (auto& extent : extents)
			extent.rect_mm = transform.mapRect(extent.rect_mm);
		map.setExtents(extents);
		
		auto bookmarks = map.bookmarks();
		for (auto& bookmark : bookmarks)
			bookmark.center = MapCoord(mapped(transform, MapCoordF(bookmark.center)));
		map.setBookmarks(bookmarks);
		
		// A georeferenced template stays where the world puts it.
		for (int i = 0; i < map.getNumTemplates(); ++i)
		{
			auto* temp = map.getTemplate(i);
			if (temp->isTemplateGeoreferenced())
				continue;
			map.setTemplateAreaDirty(i);
			transformTemplate(*temp, transform);
			map.setTemplateAreaDirty(i);
		}
		for (int i = 0; i < map.getNumClosedTemplates(); ++i)
		{
			auto* temp = map.getClosedTemplate(i);
			if (!temp->isTemplateGeoreferenced())
				transformTemplate(*temp, transform);
		}
	}
	
	if (!map.courseData().routes.isEmpty())
	{
		auto data = map.courseData();
		for (auto& route : data.routes.routes)
		{
			for (auto& point : route.points)
				point = mapped(transform, point);
			for (auto& point : route.via)
				point = mapped(transform, point);
		}
		map.setCourseData(data);
	}
	
	if (options.georeferencing)
		map.setGeoreferencing(*options.georeferencing);
	
	map.push(undo_step.release());
	map.updateAllObjects();
	if (had_selection)
		map.emitSelectionChanged();
	map.setObjectsDirty();
	map.setOtherDirty();
	map.updateAllMapWidgets();
	return true;
}


QTransform mapMoveTransform(const Map& map, double east_m, double north_m)
{
	auto const& georef = map.getGeoreferencing();
	auto const reference = MapCoordF(georef.getMapRefPoint());
	auto const projected = georef.toProjectedCoords(reference);
	auto const target = georef.toMapCoordF(projected + QPointF(east_m, north_m));
	return QTransform::fromTranslate(target.x() - reference.x(), target.y() - reference.y());
}


QTransform mapStretchTransform(double factor_x, double factor_y, bool mirror_horizontally, bool mirror_vertically)
{
	return QTransform::fromScale(mirror_horizontally ? -factor_x : factor_x, mirror_vertically ? -factor_y : factor_y);
}


PairFit fitPointPairs(const std::vector<PointPair>& pairs)
{
	// the design notes, "Affine by point pairs".
	PairFit fit;
	std::vector<PointPair> used;
	std::copy_if(pairs.begin(), pairs.end(), std::back_inserter(used), [](const PointPair& pair) { return pair.used; });
	auto const n = used.size();
	if (n == 0)
		return fit;
	if (n == 1)
	{
		auto const d = used.front().reference - used.front().map;
		fit.transform = QTransform::fromTranslate(d.x(), d.y());
	}
	else if (n == 2)
	{
		// The similarity as a complex multiplication: z' = s z + t.
		auto const p = used[1].map - used[0].map;
		auto const q = used[1].reference - used[0].reference;
		auto const norm = p.x() * p.x() + p.y() * p.y();
		if (!(norm > 1e-12))
			return fit;
		auto const a = (q.x() * p.x() + q.y() * p.y()) / norm;
		auto const b = (q.y() * p.x() - q.x() * p.y()) / norm;
		auto const& m = used[0].map;
		auto const& r = used[0].reference;
		fit.transform = QTransform(a, b, -b, a, r.x() - (a * m.x() - b * m.y()), r.y() - (b * m.x() + a * m.y()));
	}
	else
	{
		// Least squares, exact for three pairs; centred for precision.
		double mx = 0, my = 0, rx = 0, ry = 0;
		for (auto const& pair : used)
		{
			mx += pair.map.x();
			my += pair.map.y();
			rx += pair.reference.x();
			ry += pair.reference.y();
		}
		mx /= n;
		my /= n;
		rx /= n;
		ry /= n;
		double sxx = 0, sxy = 0, syy = 0, sxu = 0, syu = 0, sxv = 0, syv = 0;
		for (auto const& pair : used)
		{
			auto const x = pair.map.x() - mx;
			auto const y = pair.map.y() - my;
			auto const u = pair.reference.x() - rx;
			auto const v = pair.reference.y() - ry;
			sxx += x * x;
			sxy += x * y;
			syy += y * y;
			sxu += x * u;
			syu += y * u;
			sxv += x * v;
			syv += y * v;
		}
		auto const det = sxx * syy - sxy * sxy;
		if (!(det > 1e-12 * (sxx + syy) * (sxx + syy)))
			return fit;   // collinear
		auto const a = (sxu * syy - syu * sxy) / det;
		auto const b = (syu * sxx - sxu * sxy) / det;
		auto const d = (sxv * syy - syv * sxy) / det;
		auto const e = (syv * sxx - sxv * sxy) / det;
		fit.transform = QTransform(a, d, b, e, rx - a * mx - b * my, ry - d * mx - e * my);
	}
	for (auto const& pair : pairs)
	{
		auto const p = fit.transform.map(QPointF(pair.map));
		fit.residuals.push_back(std::hypot(p.x() - pair.reference.x(), p.y() - pair.reference.y()));
	}
	fit.ok = true;
	return fit;
}



namespace {

double distanceToSegment(const MapCoordF& p, const MapCoordF& a, const MapCoordF& b)
{
	auto const ab = b - a;
	auto const length2 = ab.x() * ab.x() + ab.y() * ab.y();
	auto t = length2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length2 : 0.0;
	t = std::max(0.0, std::min(1.0, t));
	return std::hypot(p.x() - a.x() - t * ab.x(), p.y() - a.y() - t * ab.y());
}

double distanceToPerimeter(const std::vector<std::vector<MapCoordF>>& perimeter, const MapCoordF& p)
{
	auto distance = std::numeric_limits<double>::infinity();
	for (auto const& polygon : perimeter)
	{
		for (std::size_t i = 0; i < polygon.size(); ++i)
			distance = std::min(distance, distanceToSegment(p, polygon[i], polygon[(i + 1) % polygon.size()]));
	}
	return distance;
}

/// The polygons with three or more vertices.
std::vector<QPolygonF> toPolygons(const std::vector<std::vector<MapCoordF>>& perimeter)
{
	std::vector<QPolygonF> polygons;
	for (auto const& vertices : perimeter)
	{
		if (vertices.size() < 3)
			continue;
		QPolygonF polygon;
		for (auto const& c : vertices)
			polygon << QPointF(c);
		polygons.push_back(polygon);
	}
	return polygons;
}

bool samePoint(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y()) < 1e-9;
}

/// The first line of the file names the system, by a name of its own.
QString systemLine(const Georeferencing& georef)
{
	auto const spec = georef.getProjectedCRSSpec();
	return spec.isEmpty() ? georef.getProjectedCRSName() : spec;
}

QString formatMetres(double value)
{
	return QString::number(value, 'f', 2);
}

bool writeLines(const QString& path, const QStringList& lines, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write((lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8()) < 0)
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}

QStringList readLines(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = file.errorString();
		return {};
	}
	auto lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
	for (auto& line : lines)
		line = line.trimmed();
	return lines;
}

/**
 * Moves the vertices inside the region by the displacement at them, curve handles
 * with their vertices and texts as a whole by their anchors, for rubbersheeting
 * and the local transformation; the stored routes follow; one undo step.
 */
bool warpMap(Map& map, const std::function<bool (const MapCoordF&)>& inside,
             const std::function<MapCoordF (const MapCoordF&)>& displacement,
             bool include_protected, bool include_layout,
             bool handles_follow_vertices = true,
             const std::function<double (const MapCoordF&)>& rotation = {},
             const std::vector<UndoStep*>& data_steps = {},
             WarpReport* report = nullptr)
{
	auto const moved = [&displacement](const MapCoordF& c) { return c + displacement(c); };
	double sum = 0;
	WarpReport counted;
	auto const record = [&counted, &sum](const MapCoordF& d) {
		auto const length = std::hypot(d.x(), d.y());
		counted.largest = std::max(counted.largest, length);
		sum += length;
		++counted.count;
	};
	
	// The selection's drawing shares the objects' output, which the change replaces:
	// the selection is cleared.
	auto const had_selection = map.getNumSelectedObjects() > 0;
	map.clearObjectSelection(false);
	auto undo_step = std::make_unique<CombinedUndoStep>(&map);
	for (auto* step : data_steps)
		undo_step->push(step);
	auto const has_routes = !map.courseData().routes.isEmpty();
	if (has_routes)
		undo_step->push(new MapDataUndoStep(&map, MapDataUndoStep::Courses, MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses)));
	
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (!include_layout && part->role() == MapPart::Role::Layout)
			continue;
		ReplaceObjectsUndoStep* step = nullptr;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			auto const* symbol = object->getSymbol();
			if (symbol && symbol->isProtected() && !include_protected)
				continue;
			auto const original = object->getRawCoordinateVector();
			auto const touches = std::any_of(original.begin(), original.end(), [&inside](const MapCoord& c) { return inside(MapCoordF(c)); });
			if (!touches)
				continue;
			if (!step)
			{
				step = new ReplaceObjectsUndoStep(&map);
				step->setPartIndex(p);
			}
			step->addObject(i, object->duplicate());
			// asText() and its kin do not check the type.
			if (object->getType() == Object::Text)
			{
				// A text moves as a whole, by the displacement at its anchor.
				auto const anchor = object->asText()->getAnchorCoordF();
				if (inside(anchor))
				{
					auto const d = displacement(anchor);
					record(d);
					object->transform(QTransform::fromTranslate(d.x(), d.y()));
				}
			}
			else if (object->getType() == Object::Path)
			{
				// A curve's first handle belongs to the vertex before it, the second to the vertex after it.
				std::vector<std::size_t> vertex(original.size());
				for (std::size_t k = 0; k < original.size(); ++k)
					vertex[k] = k;
				for (std::size_t k = 0; handles_follow_vertices && k + 3 < original.size(); ++k)
				{
					if (original[k].isCurveStart())
					{
						vertex[k + 1] = k;
						vertex[k + 2] = k + 3;
						k += 2;
					}
				}
				auto* path = object->asPath();
				for (std::size_t k = 0; k < original.size(); ++k)
				{
					auto const at = MapCoordF(original[vertex[k]]);
					if (!inside(at))
						continue;
					auto const d = displacement(at);
					record(d);
					auto coord = original[k];
					coord.setX(coord.x() + d.x());
					coord.setY(coord.y() + d.y());
					path->setCoordinate(k, coord);
				}
			}
			else if (object->getType() == Object::Point)
			{
				auto const position = MapCoordF(original.front());
				if (inside(position))
				{
					auto const d = displacement(position);
					record(d);
					object->asPoint()->setPosition(MapCoord(position + d));
				}
			}
			// Orientations turn by the local rotation, as Object::rotate() does.
			if (rotation && symbol && (symbol->hasRotatableFillPattern() || symbol->isRotatable()) && !original.empty() && inside(MapCoordF(original.front())))
				object->setRotation(object->getRotation() + rotation(MapCoordF(original.front())));
		}
		if (step)
			undo_step->push(step);
	}
	
	if (has_routes)
	{
		auto data = map.courseData();
		for (auto& route : data.routes.routes)
		{
			for (auto& point : route.points)
				if (inside(point))
					point = moved(point);
			for (auto& point : route.via)
				if (inside(point))
					point = moved(point);
		}
		map.setCourseData(data);
	}
	
	if (report)
	{
		counted.mean = counted.count > 0 ? sum / counted.count : 0.0;
		*report = counted;
	}
	if (undo_step->getNumSubSteps() > 0)
		map.push(undo_step.release());
	map.updateAllObjects();
	if (had_selection)
		map.emitSelectionChanged();
	map.setObjectsDirty();
	map.setOtherDirty();
	map.updateAllMapWidgets();
	return true;
}

}  // namespace


MapCoordF rubbersheetDisplacement(const RubbersheetParams& params, const MapCoordF& point)
{
	// the design notes, "Rubbersheeting".
	double sum_x = 0, sum_y = 0, sum_w = 0;
	for (auto const& pair : params.pairs)
	{
		if (!pair.used)
			continue;
		auto const d = pair.reference - pair.map;
		auto const product = std::hypot(point.x() - pair.map.x(), point.y() - pair.map.y())
		                     * std::hypot(point.x() - pair.reference.x(), point.y() - pair.reference.y());
		if (!(product > 1e-24))
			return MapCoordF(d);
		auto const w = 1 / product;
		sum_x += w * d.x();
		sum_y += w * d.y();
		sum_w += w;
	}
	if (params.decrease_near_perimeter && !toPolygons(params.perimeter).empty())
	{
		auto const e = distanceToPerimeter(params.perimeter, point);
		if (!(e > 1e-12))
			return MapCoordF(0, 0);
		sum_w += 4 / (e * e);
	}
	if (!(sum_w > 0))
		return MapCoordF(0, 0);
	return MapCoordF(sum_x / sum_w, sum_y / sum_w);
}


bool rubbersheetMap(Map& map, const RubbersheetParams& params, WarpReport* report)
{
	auto const polygons = toPolygons(params.perimeter);
	if (polygons.empty() || std::none_of(params.pairs.begin(), params.pairs.end(), [](const PointPair& pair) { return pair.used; }))
		return false;
	auto const inside = [&polygons](const MapCoordF& c) {
		return std::any_of(polygons.begin(), polygons.end(), [&c](const QPolygonF& polygon) { return polygon.containsPoint(QPointF(c), Qt::OddEvenFill); });
	};
	return warpMap(map, inside,
	               [&params](const MapCoordF& c) { return rubbersheetDisplacement(params, c); },
	               params.include_protected, params.include_layout, true, {}, {}, report);
}


MapCoordF localWarpDisplacement(const LocalWarpParams& params, const MapCoordF& point)
{
	// the design notes, "Local transformation".
	if (!(params.radius > 0))
		return MapCoordF(0, 0);
	auto const f = std::max(0.0, 1 - std::hypot(point.x() - params.center.x(), point.y() - params.center.y()) / params.radius);
	auto const d = params.target - params.center;
	return MapCoordF(d.x() * f, d.y() * f);
}


bool localWarpMap(Map& map, const LocalWarpParams& params)
{
	// The mouse must be released within the radius.
	auto const drag = std::hypot(params.target.x() - params.center.x(), params.target.y() - params.center.y());
	if (!(params.radius > 0) || !(drag < params.radius))
		return false;
	return warpMap(map,
	               [&params](const MapCoordF& c) { return std::hypot(c.x() - params.center.x(), c.y() - params.center.y()) < params.radius; },
	               [&params](const MapCoordF& c) { return localWarpDisplacement(params, c); },
	               params.include_protected, params.include_layout);
}


namespace {

/// The georeferencing in the target system with the same reference point, scale and grivation.
bool reprojectedGeoreferencing(const Georeferencing& old_georef, Georeferencing& next,
                               const QString& id, const QString& spec, const std::vector<QString>& params)
{
	if (old_georef.getState() != Georeferencing::Geospatial)
		return false;
	next = old_georef;
	if (!next.setProjectedCRS(id, spec, params) || next.getState() != Georeferencing::Geospatial)
		return false;
	bool ok = false;
	auto const reference = next.toProjectedCoords(old_georef.getGeographicRefPoint(), &ok);
	if (!ok)
		return false;
	next.setProjectedRefPoint(reference, false, false);
	next.setGrivation(old_georef.getGrivation());
	return true;
}

}  // namespace


QPointF proposedReprojectOffset(const Georeferencing& georef, const QString& id, const QString& spec, const std::vector<QString>& params, bool* ok)
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map".
	Georeferencing next(georef);
	bool ok_old = false, ok_new = false;
	QPointF origin;
	if (reprojectedGeoreferencing(georef, next, id, spec, params))
	{
		auto const lat_lon = georef.toGeographicCoords(MapCoordF(0.0, 0.0), &ok_old);
		if (ok_old)
			origin = next.toProjectedCoords(lat_lon, &ok_new);
	}
	if (ok)
		*ok = ok_old && ok_new;
	return { std::round(origin.x() / 1000.0) * 1000.0, std::round(origin.y() / 1000.0) * 1000.0 };
}


bool reprojectMap(Map& map, const QString& id, const QString& spec, const std::vector<QString>& params, const QPointF* offset)
{
	// the design notes, "Change of coordinate system".
	Georeferencing const old_georef(map.getGeoreferencing());
	Georeferencing next(old_georef);
	if (!reprojectedGeoreferencing(old_georef, next, id, spec, params))
		return false;
	if (offset)
	{
		// The origin to the offset, in whole map units as in centerMap().
		auto const shift = -MapCoord(next.toMapCoordF(*offset));
		next.setMapRefPoint(next.getMapRefPoint() + shift);
	}
	
	// Where the new georeferencing puts the old geographic position.
	auto const mapping = [&old_georef, &next](const MapCoordF& p) {
		bool ok_old = false, ok_new = false;
		auto const lat_lon = old_georef.toGeographicCoords(p, &ok_old);
		auto const q = next.toMapCoordF(lat_lon, &ok_new);
		return ok_old && ok_new ? q : p;
	};
	auto const displacement = [&mapping](const MapCoordF& p) { return mapping(p) - p; };
	auto const rotation = [&mapping](const MapCoordF& p) {
		auto const a = mapping(p);
		auto const b = mapping(MapCoordF(p.x() + 1, p.y()));
		return std::atan2(-(b.y() - a.y()), b.x() - a.x());
	};
	
	std::vector<UndoStep*> data_steps;
	for (auto section : { MapDataUndoStep::Georef, MapDataUndoStep::Extents, MapDataUndoStep::Bookmarks, MapDataUndoStep::Templates })
		data_steps.push_back(new MapDataUndoStep(&map, section, MapDataUndoStep::currentXml(map, section)));
	
	// The objects, all of them.
	warpMap(map, [](const MapCoordF&) { return true; }, displacement, true, true, false, rotation, data_steps);
	
	auto extents = map.extents();
	for (auto& extent : extents)
	{
		QRectF box;
		for (auto const& corner : { extent.rect_mm.topLeft(), extent.rect_mm.topRight(), extent.rect_mm.bottomLeft(), extent.rect_mm.bottomRight() })
			rectIncludeSafe(box, QPointF(mapping(MapCoordF(corner))));
		extent.rect_mm = box;
	}
	map.setExtents(extents);
	auto bookmarks = map.bookmarks();
	for (auto& bookmark : bookmarks)
		bookmark.center = MapCoord(mapping(MapCoordF(bookmark.center)));
	map.setBookmarks(bookmarks);
	
	// Templates which are not georeferenced, by the affine approximation at the map's centre.
	auto const centre = MapCoordF(map.calculateExtent().center());
	auto const fit = fitPointPairs({ { centre, mapping(centre) },
	                                 { MapCoordF(centre.x() + 100, centre.y()), mapping(MapCoordF(centre.x() + 100, centre.y())) },
	                                 { MapCoordF(centre.x(), centre.y() + 100), mapping(MapCoordF(centre.x(), centre.y() + 100)) } });
	if (fit.ok)
	{
		for (int i = 0; i < map.getNumTemplates(); ++i)
		{
			auto* temp = map.getTemplate(i);
			if (temp->isTemplateGeoreferenced())
				continue;
			map.setTemplateAreaDirty(i);
			transformTemplate(*temp, fit.transform);
			map.setTemplateAreaDirty(i);
		}
		for (int i = 0; i < map.getNumClosedTemplates(); ++i)
		{
			auto* temp = map.getClosedTemplate(i);
			if (!temp->isTemplateGeoreferenced())
				transformTemplate(*temp, fit.transform);
		}
	}
	
	map.setGeoreferencing(next);
	map.setOtherDirty();
	map.updateAllMapWidgets();
	return true;
}


QTransform mapRotationTransform(const MapCoordF& centre, double degrees)
{
	// QTransform::rotate() turns clockwise on screen, where y grows south.
	QTransform transform;
	transform.translate(centre.x(), centre.y());
	transform.rotate(-degrees);
	transform.translate(-centre.x(), -centre.y());
	return transform;
}

double magneticNorthRotation(const Georeferencing& georef, double declination)
{
	Georeferencing next(georef);
	next.setDeclination(declination);
	return next.getGrivation() - georef.getGrivation();
}

bool rotateMapToMagneticNorth(Map& map, double declination, MapTransformOptions options)
{
	// the design notes, "Rotation to magnetic north and centring".
	Georeferencing const old_georef(map.getGeoreferencing());
	Georeferencing next(old_georef);
	next.setDeclination(declination);
	auto const degrees = next.getGrivation() - old_georef.getGrivation();
	options.rotate_orientations = true;
	options.georeferencing = &next;
	return transformMap(map, mapRotationTransform(MapCoordF(old_georef.getMapRefPoint()), degrees), options);
}

QPointF mapOffset(const Map& map)
{
	return map.getGeoreferencing().toProjectedCoords(MapCoordF(0.0, 0.0));
}

QPointF proposedMapOffset(const Map& map)
{
	// The bounding box of the coordinates.
	QRectF box;
	map.applyOnAllObjects([&box](const Object* object) {
		for (auto const& coord : object->getRawCoordinateVector())
			rectIncludeSafe(box, QPointF(MapCoordF(coord)));
	});
	auto const centre = map.getGeoreferencing().toProjectedCoords(MapCoordF(box.center()));
	return { std::round(centre.x() / 1000.0) * 1000.0, std::round(centre.y() / 1000.0) * 1000.0 };
}

bool centerMap(Map& map, const QPointF& offset)
{
	// the design notes, "Rotation to magnetic north and centring".
	Georeferencing const old_georef(map.getGeoreferencing());
	// Whole map units, so that the reference point moves exactly as the objects do.
	auto const shift = -MapCoord(old_georef.toMapCoordF(offset));
	Georeferencing next(old_georef);
	next.setMapRefPoint(old_georef.getMapRefPoint() + shift);
	MapTransformOptions options;
	options.georeferencing = &next;
	return transformMap(map, QTransform::fromTranslate(MapCoordF(shift).x(), MapCoordF(shift).y()), options);
}


bool writeRubbersheetPoints(const QString& path, const Map& map, const std::vector<PointPair>& pairs, QString* error)
{
	auto const& georef = map.getGeoreferencing();
	QStringList lines = { systemLine(georef), QStringLiteral("m"), QString::number(pairs.size()) };
	for (auto const& pair : pairs)
	{
		auto const from = georef.toProjectedCoords(pair.map);
		auto const to = georef.toProjectedCoords(pair.reference);
		lines << QStringList{ formatMetres(from.x()), formatMetres(from.y()), formatMetres(to.x()), formatMetres(to.y()),
		                      pair.used ? QStringLiteral("1") : QStringLiteral("0"),
		                      formatMetres(std::hypot(to.x() - from.x(), to.y() - from.y())) }.join(QLatin1Char(';'));
	}
	return writeLines(path, lines, error);
}


std::vector<PointPair> readRubbersheetPoints(const QString& path, const Map& map, QString* error)
{
	std::vector<PointPair> pairs;
	auto const lines = readLines(path, error);
	auto const& georef = map.getGeoreferencing();
	for (int i = 3; i < lines.size(); ++i)
	{
		auto const fields = lines[i].split(QLatin1Char(';'));
		if (fields.size() < 4)
			continue;
		pairs.push_back({ georef.toMapCoordF(QPointF(fields[0].toDouble(), fields[1].toDouble())),
		                  georef.toMapCoordF(QPointF(fields[2].toDouble(), fields[3].toDouble())),
		                  !(fields.size() >= 5 && fields[4].trimmed() == QLatin1String("0")) });
	}
	return pairs;
}


bool writeRubbersheetPerimeter(const QString& path, const Map& map, const std::vector<std::vector<MapCoordF>>& perimeter, QString* error)
{
	auto const& georef = map.getGeoreferencing();
	QStringList lines = { systemLine(georef), QStringLiteral("m"), QString::number(perimeter.size()) };
	for (auto const& polygon : perimeter)
	{
		// Each polygon closed by its first vertex (a file may repeat a vertex where the drawing ended).
		auto vertices = polygon;
		if (!vertices.empty() && !samePoint(vertices.front(), vertices.back()))
			vertices.push_back(vertices.front());
		for (auto const& c : vertices)
		{
			auto const p = georef.toProjectedCoords(c);
			lines << formatMetres(p.x()) + QLatin1Char(';') + formatMetres(p.y());
		}
	}
	return writeLines(path, lines, error);
}


std::vector<std::vector<MapCoordF>> readRubbersheetPerimeter(const QString& path, const Map& map, QString* error)
{
	std::vector<std::vector<MapCoordF>> perimeter;
	std::vector<MapCoordF> polygon;
	auto const lines = readLines(path, error);
	auto const& georef = map.getGeoreferencing();
	for (int i = 3; i < lines.size(); ++i)
	{
		auto const fields = lines[i].split(QLatin1Char(';'));
		if (fields.size() < 2)
			continue;
		auto const c = georef.toMapCoordF(QPointF(fields[0].toDouble(), fields[1].toDouble()));
		// A file may repeat the last vertex, where the drawing was finished.
		if (!polygon.empty() && samePoint(c, polygon.back()))
			continue;
		if (polygon.size() >= 3 && samePoint(c, polygon.front()))
		{
			// The polygon is closed; the next vertex starts another (Proposed).
			perimeter.push_back(polygon);
			polygon.clear();
			continue;
		}
		polygon.push_back(c);
	}
	if (polygon.size() >= 3)
		perimeter.push_back(polygon);
	return perimeter;
}


GeomagneticField parseGeomagneticReply(const QByteArray& xml)
{
	// the design notes, "Measured declination data".
	GeomagneticField field;
	QXmlStreamReader reader(xml);
	QString section;
	while (!reader.atEnd())
	{
		auto const token = reader.readNext();
		if (token != QXmlStreamReader::StartElement)
			continue;
		auto const name = reader.name();
		if (name == QLatin1String("field-value") || name == QLatin1String("secular-variation"))
		{
			section = name.toString();
			continue;
		}
		bool ok = false;
		if (section == QLatin1String("field-value") && name == QLatin1String("declination"))
		{
			field.declination = reader.readElementText().toDouble(&ok);
			field.has_declination = ok;
		}
		else if (section == QLatin1String("field-value") && name == QLatin1String("horizontal-intensity"))
		{
			field.horizontal_intensity = reader.readElementText().toDouble(&ok);
			field.has_horizontal_intensity = ok;
		}
		else if (section == QLatin1String("secular-variation") && name == QLatin1String("declination"))
		{
			auto const units = reader.attributes().value(QLatin1String("units")).toString();
			auto const value = reader.readElementText().toDouble(&ok);
			// The service gives arcmin per year.
			field.yearly_change = units.startsWith(QLatin1String("deg")) ? value : value / 60.0;
			field.has_yearly_change = ok;
		}
	}
	return field;
}


double declinationUncertainty(double horizontal_intensity)
{
	// The World Magnetic Model 2025 error model.
	if (!(horizontal_intensity > 0))
		return std::numeric_limits<double>::infinity();
	auto const h = 5417.0 / horizontal_intensity;
	return std::sqrt(0.26 * 0.26 + h * h);
}


}  // namespace OpenOrienteering
