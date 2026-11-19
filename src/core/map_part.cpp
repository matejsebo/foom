/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2017 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "map_part.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <QtGlobal>
#include <QLatin1String>
#include <QObject>
#include <QStringRef>
#include <QTransform>
#include <QXmlStreamReader>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "undo/object_undo.h"
#include "fileformats/xml_file_format.h"  // foom
#include "util/util.h"
#include "util/xml_stream_util.h"


namespace literal
{
	const QLatin1String part("part");
	const QLatin1String name("name");
	const QLatin1String objects("objects");
	const QLatin1String object("object");
	const QLatin1String count("count");
	const QLatin1String role("role");  // foom
	const QLatin1String visible("visible");  // foom
	const QLatin1String string_false("false");  // foom
}



namespace OpenOrienteering {

MapPart::MapPart(const QString& name, Map* map)
: name(name)
, map(map)
{
	Q_ASSERT(map);
	; // nothing else
}

MapPart::~MapPart()
{
	for (Object* object : objects)
	{
		map->unregisterObject(object);
		delete object;
	}
}


void MapPart::setName(const QString& new_name)
{
	name = new_name;
	if (map)
		emit map->mapPartChanged(map->findPartIndex(this), this);
}


// foom
void MapPart::setRole(Role role)
{
	part_role = role;
	auto const index = map->findPartIndex(this);
	if (index >= 0)
		emit map->mapPartChanged(index, this);
}

// foom
void MapPart::setVisible(bool visible)
{
	if (visible == part_visible)
		return;
	part_visible = visible;
	for (auto* object : objects)
	{
		map->setObjectHidden(object, !visible);
		if (visible)
		{
			map->insertRenderablesOfObject(object);
			map->setObjectAreaDirty(object->getExtent());
		}
		else
		{
			map->removeRenderablesOfObject(object, true);
		}
	}
	auto const index = map->findPartIndex(this);
	if (index >= 0)
	{
		map->setHasUnsavedChanges(true);
		emit map->mapPartChanged(index, this);
	}
}

// foom, static
QString MapPart::roleName(Role role)
{
	switch (role)
	{
	case Role::Map:
		return QStringLiteral("map");
	case Role::Layout:
		return QStringLiteral("layout");
	case Role::Sketch:
		return QStringLiteral("sketch");
	case Role::Issues:
		return QStringLiteral("issues");
	case Role::Course:  // the design notes, "OCD files from the course data"
		return QStringLiteral("course");
	}
	return QStringLiteral("map");
}

// foom, static
MapPart::Role MapPart::roleFromName(const QString& name, bool* ok)
{
	for (auto role : { Role::Map, Role::Layout, Role::Sketch, Role::Issues, Role::Course })
	{
		if (name == roleName(role))
		{
			if (ok)
				*ok = true;
			return role;
		}
	}
	if (ok)
		*ok = false;
	return Role::Map;
}



void MapPart::save(QXmlStreamWriter& xml) const
{
	XmlElementWriter part_element(xml, literal::part);
	part_element.writeAttribute(literal::name, name);
	if (XMLFileFormat::foom_revision >= 1)  // foom
	{
		if (part_role != Role::Map)
			part_element.writeAttribute(literal::role, roleName(part_role));
		if (!part_visible)
			part_element.writeAttribute(literal::visible, literal::string_false);
	}
	{
		XmlElementWriter objects_element(xml, literal::objects);
		// foom: OOM cannot read image objects, so they are left out with the foom additions off.
		auto const skip_images = XMLFileFormat::foom_revision < 1;
		auto const num_images = std::size_t(std::count_if(begin(objects), end(objects), [](auto const* object) { return object->getType() == Object::Image; }));
		objects_element.writeAttribute(literal::count, objects.size() - (skip_images ? num_images : 0));
		for (const Object* object : objects)
		{
			if (skip_images && object->getType() == Object::Image)
				continue;
			writeLineBreak(xml);
			object->save(xml);
		}
		writeLineBreak(xml);
	}
}

MapPart* MapPart::load(QXmlStreamReader& xml, Map& map, SymbolDictionary& symbol_dict)
{
	Q_ASSERT(xml.name() == literal::part);
	
	XmlElementReader part_element(xml);
	auto part = new MapPart(part_element.attribute<QString>(literal::name), &map);
	if (part_element.hasAttribute(literal::role))  // foom
		part->part_role = roleFromName(part_element.attribute<QString>(literal::role));
	part->part_visible = part_element.attribute<QString>(literal::visible) != literal::string_false;  // foom
	
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::objects)
		{
			XmlElementReader objects_element(xml);
			
			std::size_t num_objects = objects_element.attribute<std::size_t>(literal::count);
			if (num_objects > 0)
				part->objects.reserve(qMin(num_objects, std::size_t(20000))); // 20000 is not a limit
			
			while (xml.readNextStartElement())
			{
				if (xml.name() == literal::object)
				{
					part->objects.push_back(Object::load(xml, &map, symbol_dict));
					map.registerObject(part->objects.back());
					if (!part->part_visible)  // foom
						map.setObjectHidden(part->objects.back(), true);
					part->spatial_index.add(part->objects.back());
				}
				else
					xml.skipCurrentElement(); // unknown
			}
		}
		else
			xml.skipCurrentElement(); // unknown
	}
	
	return part;
}


