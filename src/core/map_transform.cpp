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

#include "core/bookmarks.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
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



namespace {

double distanceToSegment(const MapCoordF& p, const MapCoordF& a, const MapCoordF& b)
{
	auto const ab = b - a;
	auto const length2 = ab.x() * ab.x() + ab.y() * ab.y();
	auto t = length2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length2 : 0.0;
	t = std::max(0.0, std::min(1.0, t));
	return std::hypot(p.x() - a.x() - t * ab.x(), p.y() - a.y() - t * ab.y());
}

double distanceToPerimeter(const std::vector<MapCoordF>& perimeter, const MapCoordF& p)
{
	auto distance = std::numeric_limits<double>::infinity();
	for (std::size_t i = 0; i < perimeter.size(); ++i)
		distance = std::min(distance, distanceToSegment(p, perimeter[i], perimeter[(i + 1) % perimeter.size()]));
	return distance;
}

QPolygonF toPolygon(const std::vector<MapCoordF>& perimeter)
{
	QPolygonF polygon;
	for (auto const& c : perimeter)
		polygon << QPointF(c);
	return polygon;
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

}  // namespace


MapCoordF rubbersheetDisplacement(const RubbersheetParams& params, const MapCoordF& point)
{
	// the design notes, "Rubbersheeting".
	double sum_x = 0, sum_y = 0, sum_w = 0;
	for (auto const& pair : params.pairs)
	{
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
	if (params.decrease_near_perimeter && params.perimeter.size() >= 3)
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


bool rubbersheetMap(Map& map, const RubbersheetParams& params)
{
	if (params.pairs.empty() || params.perimeter.size() < 3)
		return false;
	auto const polygon = toPolygon(params.perimeter);
	auto const inside = [&polygon](const MapCoordF& c) { return polygon.containsPoint(QPointF(c), Qt::OddEvenFill); };
	auto const moved = [&params](const MapCoordF& c) { return c + rubbersheetDisplacement(params, c); };
	
	// The selection's drawing shares the objects' output, which the change replaces:
	// the selection is cleared.
	auto const had_selection = map.getNumSelectedObjects() > 0;
	map.clearObjectSelection(false);
	auto undo_step = std::make_unique<CombinedUndoStep>(&map);
	auto const has_routes = !map.courseData().routes.isEmpty();
	if (has_routes)
		undo_step->push(new MapDataUndoStep(&map, MapDataUndoStep::Courses, MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses)));
	
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(p);
		if (!params.include_layout && part->role() == MapPart::Role::Layout)
			continue;
		ReplaceObjectsUndoStep* step = nullptr;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			auto const* symbol = object->getSymbol();
			if (symbol && symbol->isProtected() && !params.include_protected)
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
				auto* text = object->asText();
				// A text moves as a whole, by the displacement at its anchor.
				auto const anchor = text->getAnchorCoordF();
				if (inside(anchor))
				{
					auto const d = rubbersheetDisplacement(params, anchor);
					text->transform(QTransform::fromTranslate(d.x(), d.y()));
				}
			}
			else if (object->getType() == Object::Path)
			{
				auto* path = object->asPath();
				for (std::size_t k = 0; k < original.size(); ++k)
				{
					auto const c = MapCoordF(original[k]);
					if (!inside(c))
						continue;
					auto coord = original[k];
					auto const target = moved(c);
					coord.setX(target.x());
					coord.setY(target.y());
					path->setCoordinate(k, coord);
				}
			}
			else if (object->getType() == Object::Point)
			{
				object->asPoint()->setPosition(MapCoord(moved(MapCoordF(original.front()))));
			}
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


bool writeRubbersheetPoints(const QString& path, const Map& map, const std::vector<PointPair>& pairs, QString* error)
{
	auto const& georef = map.getGeoreferencing();
	QStringList lines = { systemLine(georef), QStringLiteral("m"), QString::number(pairs.size()) };
	for (auto const& pair : pairs)
	{
		auto const from = georef.toProjectedCoords(pair.map);
		auto const to = georef.toProjectedCoords(pair.reference);
		lines << QStringList{ formatMetres(from.x()), formatMetres(from.y()), formatMetres(to.x()), formatMetres(to.y()),
		                      QStringLiteral("1"), formatMetres(std::hypot(to.x() - from.x(), to.y() - from.y())) }.join(QLatin1Char(';'));
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
		if (fields.size() >= 5 && fields[4].trimmed() == QLatin1String("0"))
			continue;
		pairs.push_back({ georef.toMapCoordF(QPointF(fields[0].toDouble(), fields[1].toDouble())),
		                  georef.toMapCoordF(QPointF(fields[2].toDouble(), fields[3].toDouble())) });
	}
	return pairs;
}


bool writeRubbersheetPerimeter(const QString& path, const Map& map, const std::vector<MapCoordF>& perimeter, QString* error)
{
	auto const& georef = map.getGeoreferencing();
	QStringList lines = { systemLine(georef), QStringLiteral("m"), QStringLiteral("1") };
	for (auto const& c : perimeter)
	{
		auto const p = georef.toProjectedCoords(c);
		lines << formatMetres(p.x()) + QLatin1Char(';') + formatMetres(p.y());
	}
	return writeLines(path, lines, error);
}


std::vector<MapCoordF> readRubbersheetPerimeter(const QString& path, const Map& map, QString* error)
{
	std::vector<MapCoordF> perimeter;
	auto const lines = readLines(path, error);
	auto const& georef = map.getGeoreferencing();
	for (int i = 3; i < lines.size(); ++i)
	{
		auto const fields = lines[i].split(QLatin1Char(';'));
		if (fields.size() < 2)
			continue;
		auto const c = georef.toMapCoordF(QPointF(fields[0].toDouble(), fields[1].toDouble()));
		// A file may repeat the last vertex, where the drawing was finished.
		if (perimeter.empty() || std::hypot(c.x() - perimeter.back().x(), c.y() - perimeter.back().y()) > 1e-9)
			perimeter.push_back(c);
	}
	if (perimeter.size() > 1 && std::hypot(perimeter.front().x() - perimeter.back().x(), perimeter.front().y() - perimeter.back().y()) < 1e-9)
		perimeter.pop_back();
	return perimeter;
}


}  // namespace OpenOrienteering
