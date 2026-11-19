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


#include "course_compose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <QPointF>
#include <QUuid>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"
#include "course/course_edit.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "course/course_sync.h"
#include "undo/map_data_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

namespace OpenOrienteering {

namespace {

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
	auto const ab = b - a;
	auto const ap = p - a;
	auto const length_sq = ab.x() * ab.x() + ab.y() * ab.y();
	auto t = length_sq > 0.0 ? (ap.x() * ab.x() + ap.y() * ab.y()) / length_sq : 0.0;
	t = std::max(0.0, std::min(1.0, t));
	auto const off = p - (a + ab * t);
	return std::hypot(off.x(), off.y());
}

bool names(const CourseItem& item, const QString& code)
{
	return item.namesObject() && item.code == code;
}

/// The kinds which have a place in the map and can stand in a course.
bool hasPlace(CourseObject::Kind kind)
{
	return kind == CourseObject::Start || kind == CourseObject::Control
	       || kind == CourseObject::Finish || kind == CourseObject::MarkedRoute;
}

Symbol::CourseRole roleOf(CourseObject::Kind kind)
{
	switch (kind)
	{
	case CourseObject::Start:   return Symbol::CourseStart;
	case CourseObject::Finish:  return Symbol::CourseFinish;
	default:                    return Symbol::CourseControl;
	}
}

/// The index of the part which holds the object, or -1.
int partOf(const Map& map, const Object* object)
{
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		if (map.getPart(std::size_t(p))->findObjectIndex(object) >= 0)
			return p;
	}
	return -1;
}

/// Selects the object alone, when it is in the current part.
void select(Map& map, Object* object)
{
	if (!object || partOf(map, object) != int(map.getCurrentPartIndex()))
		return;
	map.clearObjectSelection(false);
	map.addObjectToSelection(object, true);
}

}  // namespace



CourseComposer::CourseComposer(Map& map)
: map(map)
{
	// nothing else
}


void CourseComposer::setCourse(const QString& name)
{
	if (name == course_name)
		return;
	course_name = name;
	insert_after = -1;
	made_control = false;
	items_left.clear();
}


const Course* CourseComposer::findCourse() const
{
	return map.courseData().findCourse(course_name);
}


CourseComposer::Reason CourseComposer::refusal() const
{
	auto const* course = findCourse();
	if (!course)
		return NoCourse;
	auto const has_variation = std::any_of(begin(course->items), end(course->items), [](const CourseItem& item) {
		return isVariationItem(item.type);
	});
	return has_variation ? HasVariation : NoReason;
}


bool CourseComposer::isEmpty() const
{
	auto const* course = findCourse();
	return !course || course->items.empty();
}


bool CourseComposer::isComplete() const
{
	auto const* course = findCourse();
	if (!course)
		return false;
	for (auto item = course->items.rbegin(); item != course->items.rend(); ++item)
	{
		if (item->type != CourseItem::TextBlock)
			return item->type == CourseItem::Finish;
	}
	return false;
}


CourseComposer::Target CourseComposer::targetAt(const MapCoordF& pos, double reach) const
{
	Target target;
	auto const& data = map.courseData();
	
	auto nearest = 0.0;
	for (auto const& course_object : data.objects)
	{
		if (!hasPlace(course_object.kind) || course_object.object.isNull())
			continue;
		auto const* object = map.findObject(course_object.object);
		if (!object || !object->getSymbol() || object->getSymbol()->isHidden())
			continue;
		
		auto distance = 0.0;
		auto limit = reach;
		if (object->getType() == Object::Point)
		{
			auto const off = QPointF(pos) - QPointF(object->asPoint()->getCoordF());
			distance = std::hypot(off.x(), off.y());
			limit = std::max(reach, courseSymbolExtent(object->getSymbol()));
		}
		else if (object->getType() == Object::Path)
		{
			auto const* path = object->asPath();
			path->update();
			if (path->getRawCoordinateVector().empty())
				continue;
			distance = std::sqrt(path->findClosestPointTo(pos).distance_squared);
		}
		else
		{
			continue;
		}
		if (distance <= limit && (target.what == Target::Nothing || distance < nearest))
		{
			target.what = Target::Object;
			target.code = course_object.code;
			nearest = distance;
		}
	}
	if (target.what == Target::Object)
		return target;
	
	auto const* course = findCourse();
	if (!course)
		return target;
	auto const stops = courseStops(map, data, *course);
	for (std::size_t i = 1; i < stops.size(); ++i)
	{
		if (stops[i].after_break)
			continue;
		auto const distance = distanceToSegment(pos, stops[i - 1].exit, stops[i].entry);
		if (distance <= reach && (target.what == Target::Nothing || distance < nearest))
		{
			target.what = Target::Leg;
			target.item = stops[i - 1].item;
			nearest = distance;
		}
	}
	return target;
}