bool MapPart::contains(const Object* const object) const
{
	return existsObject([object](auto const* o) { return object == o; });
}

int MapPart::findObjectIndex(const Object* object) const
{
	int size = objects.size();
	for (int i = size - 1; i >= 0; --i)
	{
		if (objects[i] == object)
			return i;
	}
	Q_ASSERT(false);
	return -1;
}

void MapPart::setObject(Object* object, int pos, bool delete_old)
{
	map->unregisterObject(objects[pos]);
	spatial_index.remove(objects[pos]);
	map->removeRenderablesOfObject(objects[pos], true);
	if (delete_old)
		delete objects[pos];
	
	objects[pos] = object;
	map->registerObject(object);
	if (!part_visible)  // foom
		map->setObjectHidden(object, true);
	object->setMap(map);
	object->update();
	spatial_index.enter(object);
	map->setObjectsDirty(); // TODO: remove from here, dirty state handling should be separate
}

void MapPart::addObject(Object* object)
{
	addObject(object, objects.size());
}

void MapPart::addObject(Object* object, int pos)
{
	objects.insert(objects.begin() + pos, object);
	map->registerObject(object);
	if (!part_visible)  // foom
		map->setObjectHidden(object, true);
	object->setMap(map);
	object->update();
	spatial_index.enter(object);
	
	if (objects.size() == 1 && map->getNumObjects() == 1)
		map->updateAllMapWidgets();
}

void MapPart::deleteObject(int pos)
{
	delete releaseObject(pos);
}

bool MapPart::deleteObject(Object* object)
{
	auto object_ptr = releaseObject(object);
	delete object_ptr;

	return object_ptr;
}

Object* MapPart::releaseObject(int pos)
{
	map->removeRenderablesOfObject(objects[pos], true);
	auto object_to_return = objects[pos];
	objects.erase(objects.begin() + pos);
	map->unregisterObject(object_to_return);
	spatial_index.remove(object_to_return);
	
	if (objects.empty() && map->getNumObjects() == 0)
		map->updateAllMapWidgets();

	return object_to_return;
}

Object* MapPart::releaseObject(Object* object)
{
	int size = objects.size();
	for (int i = size - 1; i >= 0; --i)
	{
		if (objects[i] == object)
		{
			return releaseObject(i);
		}
	}
	return nullptr;
}

std::unique_ptr<UndoStep> MapPart::importPart(const MapPart* other, const QHash<const Symbol*, Symbol*>& symbol_map, const QTransform& transform, bool select_new_objects)
{
	if (!part_visible)  // foom: import as visible, then hide again
	{
		setVisible(true);
		auto undo_step = importPart(other, symbol_map, transform, select_new_objects);
		setVisible(false);
		return undo_step;
	}
	
	if (other->getNumObjects() == 0)
		return {};
	
	bool first_objects = map->getNumObjects() == 0;
	auto undo_step = new DeleteObjectsUndoStep(map);
	if (select_new_objects)
		map->clearObjectSelection(false);
	
	objects.reserve(objects.size() + other->objects.size());
	for (const Object* object: other->objects)
	{
		Object* new_object = object->duplicate();
		if (symbol_map.contains(new_object->getSymbol()))
			new_object->setSymbol(symbol_map.value(new_object->getSymbol()), true);
		new_object->transform(transform);
		
		objects.push_back(new_object);
		map->registerObject(new_object);
		new_object->setMap(map);
		new_object->update();
		spatial_index.enter(new_object);
		
		undo_step->addObject((int)objects.size() - 1);
		if (select_new_objects)
			map->addObjectToSelection(new_object, false);
	}
	
	map->setObjectsDirty();
	if (select_new_objects)
	{
		map->emitSelectionChanged();
		map->emitSelectionEdited();		// TODO: is this necessary here?
		                                // Not as long as observers listen to both...
	}
	if (first_objects)
		map->updateAllMapWidgets();
	
	return std::unique_ptr<UndoStep>{undo_step};
}

