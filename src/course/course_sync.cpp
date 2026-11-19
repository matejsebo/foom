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


#include "course_sync.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

#include <QSet>
#include <QString>
#include <QUuid>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "course/course_edit.h"
#include "undo/map_data_undo.h"
#include "undo/undo.h"

namespace OpenOrienteering {

namespace {

/// The kind of course object which the map object is; false when it is none.
bool courseKindOfObject(const Object* object, CourseObject::Kind* kind)
{
	return object && object->getSymbol() && courseKindOfRole(object->getSymbol()->courseRole(), kind);
}

/// Returns true for the parts whose objects are no course objects.
bool isLeftAside(const MapPart* part)
{
	return part->role() == MapPart::Role::Course;
}

}  // namespace



bool courseKindOfRole(Symbol::CourseRole role, CourseObject::Kind* kind)
{
	auto result = CourseObject::Control;
	switch (role)
	{
	case Symbol::CourseStart:        result = CourseObject::Start; break;
	case Symbol::CourseControl:      result = CourseObject::Control; break;
	case Symbol::CourseFinish:       result = CourseObject::Finish; break;
	case Symbol::CourseMarkedRoute:  result = CourseObject::MarkedRoute; break;
	case Symbol::CourseDescription:  result = CourseObject::Description; break;
	case Symbol::CourseTitle:        result = CourseObject::CourseTitle; break;
	case Symbol::CourseStartNumber:  result = CourseObject::StartNumber; break;
	case Symbol::CourseVariantCode:  result = CourseObject::VariantCode; break;
	case Symbol::NoCourseRole:
	case Symbol::DescriptionSymbol:
		return false;
	}
	if (kind)
		*kind = result;
	return true;
}


bool syncCourseObjects(const Map& map, CourseData& data, CourseSyncReport* report)
{
	// The kinds of course objects for which the map has a symbol.
	QSet<int> kinds_of_symbols;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto kind = CourseObject::Control;
		if (courseKindOfRole(map.getSymbol(i)->courseRole(), &kind))
			kinds_of_symbols.insert(kind);
	}
	auto const has_symbols = !kinds_of_symbols.isEmpty();
	auto const has_objects = std::any_of(begin(data.objects), end(data.objects), [](const CourseObject& object) {
		return !object.object.isNull();
	});
	if (!has_symbols && !has_objects)
		return false;
	
	auto changed = false;
	
	// The course objects whose map object went, or is of another kind now.
	// An object whose symbol has no role is judged only when the map has a
	// symbol for the kind of its course object: a symbol set without roles
	// takes no course object away.
	QStringList gone;
	QSet<QUuid> known;
	for (auto const& object : data.objects)
	{
		if (object.object.isNull())
			continue;
		auto const* map_object = map.findObject(object.object);
		auto kind = CourseObject::Control;
		auto const has_kind = courseKindOfObject(map_object, &kind);
		auto const stays = map_object && !known.contains(object.object)
		                   && (has_kind ? kind == object.kind : !kinds_of_symbols.contains(object.kind));
		if (stays)
			known.insert(object.object);
		else
			gone.push_back(object.code);
	}
	for (auto const& code : qAsConst(gone))
	{
		auto const courses = removeCourseObject(data, code);
		changed = true;
		if (!report)
			continue;
		report->removed.push_back(code);
		if (!courses.isEmpty())
			report->removed_from_courses.push_back(code);
		for (auto const& course : courses)
		{
			if (!report->courses.contains(course))
				report->courses.push_back(course);
		}
	}
	
	// The objects of the map which have no course object yet.
	if (has_symbols)
	{
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			auto const* part = map.getPart(std::size_t(p));
			if (isLeftAside(part))
				continue;
			for (int i = 0; i < part->getNumObjects(); ++i)
			{
				auto const* map_object = part->getObject(i);
				auto kind = CourseObject::Control;
				if (!courseKindOfObject(map_object, &kind) || known.contains(map_object->uuid()))
					continue;
				CourseObject object;
				object.object = map_object->uuid();
				object.kind = kind;
				object.code = nextCourseCode(data, kind);
				known.insert(object.object);
				if (report)
					report->added.push_back(object.code);
				data.objects.push_back(std::move(object));
				changed = true;
			}
		}
	}
	return changed;
}


std::unique_ptr<UndoStep> courseSyncedStep(Map& map, std::unique_ptr<UndoStep> step)
{
	auto data = map.courseData();
	CourseSyncReport report;
	if (!syncCourseObjects(map, data, &report))
		return step;
	
	// The edit first and the course data behind it: taking a combined step
	// back begins at its end, so that the course data are restored before
	// the edit is, also when the edit is itself a step of the course data.
	auto combined = std::make_unique<CombinedUndoStep>(&map);
	auto const before = MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses);
	combined->push(step.release());
	combined->push(new MapDataUndoStep(&map, MapDataUndoStep::Courses, before));
	map.setCourseData(std::move(data));
	map.setOtherDirty();
	if (!report.courses.isEmpty())
		emit map.courseObjectsRemovedFromCourses(report.removed_from_courses, report.courses);
	return combined;
}


}  // namespace OpenOrienteering
