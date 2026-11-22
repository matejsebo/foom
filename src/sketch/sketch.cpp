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


#include "sketch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include <Qt>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPainter>
#include <QPointF>
#include <QSet>
#include <QTransform>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_part.h"
#include "core/object_groups.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/virtual_path.h"
#include "layout/layout_part.h"
#include "templates/world_file.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Sketch", text);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

struct ColorPreset
{
	const char* name;
	MapColorCmyk cmyk;
};

const ColorPreset color_presets[sketch_color_count] = {
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch red"),     { 0.0f, 1.0f, 1.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch orange"),  { 0.0f, 0.5f, 1.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch yellow"),  { 0.0f, 0.0f, 1.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch green"),   { 1.0f, 0.0f, 1.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch cyan"),    { 1.0f, 0.0f, 0.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch blue"),    { 1.0f, 1.0f, 0.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch magenta"), { 0.0f, 1.0f, 0.0f, 0.0f } },
	{ QT_TRANSLATE_NOOP("OpenOrienteering::Sketch", "Sketch black"),   { 0.0f, 0.0f, 0.0f, 1.0f } },
};

const qreal width_presets[sketch_width_count] = { 0.25, 0.5, 1.0 };

int colorIndex(int color)
{
	return qBound(0, color, sketch_color_count - 1);
}

/** The sketch colour in the map's colour table, added at the top when it is not there. */
const MapColor* sketchColor(Map& map, int color)
{
	auto const name = sketchColorName(color);
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == name)
			return map.getColor(i);
	}
	auto* new_color = new MapColor(name, 0);
	new_color->setCmyk(color_presets[colorIndex(color)].cmyk);
	new_color->setRgbFromCmyk();
	// foom: at the bottom while the sketch is drawn behind the map
	map.addColor(new_color, sketchDrawnBehind(map) ? map.getNumColors() : 0);
	// What is drawn already refers to the colours by their place in the table.
	map.updateAllObjects();
	return new_color;
}

/** True for a colour which is one of the sketch colours. */
bool isSketchColor(const MapColor* color)
{
	for (int i = 0; i < sketch_color_count; ++i)
	{
		if (color->getName() == sketchColorName(i))
			return true;
	}
	return false;
}

/** A private helper symbol of the type and name, or null. */
Symbol* findSketchSymbol(Map& map, Symbol::Type type, const QString& name)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (symbol->getType() == type && symbol->isPrivate() && symbol->isHelperSymbol() && symbol->getName() == name)
			return symbol;
	}
	return nullptr;
}

void addSketchSymbol(Map& map, Symbol* symbol, const QString& name)
{
	symbol->setName(name);
	symbol->setPrivate(true);
	symbol->setIsHelperSymbol(true);
	map.addSymbol(symbol, map.getNumSymbols());
}

/** The index of the sketch part. A new part's undo step goes into the combined step. */
int ensureSketchPart(Map& map, CombinedUndoStep& undo)
{
	auto index = sketchPartIndex(map);
	if (index >= 0)
		return index;
	auto* part = new MapPart(sketchPartName(), &map);
	part->setRole(MapPart::Role::Sketch);
	map.addPart(part, std::size_t(map.getNumParts()));
	undo.push(new MapPartUndoStep(&map, MapPartUndoStep::RemoveMapPart, part));
	return map.findPartIndex(part);
}

/** Adds the objects to the part and records the undo step. */
void addToPart(Map& map, int part_index, const std::vector<Object*>& objects, CombinedUndoStep& undo)
{
	auto const current = map.getCurrentPartIndex();
	map.setCurrentPartIndex(std::size_t(part_index));
	auto* step = new DeleteObjectsUndoStep(&map);
	for (auto* object : objects)
		step->addObject(map.addObject(object, part_index));
	undo.push(step);
	map.setCurrentPartIndex(current);
}

/** Takes the objects out of the part and records the undo step, which then owns them. */
void removeFromPart(Map& map, int part_index, const std::vector<Object*>& objects, CombinedUndoStep& undo)
{
	auto const current = map.getCurrentPartIndex();
	map.setCurrentPartIndex(std::size_t(part_index));
	auto* part = map.getPart(std::size_t(part_index));
	auto* step = new AddObjectsUndoStep(&map);
	for (auto* object : objects)
		step->addObject(part->findObjectIndex(object), object);
	step->removeContainedObjects(false);
	undo.push(step);
	map.setCurrentPartIndex(current);
}

double lengthOf(const std::vector<QPointF>& line)
{
	auto length = 0.0;
	for (std::size_t i = 0; i + 1 < line.size(); ++i)
		length += std::hypot(line[i + 1].x() - line[i].x(), line[i + 1].y() - line[i].y());
	return length;
}