const Symbol* CourseComposer::symbolFor(CourseObject::Kind kind) const
{
	auto const role = roleOf(kind);
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Point && symbol->courseRole() == role && !symbol->isHidden())
			return symbol;
	}
	return nullptr;
}


bool CourseComposer::besideItself(const Course& course, int index, const QString& code) const
{
	auto const& items = course.items;
	return (index > 0 && names(items[std::size_t(index - 1)], code))
	       || (std::size_t(index) < items.size() && names(items[std::size_t(index)], code));
}


void CourseComposer::remember()
{
	auto const* course = findCourse();
	if (course)
		items_left = course->items;
	else
		items_left.clear();
}


CourseComposer::Result CourseComposer::makeObject(CourseObject::Kind kind, const MapCoordF& pos, int index, Outcome outcome)
{
	Result result;
	auto const* symbol = symbolFor(kind);
	if (!symbol)
	{
		result.outcome = Refused;
		result.reason = NoSymbol;
		result.kind = kind;
		return result;
	}
	
	// Not into the part of the overrides: its objects are no course objects.
	auto part_index = int(map.getCurrentPartIndex());
	if (map.getPart(std::size_t(part_index))->role() == MapPart::Role::Course)
	{
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			if (map.getPart(std::size_t(p))->role() != MapPart::Role::Course)
			{
				part_index = p;
				break;
			}
		}
	}
	
	auto const before = MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses);
	auto* object = new PointObject(symbol);
	object->setPosition(pos);
	auto const object_index = map.addObject(object, part_index);
	
	// The course object comes by the rule which keeps them in step with the map.
	auto data = map.courseData();
	syncCourseObjects(map, data, nullptr);
	for (auto const& course_object : data.objects)
	{
		if (course_object.object == object->uuid())
			result.code = course_object.code;
	}
	if (!result.code.isEmpty())
		insertCourseItem(data, course_name, index, result.code);
	
	// The object first and the course data behind it, as courseSyncedStep() has them.
	auto* object_step = new DeleteObjectsUndoStep(&map);
	object_step->setPartIndex(part_index);
	object_step->addObject(object_index);
	auto* combined = new CombinedUndoStep(&map);
	combined->push(object_step);
	combined->push(new MapDataUndoStep(&map, MapDataUndoStep::Courses, before));
	map.setCourseData(std::move(data));
	select(map, object);
	map.setObjectsDirty();
	map.setOtherDirty();
	map.push(combined);
	
	result.outcome = outcome;
	return result;
}


CourseComposer::Result CourseComposer::takeObject(const QString& code, int index, Outcome outcome)
{
	Result result;
	auto data = map.courseData();
	if (!insertCourseItem(data, course_name, index, code))
		return result;
	auto const* course_object = data.findObject(code);
	auto* object = course_object->object.isNull() ? nullptr : map.findObject(course_object->object);
	changeCourseData(map, std::move(data));
	select(map, object);
	result.outcome = outcome;
	result.code = code;
	return result;
}


