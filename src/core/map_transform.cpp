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

#include <cmath>
#include <memory>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "core/bookmarks.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
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
	
	// The objects, part by part.
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (part->getNumObjects() == 0 || (!options.carry && part->role() == MapPart::Role::Layout))
			continue;
		// The rotation of the x axis, cf. Object::rotate().
		auto const angle = std::atan2(-transform.m12(), transform.m11());
		auto* step = new ReplaceObjectsUndoStep(&map);
		step->setPartIndex(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			step->addObject(i, object->duplicate());
			object->transform(transform);
			auto const* symbol = object->getSymbol();
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
	
	map.push(undo_step.release());
	map.updateAllObjects();
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
	auto const n = pairs.size();
	if (n == 0)
		return fit;
	if (n == 1)
	{
		auto const d = pairs.front().reference - pairs.front().map;
		fit.transform = QTransform::fromTranslate(d.x(), d.y());
	}
	else if (n == 2)
	{
		// The similarity as a complex multiplication: z' = s z + t.
		auto const p = pairs[1].map - pairs[0].map;
		auto const q = pairs[1].reference - pairs[0].reference;
		auto const norm = p.x() * p.x() + p.y() * p.y();
		if (!(norm > 1e-12))
			return fit;
		auto const a = (q.x() * p.x() + q.y() * p.y()) / norm;
		auto const b = (q.y() * p.x() - q.x() * p.y()) / norm;
		auto const& m = pairs[0].map;
		auto const& r = pairs[0].reference;
		fit.transform = QTransform(a, b, -b, a, r.x() - (a * m.x() - b * m.y()), r.y() - (b * m.x() + a * m.y()));
	}
	else
	{
		// Least squares, exact for three pairs; centred for precision.
		double mx = 0, my = 0, rx = 0, ry = 0;
		for (auto const& pair : pairs)
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
		for (auto const& pair : pairs)
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


}  // namespace OpenOrienteering