/** The pieces of the line outside the circle. Sets cut when a part of the line lies inside. */
std::vector<std::vector<QPointF>> outsidePieces(const std::vector<QPointF>& line, const QPointF& centre, double radius, bool& cut)
{
	std::vector<std::vector<QPointF>> pieces;
	auto open = false;
	for (std::size_t s = 0; s + 1 < line.size(); ++s)
	{
		auto const p = line[s];
		auto const d = line[s + 1] - p;
		auto const f = p - centre;
		auto const a = QPointF::dotProduct(d, d);
		std::vector<double> stops { 0.0, 1.0 };
		if (a > 0)
		{
			auto const b = 2 * QPointF::dotProduct(f, d);
			auto const c = QPointF::dotProduct(f, f) - radius * radius;
			auto const discriminant = b * b - 4 * a * c;
			if (discriminant > 0)
			{
				auto const root = std::sqrt(discriminant);
				for (auto t : { (-b - root) / (2 * a), (-b + root) / (2 * a) })
				{
					if (t > 0 && t < 1)
						stops.push_back(t);
				}
			}
		}
		std::sort(begin(stops), end(stops));
		for (std::size_t k = 0; k + 1 < stops.size(); ++k)
		{
			auto const from = stops[k];
			auto const to = stops[k + 1];
			if (to - from < 1e-9)
				continue;
			auto const middle = p + d * ((from + to) / 2) - centre;
			if (std::hypot(middle.x(), middle.y()) <= radius)
			{
				cut = true;
				open = false;
				continue;
			}
			if (!open)
			{
				pieces.emplace_back();
				pieces.back().push_back(p + d * from);
				open = true;
			}
			pieces.back().push_back(p + d * to);
		}
	}
	return pieces;
}

}  // namespace


QString sketchPartName()
{
	return tr("Sketch");
}

int sketchPartIndex(const Map& map)
{
	return map.findPartIndexWithRole(MapPart::Role::Sketch);
}

QString sketchColorName(int color)
{
	return tr(color_presets[colorIndex(color)].name);
}

QColor sketchColorRgb(int color)
{
	auto const& cmyk = color_presets[colorIndex(color)].cmyk;
	return QColor::fromCmykF(cmyk.c, cmyk.m, cmyk.y, cmyk.k).toRgb();
}

qreal sketchWidth(int width)
{
	return width_presets[qBound(0, width, sketch_width_count - 1)];
}

const Symbol* sketchSymbol(Map& map, int color, int width)
{
	QString name = sketchColorName(color);
	name += QLatin1Char(' ');
	name += QString::number(sketchWidth(width));
	if (auto const* symbol = findSketchSymbol(map, Symbol::Line, name))
		return symbol;
	auto* symbol = new LineSymbol();
	symbol->setLineWidth(sketchWidth(width));
	symbol->setColor(sketchColor(map, color));
	symbol->setCapStyle(LineSymbol::RoundCap);
	symbol->setJoinStyle(LineSymbol::RoundJoin);
	addSketchSymbol(map, symbol, name);
	return symbol;
}

Object* addSketchStroke(Map& map, const std::vector<MapCoordF>& points, int color, int width)
{
	if (points.size() < 2)
		return nullptr;
	auto const* symbol = sketchSymbol(map, color, width);
	MapCoordVector coords;
	coords.reserve(points.size());
	for (auto const& point : points)
		coords.emplace_back(point);
	auto* stroke = new PathObject(symbol, coords);
	
	auto undo = std::make_unique<CombinedUndoStep>(&map);
	auto const part = ensureSketchPart(map, *undo);
	addToPart(map, part, { stroke }, *undo);
	map.push(undo.release());
	map.setObjectsDirty();
	return stroke;
}