CourseComposer::Result CourseComposer::click(const MapCoordF& pos, double reach)
{
	Result result;
	made_control = false;
	auto const reason = refusal();
	if (reason != NoReason)
	{
		result.outcome = Refused;
		result.reason = reason;
		return result;
	}
	auto const* course = findCourse();
	auto const target = targetAt(pos, reach);
	
	if (inInsertMode())
	{
		auto const index = insert_after + 1;
		if (target.what == Target::Object)
		{
			if (besideItself(*course, index, target.code))
			{
				result.reason = BesideItself;
				result.code = target.code;
				return result;
			}
			result = takeObject(target.code, index, Inserted);
		}
		else
		{
			result = makeObject(CourseObject::Control, pos, index, Inserted);
		}
		if (result.outcome == Inserted)
		{
			insert_after = index;
			remember();
		}
		return result;
	}
	
	insert_after = -1;
	if (target.what == Target::Leg)
	{
		insert_after = target.item;
		remember();
		result.outcome = InsertModeBegun;
		return result;
	}
	if (isComplete())
	{
		result.outcome = Refused;
		result.reason = Complete;
		return result;
	}
	
	auto const index = int(course->items.size());
	if (target.what == Target::Object)
	{
		if (besideItself(*course, index, target.code))
		{
			result.reason = BesideItself;
			result.code = target.code;
			return result;
		}
		result = takeObject(target.code, index, Added);
		remember();
		return result;
	}
	
	auto const has_start = std::any_of(begin(course->items), end(course->items), [](const CourseItem& item) {
		return item.type == CourseItem::Start;
	});
	auto const kind = has_start ? CourseObject::Control : CourseObject::Start;
	result = makeObject(kind, pos, index, Added);
	if (result.outcome == Added)
	{
		made_control = kind == CourseObject::Control;
		remember();
	}
	return result;
}


CourseComposer::Result CourseComposer::doubleClick(const MapCoordF& pos, double /*reach*/)
{
	Result result;
	auto const was_made = made_control;
	made_control = false;
	auto const* course = findCourse();
	if (!was_made || !course || course->items != items_left || course->items.empty()
	    || course->items.back().type != CourseItem::Control)
		return result;
	
	if (!symbolFor(CourseObject::Finish))
	{
		result.outcome = Refused;
		result.reason = NoSymbol;
		result.kind = CourseObject::Finish;
		return result;
	}
	
	// The finish stands where the control of the first click stands.
	auto place = pos;
	auto const* control = map.courseData().findObject(course->items.back().code);
	auto const* object = control && !control->object.isNull() ? map.findObject(control->object) : nullptr;
	if (object && object->getType() == Object::Point)
		place = object->asPoint()->getCoordF();
	
	if (!map.undoManager().undo())
		return result;
	course = findCourse();
	if (!course)
		return result;
	result = makeObject(CourseObject::Finish, place, int(course->items.size()), Added);
	remember();
	return result;
}


CourseComposer::Result CourseComposer::removeAt(const MapCoordF& pos, double reach)
{
	Result result;
	made_control = false;
	auto const reason = refusal();
	if (reason != NoReason)
	{
		result.outcome = Refused;
		result.reason = reason;
		return result;
	}
	auto const target = targetAt(pos, reach);
	if (target.what != Target::Object)
		return result;
	
	auto const* course = findCourse();
	auto index = -1;
	for (std::size_t i = 0; i < course->items.size(); ++i)
	{
		if (names(course->items[i], target.code))
			index = int(i);
	}
	if (index < 0)
		return result;
	
	auto const inserting = inInsertMode();
	auto data = map.courseData();
	if (!removeCourseItem(data, course_name, index))
		return result;
	changeCourseData(map, std::move(data));
	if (inserting && index <= insert_after)
		--insert_after;
	if (!inserting)
		insert_after = -1;
	remember();
	
	result.outcome = Removed;
	result.code = target.code;
	return result;
}


bool CourseComposer::inInsertMode() const
{
	auto const* course = findCourse();
	return insert_after >= 0 && course && std::size_t(insert_after) < course->items.size() && course->items == items_left;
}


void CourseComposer::leaveInsertMode()
{
	insert_after = -1;
}


std::vector<MapCoordF> CourseComposer::rubberEnds() const
{
	std::vector<MapCoordF> ends;
	if (refusal() != NoReason)
		return ends;
	auto const stops = courseStops(map, map.courseData(), *findCourse());
	if (inInsertMode())
	{
		const CourseStop* before = nullptr;
		const CourseStop* behind = nullptr;
		for (auto const& stop : stops)
		{
			if (stop.item <= insert_after)
				before = &stop;
			else if (!behind)
				behind = &stop;
		}
		if (before)
			ends.push_back(before->exit);
		if (behind)
			ends.push_back(behind->entry);
	}
	else if (!isComplete() && !stops.empty())
	{
		ends.push_back(stops.back().exit);
	}
	return ends;
}


}  // namespace OpenOrienteering