void MapPart::findObjectsAt(
        const MapCoordF& coord,
        qreal tolerance,
        bool treat_areas_as_paths,
        bool extended_selection,
        bool include_hidden_objects,
        bool include_protected_objects,
        SelectionInfoVector& out ) const
{
	// foom: The candidates come from the spatial index; the tests are unchanged.
	// Object::isPointOnObject() finds objects within the tolerance of their
	// extent, and point objects where the squared distance to the coordinate
	// is below the tolerance, i.e. within sqrt(tolerance).
	auto const radius = qMax(tolerance, std::sqrt(tolerance));
	std::vector<Object*> candidates;
	query(QRectF(coord.x() - radius, coord.y() - radius, 2 * radius, 2 * radius), candidates);
	
	for (Object* object : candidates)
	{
		if (!include_hidden_objects && object->getSymbol()->isHidden())
			continue;
		if (!include_protected_objects && object->getSymbol()->isProtected())
			continue;
		
		object->update();
		int selected_type = object->isPointOnObject(coord, tolerance, treat_areas_as_paths, extended_selection);
		if (selected_type != (int)Symbol::NoSymbol)
			out.emplace_back(selected_type, object);
	}
}

void MapPart::findObjectsAtBox(
        const MapCoordF& corner1,
        const MapCoordF& corner2,
        bool include_hidden_objects,
        bool include_protected_objects,
        std::vector< Object* >& out ) const
{
	auto rect = QRectF(corner1, corner2).normalized();
	
	// foom: The candidates come from the spatial index; the tests are unchanged.
	std::vector<Object*> candidates;
	query(rect, candidates);
	
	for (Object* object : candidates)
	{
		if (!include_hidden_objects && object->getSymbol()->isHidden())
			continue;
		if (!include_protected_objects && object->getSymbol()->isProtected())
			continue;
		
		object->update();
		if (rect.intersects(object->getExtent()) && object->intersectsBox(rect))
			out.push_back(object);
	}
}

void MapPart::query(const QRectF& rect_mm, std::vector<Object*>& out) const
{
	map->updateIndexes();
	spatial_index.query(rect_mm, out);
}

void MapPart::markIndexStale(const Object* object) const
{
	spatial_index.markStale(object);
}

int MapPart::countObjectsInRect(const QRectF& map_coord_rect, bool include_hidden_objects) const
{
	int count = 0;
	for (const Object* object : objects)
	{
		if (object->getSymbol()->isHidden() && !include_hidden_objects)
			continue;
		object->update();
		if (object->getExtent().intersects(map_coord_rect))
			++count;
	}
	return count;
}

QRectF MapPart::calculateExtent(bool include_helper_symbols) const
{
	QRectF rect;
	for (const auto* object : objects)
	{
		if (!object->getSymbol()->isHidden()
		    && (include_helper_symbols || !object->getSymbol()->isHelperSymbol()) )
		{
			object->update();
			rectIncludeSafe(rect, object->getExtent());
		}
	}
	return rect;
}



bool MapPart::existsObject(const std::function<bool(const Object*)>& condition) const
{
	return std::any_of(begin(objects), end(objects), condition);
}


void MapPart::applyOnMatchingObjects(const std::function<void (Object*)>& operation, const std::function<bool (const Object*)>& condition)
{
	std::for_each(objects.rbegin(), objects.rend(), [&operation, &condition](auto object) {
		if (condition(object))
			operation(object);
	});
}


void MapPart::applyOnMatchingObjects(const std::function<void (const Object*)>& operation, const std::function<bool (const Object*)>& condition) const
{
	std::for_each(objects.rbegin(), objects.rend(), [&operation, &condition](auto object) {
		if (condition(object))
			operation(object);
	});
}


void MapPart::applyOnMatchingObjects(const std::function<void (Object*, MapPart*, int)>& operation, const std::function<bool (const Object*)>& condition)
{
	for (auto i = objects.size(); i > 0; )
	{
		--i;
		Object* const object = objects[i];
		if (condition(object))
			operation(object, this, int(i));
	}
}


void MapPart::applyOnAllObjects(const std::function<void (Object*)>& operation)
{
	std::for_each(objects.rbegin(), objects.rend(), operation);
}


void MapPart::applyOnAllObjects(const std::function<void (const Object*)>& operation) const
{
	std::for_each(objects.rbegin(), objects.rend(), operation);
}


void MapPart::applyOnAllObjects(const std::function<void (Object*, MapPart*, int)>& operation)
{
	for (auto i = objects.size(); i > 0; )
	{
		--i;
		operation(objects[i], this, int(i));
	}
}


}  // namespace OpenOrienteering