int addSketchNote(Map& map, const MapCoordF& position, const QString& text, int color)
{
	auto const* map_color = sketchColor(map, color);
	QString marker_name = sketchColorName(color);
	marker_name += QLatin1String(" note");
	auto* marker_symbol = findSketchSymbol(map, Symbol::Point, marker_name);
	if (!marker_symbol)
	{
		auto* symbol = new PointSymbol();
		symbol->setInnerRadius(600);
		symbol->setInnerColor(map_color);
		addSketchSymbol(map, symbol, marker_name);
		marker_symbol = symbol;
	}
	auto* text_symbol = findSketchSymbol(map, Symbol::Text, marker_name);
	if (!text_symbol)
	{
		auto* symbol = new TextSymbol();
		symbol->setFontSize(3000);
		symbol->setColor(map_color);
		addSketchSymbol(map, symbol, marker_name);
		text_symbol = symbol;
	}
	
	auto* marker = new PointObject(marker_symbol);
	marker->setPosition(position);
	auto* note = new TextObject(text_symbol);
	note->setAnchorPosition(MapCoordF(position.x() + 1.5, position.y()));
	note->setText(text);
	note->setHorizontalAlignment(TextObject::AlignLeft);
	note->setVerticalAlignment(TextObject::AlignVCenter);
	auto const group = nextObjectGroupId(map);
	marker->setGroup(group);
	note->setGroup(group);
	
	auto undo = std::make_unique<CombinedUndoStep>(&map);
	auto const part = ensureSketchPart(map, *undo);
	addToPart(map, part, { note, marker }, *undo);
	map.setObjectGroupName(group, tr("Note"));
	map.push(undo.release());
	map.setObjectsDirty();
	return group;
}

int eraseSketch(Map& map, const MapCoordF& centre, qreal radius)
{
	return eraseSketchAlong(map, { centre }, radius);
}

int eraseSketchAlong(Map& map, const std::vector<MapCoordF>& trail, qreal radius)
{
	auto const part_index = sketchPartIndex(map);
	if (part_index < 0 || radius <= 0 || trail.empty())
		return 0;
	auto* part = map.getPart(std::size_t(part_index));
	// The centres of the eraser: the trail, filled in at half the radius.
	std::vector<QPointF> centres { QPointF(trail.front()) };
	for (std::size_t i = 1; i < trail.size(); ++i)
	{
		auto const from = centres.back();
		const QPointF to(trail[i]);
		auto const steps = int(std::ceil(std::hypot(to.x() - from.x(), to.y() - from.y()) / (radius / 2)));
		for (int k = 1; k <= steps; ++k)
			centres.push_back(from + (to - from) * (qreal(k) / steps));
	}
	
	std::vector<Object*> removed;
	std::vector<Object*> added;
	QSet<int> groups;
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto* object = part->getObject(i);
		object->update();
		auto const reach = object->getExtent().adjusted(-radius, -radius, radius, radius);
		std::vector<QPointF> in_reach;
		for (auto const& centre : centres)
		{
			if (reach.contains(centre))
				in_reach.push_back(centre);
		}
		if (in_reach.empty())
			continue;
		if (object->getType() != Object::Path)
		{
			// A note goes as a whole.
			removed.push_back(object);
			if (object->group() != 0)
				groups.insert(object->group());
			continue;
		}
		
		auto cut = false;
		std::vector<std::vector<QPointF>> pieces;
		for (auto const& path_part : object->asPath()->parts())
		{
			std::vector<QPointF> line;
			line.reserve(path_part.path_coords.size());
			for (auto const& coord : path_part.path_coords)
				line.push_back(coord.pos);
			std::vector<std::vector<QPointF>> left { line };
			for (auto const& centre : in_reach)
			{
				std::vector<std::vector<QPointF>> next;
				for (auto const& piece : left)
				{
					for (auto& rest : outsidePieces(piece, centre, radius, cut))
						next.push_back(std::move(rest));
				}
				left = std::move(next);
			}
			for (auto& piece : left)
			{
				if (lengthOf(piece) >= 0.05)
					pieces.push_back(std::move(piece));
			}
		}
		if (!cut)
			continue;
		removed.push_back(object);
		// The longest piece stays the same object; the others derive from it.
		std::stable_sort(begin(pieces), end(pieces), [](const std::vector<QPointF>& a, const std::vector<QPointF>& b) {
			return lengthOf(a) > lengthOf(b) + 1e-9;
		});
		for (std::size_t k = 0; k < pieces.size(); ++k)
		{
			MapCoordVector coords;
			coords.reserve(pieces[k].size());
			for (auto const& point : pieces[k])
				coords.emplace_back(point);
			auto* piece = new PathObject(object->getSymbol(), coords);
			piece->setTags(object->tags());
			piece->setGroup(object->group());
			if (k == 0)
				piece->setIdentity(object->uuid(), object->createdTime(), object->modifiedTime());
			else
				piece->setDerivedFrom(object->uuid());
			added.push_back(piece);
		}
	}
	// The other objects of a note which was hit.
	for (int i = 0; i < part->getNumObjects() && !groups.isEmpty(); ++i)
	{
		auto* object = part->getObject(i);
		if (object->getType() != Object::Path && groups.contains(object->group())
		    && std::find(begin(removed), end(removed), object) == end(removed))
			removed.push_back(object);
	}
	if (removed.empty())
		return 0;
	
	map.clearObjectSelection(false);
	auto undo = std::make_unique<CombinedUndoStep>(&map);
	removeFromPart(map, part_index, removed, *undo);
	if (!added.empty())
		addToPart(map, part_index, added, *undo);
	map.push(undo.release());
	map.setObjectsDirty();
	map.emitSelectionChanged();
	return int(removed.size());
}

