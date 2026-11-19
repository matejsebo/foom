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


#include "course_bulk.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>

#include <QChar>
#include <QLatin1Char>
#include <QSet>

#include "course/course_edit.h"

namespace OpenOrienteering {

namespace {

bool names(const CourseItem& item, const QString& code)
{
	return item.namesObject() && item.code == code;
}

Course* findCourse(CourseData& data, const QString& name)
{
	auto const found = std::find_if(begin(data.courses), end(data.courses), [&name](const Course& course) {
		return course.name == name;
	});
	return found == end(data.courses) ? nullptr : &*found;
}

/// The checks which putting in and taking out share.
CourseBulkReport::Error checkCommand(const CourseData& data, const QString& code, const QStringList& courses, const CourseCondition& condition, bool all_allowed)
{
	auto const* object = data.findObject(code);
	if (!object || !canBeCourseItem(object->kind))
		return CourseBulkReport::NoObject;
	
	if (courses.isEmpty())
		return CourseBulkReport::NoCourses;
	for (auto const& name : courses)
	{
		if (!data.findCourse(name))
			return CourseBulkReport::NoCourses;
	}
	
	switch (condition.relation)
	{
	case CourseCondition::None:
		return CourseBulkReport::NoCondition;
	case CourseCondition::All:
		return all_allowed ? CourseBulkReport::NoError : CourseBulkReport::NoCondition;
	case CourseCondition::After:
	case CourseCondition::Before:
		if (!data.findObject(condition.first))
			return CourseBulkReport::NoCondition;
		if (condition.first == code)
			return CourseBulkReport::InvalidOrder;
		break;
	case CourseCondition::Between:
		if (!data.findObject(condition.first) || !data.findObject(condition.second))
			return CourseBulkReport::NoCondition;
		if (condition.first == code || condition.second == code || condition.first == condition.second)
			return CourseBulkReport::InvalidOrder;
		break;
	}
	return CourseBulkReport::NoError;
}

/// The number of a code which is a number; 0 for another code.
int controlNumber(const QString& code)
{
	if (code.isEmpty() || code.at(0) == QLatin1Char('0'))
		return 0;
	for (auto const c : code)
	{
		if (c < QLatin1Char('0') || c > QLatin1Char('9'))
			return 0;
	}
	auto ok = false;
	auto const number = code.toInt(&ok);
	return ok ? number : 0;
}

}  // namespace



int CourseBulkReport::items() const
{
	auto sum = 0;
	for (auto const& entry : changed)
		sum += entry.second;
	return sum;
}


CourseBulkReport insertIntoCourses(CourseData& data, const QString& code, const QStringList& courses, const CourseCondition& condition)
{
	CourseBulkReport report;
	report.error = checkCommand(data, code, courses, condition, false);
	if (!report.ok())
		return report;
	
	CourseItem new_item;
	canBeCourseItem(data.findObject(code)->kind, &new_item.type);
	new_item.code = code;
	
	for (auto const& name : courses)
	{
		auto* course = findCourse(data, name);
		auto const& items = course->items;
		auto const size = items.size();
		
		// The places, as the index which the new item would have in the list as it is.
		std::vector<std::size_t> places;
		for (std::size_t i = 0; i < size; ++i)
		{
			if (!names(items[i], condition.first))
				continue;
			auto place = i;
			switch (condition.relation)
			{
			case CourseCondition::After:
				place = i + 1;
				break;
			case CourseCondition::Before:
				break;
			case CourseCondition::Between:
				if (i + 1 >= size || !names(items[i + 1], condition.second))
					continue;
				place = i + 1;
				break;
			default:
				continue;
			}
			// Not beside itself.
			if ((place > 0 && names(items[place - 1], code)) || (place < size && names(items[place], code)))
				continue;
			places.push_back(place);
		}
		
		if (places.empty())
		{
			report.unchanged.push_back(name);
			continue;
		}
		for (auto place = places.rbegin(); place != places.rend(); ++place)
			course->items.insert(begin(course->items) + int(*place), new_item);
		report.changed.emplace_back(name, int(places.size()));
	}
	return report;
}


CourseBulkReport removeFromCourses(CourseData& data, const QString& code, const QStringList& courses, const CourseCondition& condition)
{
	CourseBulkReport report;
	report.error = checkCommand(data, code, courses, condition, true);
	if (!report.ok())
		return report;
	
	for (auto const& name : courses)
	{
		auto* course = findCourse(data, name);
		auto const& items = course->items;
		auto const size = items.size();
		
		std::vector<std::size_t> places;
		for (std::size_t i = 0; i < size; ++i)
		{
			if (!names(items[i], code))
				continue;
			auto const after_first = i > 0 && names(items[i - 1], condition.first);
			auto meant = false;
			switch (condition.relation)
			{
			case CourseCondition::All:
				meant = true;
				break;
			case CourseCondition::After:
				meant = after_first;
				break;
			case CourseCondition::Before:
				meant = i + 1 < size && names(items[i + 1], condition.first);
				break;
			case CourseCondition::Between:
				meant = after_first && i + 1 < size && names(items[i + 1], condition.second);
				break;
			case CourseCondition::None:
				break;
			}
			if (meant)
				places.push_back(i);
		}
		
		if (places.empty())
		{
			report.unchanged.push_back(name);
			continue;
		}
		for (auto place = places.rbegin(); place != places.rend(); ++place)
			course->items.erase(begin(course->items) + int(*place));
		report.changed.emplace_back(name, int(places.size()));
	}
	return report;
}



int firstControlCode(const CourseData& data)
{
	auto first = 0;
	for (auto const& object : data.objects)
	{
		if (object.kind != CourseObject::Control)
			continue;
		auto const number = controlNumber(object.code);
		if (number > 0 && (first == 0 || number < first))
			first = number;
	}
	return first;
}


CourseRenumberReport renumberControls(CourseData& data, int old_first, int new_first, bool close_gaps)
{
	CourseRenumberReport report;
	if (new_first < 1 || old_first < 1)
		return report;
	
	// The controls which count, in the order of their numbers.
	std::vector<int> numbers;
	for (auto const& object : data.objects)
	{
		if (object.kind != CourseObject::Control)
			continue;
		auto const number = controlNumber(object.code);
		if (number >= old_first)
			numbers.push_back(number);
	}
	if (numbers.empty())
		return report;
	std::sort(begin(numbers), end(numbers));
	
	std::map<QString, QString> new_codes;
	QSet<QString> taken;
	for (std::size_t i = 0; i < numbers.size(); ++i)
	{
		auto const number = close_gaps ? new_first + int(i) : numbers[i] + new_first - old_first;
		auto const code = QString::number(number);
		new_codes[QString::number(numbers[i])] = code;
		taken.insert(code);
	}
	
	// A new code must not be that of an object which keeps its code.
	for (auto const& object : data.objects)
	{
		auto const renumbered = object.kind == CourseObject::Control && new_codes.count(object.code) > 0;
		if (!renumbered && taken.contains(object.code))
		{
			report.clash = object.code;
			return report;
		}
	}
	
	auto const new_code = [&new_codes](const QString& code) {
		auto const found = new_codes.find(code);
		return found == new_codes.end() ? code : found->second;
	};
	for (auto& object : data.objects)
	{
		if (object.kind != CourseObject::Control)
			continue;
		auto const code = new_code(object.code);
		if (code != object.code)
		{
			object.code = code;
			++report.renumbered;
		}
	}
	for (auto& course : data.courses)
	{
		for (auto& item : course.items)
		{
			if (item.type == CourseItem::Control)
				item.code = new_code(item.code);
		}
	}
	for (auto& override : data.overrides)
	{
		override.code_from = new_code(override.code_from);
		if (!override.code_to.isEmpty())
			override.code_to = new_code(override.code_to);
	}
	report.done = true;
	return report;
}


}  // namespace OpenOrienteering