std::vector<SketchItem> sketchItems(const Map& map)
{
	std::vector<SketchItem> items;
	auto const part_index = sketchPartIndex(map);
	if (part_index < 0)
		return items;
	auto* part = map.getPart(std::size_t(part_index));  // The map hands out its parts for editing.
	
	auto const textOfGroup = [part](int group) -> Object* {
		for (int i = 0; group != 0 && i < part->getNumObjects(); ++i)
		{
			if (part->getObject(i)->group() == group && part->getObject(i)->getType() == Object::Text)
				return part->getObject(i);
		}
		return nullptr;
	};
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto* object = part->getObject(i);
		object->update();
		SketchItem item;
		item.extent = object->getExtent();
		if (object->getType() == Object::Path)
		{
			auto length = 0.0;
			for (auto const& path_part : object->asPath()->parts())
				length += path_part.length();
			item.kind = SketchItem::Stroke;
			item.objects.push_back(object);
			item.label = tr("Stroke, %1 mm").arg(length, 0, 'f', 1);
		}
		else if (object->getType() == Object::Text)
		{
			item.kind = SketchItem::Note;
			item.objects.push_back(object);
			item.label = object->asText()->getText();
			for (int k = 0; object->group() != 0 && k < part->getNumObjects(); ++k)
			{
				auto* other = part->getObject(k);
				if (other != object && other->group() == object->group() && other->getType() != Object::Path)
				{
					other->update();
					item.objects.push_back(other);
					rectIncludeSafe(item.extent, other->getExtent());
				}
			}
		}
		else if (!textOfGroup(object->group()))
		{
			// A marker without a text.
			item.kind = SketchItem::Note;
			item.objects.push_back(object);
		}
		else
		{
			continue;  // listed with its text
		}
		items.push_back(item);
	}
	return items;
}

Object* convertSketchItem(Map& map, const SketchItem& item, const Symbol* target, QString* error)
{
	auto const sketch_index = sketchPartIndex(map);
	if (sketch_index < 0 || item.objects.empty() || !target)
	{
		setError(error, tr("There is nothing to convert."));
		return nullptr;
	}
	auto target_index = int(map.getCurrentPartIndex());
	if (!isMapContent(*map.getPart(std::size_t(target_index))) || target_index == sketch_index)
		target_index = mapContentPartIndex(map);
	if (target_index < 0)
	{
		setError(error, tr("The map has no part for map objects."));
		return nullptr;
	}
	
	auto const* first = item.objects.front();
	Object* converted = nullptr;
	if (item.kind == SketchItem::Stroke)
	{
		if (!(target->getType() & (Symbol::Line | Symbol::Area | Symbol::Combined)))
		{
			setError(error, tr("A stroke becomes a line or an area."));
			return nullptr;
		}
		auto* path = first->asPath()->duplicate();
		if (target->getContainedTypes() & Symbol::Area)
			path->closeAllParts();
		converted = path;
	}
	else if (target->getType() == Symbol::Text && first->getType() == Object::Text)
	{
		converted = first->duplicate();
	}
	else if (target->getType() == Symbol::Point)
	{
		// At the marker when there is one, else where the text is anchored.
		auto position = first->getType() == Object::Text ? first->asText()->getAnchorCoordF()
		                                                 : MapCoordF(first->getRawCoordinateVector().front());
		for (auto const* object : item.objects)
		{
			if (object->getType() == Object::Point)
				position = MapCoordF(object->getRawCoordinateVector().front());
		}
		auto* point = new PointObject();
		point->setPosition(position);
		point->setIdentity(first->uuid(), first->createdTime(), first->modifiedTime());
		if (!item.label.isEmpty())
			point->setTag(QStringLiteral("note"), item.label);
		converted = point;
	}
	else
	{
		setError(error, tr("A note becomes a text or a point."));
		return nullptr;
	}
	converted->setSymbol(target, true);
	converted->setGroup(0);
	
	map.clearObjectSelection(false);
	auto undo = std::make_unique<CombinedUndoStep>(&map);
	removeFromPart(map, sketch_index, item.objects, *undo);
	addToPart(map, target_index, { converted }, *undo);
	map.push(undo.release());
	map.setObjectsDirty();
	map.emitSelectionChanged();
	return converted;
}

bool deleteSketchItem(Map& map, const SketchItem& item)
{
	auto const part_index = sketchPartIndex(map);
	if (part_index < 0)
		return false;
	auto const* part = map.getPart(std::size_t(part_index));
	std::vector<Object*> objects;
	for (auto* object : item.objects)
	{
		if (part->findObjectIndex(object) >= 0)
			objects.push_back(object);
	}
	if (objects.empty())
		return false;
	map.clearObjectSelection(false);
	auto undo = std::make_unique<CombinedUndoStep>(&map);
	removeFromPart(map, part_index, objects, *undo);
	map.push(undo.release());
	map.setObjectsDirty();
	map.emitSelectionChanged();
	return true;
}

bool flattenSketch(const Map& map, const QString& path, double dpi, QString* error)
{
	auto const index = sketchPartIndex(map);
	if (index < 0 || map.getPart(std::size_t(index))->getNumObjects() == 0)
	{
		setError(error, tr("The map has no sketch."));
		return false;
	}
	// The sketch alone, in a map of its own.
	auto sketch = extractPart(map, index);
	auto const extent = sketch->calculateExtent(true, false, nullptr).adjusted(-1, -1, 1, 1);
	auto const scale = dpi / 25.4;  // pixels per mm
	auto const width = qRound(extent.width() * scale);
	auto const height = qRound(extent.height() * scale);
	if (width < 1 || height < 1 || width > 30000 || height > 30000)
	{
		setError(error, tr("The sketch is empty or too large for an image at %1 dpi.").arg(dpi));
		return false;
	}
	QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull())
	{
		setError(error, tr("The sketch is empty or too large for an image at %1 dpi.").arg(dpi));
		return false;
	}
	image.fill(QColor(Qt::transparent));
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(scale, scale);
		painter.translate(-extent.topLeft());
		RenderConfig config = { *sketch, extent, scale, RenderConfig::HelperSymbols, 1.0 };
		sketch->draw(&painter, config);
	}
	auto const dots_per_meter = qRound(dpi / 0.0254);
	image.setDotsPerMeterX(dots_per_meter);
	image.setDotsPerMeterY(dots_per_meter);
	if (!image.save(path, "PNG"))
	{
		setError(error, tr("Cannot write %1.").arg(path));
		return false;
	}
	
	// As PrintWidget::exportWorldFile()
	auto const& georef = map.getGeoreferencing();
	auto const& to_world = georef.mapToProjected();
	auto const top_left = georef.toProjectedCoords(MapCoordF(extent.topLeft()));
	const QTransform pixel_to_world(to_world.m11() / scale, to_world.m21() / scale, 0,
	                                to_world.m12() / scale, to_world.m22() / scale, 0,
	                                top_left.x(), top_left.y());
	if (!WorldFile(pixel_to_world).save(WorldFile::pathForImage(path)))
	{
		setError(error, tr("Cannot write the world file of %1.").arg(path));
		return false;
	}
	return true;
}

bool sketchDrawnBehind(const Map& map)
{
	auto sketch_seen = false;
	auto other_seen = false;
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (isSketchColor(map.getColor(i)))
			sketch_seen = true;
		else if (sketch_seen)
			return false;  // another colour below a sketch colour
		else
			other_seen = true;
	}
	return sketch_seen && other_seen;
}

void setSketchDrawnBehind(Map& map, bool behind)
{
	std::vector<MapColor*> sketch;
	std::vector<MapColor*> others;
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto* color = map.getMapColor(i);
		(isSketchColor(color) ? sketch : others).push_back(color);
	}
	if (sketch.empty())
		return;
	auto order = behind ? others : sketch;
	auto const& rest = behind ? sketch : others;
	order.insert(end(order), begin(rest), end(rest));
	auto changed = false;
	for (int i = 0; i < int(order.size()); ++i)
	{
		if (map.getMapColor(i) != order[std::size_t(i)])
		{
			map.setColor(order[std::size_t(i)], i);
			changed = true;
		}
	}
	if (!changed)
		return;
	// As ColorListWidget::moveColorUp(): what is drawn refers to the colours by their place.
	map.setColorsDirty();
	map.updateAllObjects();
}

std::unique_ptr<Map> printableSketch(const Map& map)
{
	auto const index = sketchPartIndex(map);
	if (index < 0)
		return {};
	auto const* part = map.getPart(std::size_t(index));
	if (!part->isVisible() || part->getNumObjects() == 0)
		return {};
	return extractPart(map, index);
}


}  // namespace OpenOrienteering
