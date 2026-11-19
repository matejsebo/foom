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


#include "course_edit.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

#include <QChar>
#include <QLatin1Char>

namespace OpenOrienteering {

namespace {

/// The letter with which the codes of a kind begin; null for a control.
QChar codeLetter(CourseObject::Kind kind)
{
	switch (kind)
	{
	case CourseObject::Start:        return QLatin1Char('S');
	case CourseObject::Control:      break;
	case CourseObject::MarkedRoute:  return QLatin1Char('M');
	case CourseObject::Finish:       return QLatin1Char('F');
	case CourseObject::Description:  return QLatin1Char('D');
	case CourseObject::CourseTitle:  return QLatin1Char('N');
	case CourseObject::VariantCode:  return QLatin1Char('V');
	case CourseObject::StartNumber:  return QLatin1Char('U');
	case CourseObject::TextBlock:    return QLatin1Char('T');
	}
	return {};
}

/// The number of a code which is the letter and a number; 0 for another code.
int codeNumber(const QString& code, QChar letter)
{
	auto digits = code;
	if (!letter.isNull())
	{
		if (!code.startsWith(letter))
			return 0;
		digits = code.mid(1);
	}
	if (digits.isEmpty())
		return 0;
	for (auto const c : digits)
	{
		if (c < QLatin1Char('0') || c > QLatin1Char('9'))
			return 0;
	}
	auto ok = false;
	auto const number = digits.toInt(&ok);
	return ok ? number : 0;
}

bool isValidName(const QString& name)
{
	return !name.trimmed().isEmpty() && !name.contains(QLatin1Char('\t'));
}

Course* findCourse(CourseData& data, const QString& name)
{
	auto const found = std::find_if(begin(data.courses), end(data.courses), [&name](const Course& course) {
		return course.name == name;
	});
	return found == end(data.courses) ? nullptr : &*found;
}

bool isValidIndex(const Course& course, int index)
{
	return index >= 0 && std::size_t(index) < course.items.size();
}

}  // namespace



QString nextCourseCode(const CourseData& data, CourseObject::Kind kind)
{
	auto const letter = codeLetter(kind);
	// Codes below 31 are not used for controls.
	auto highest = kind == CourseObject::Control ? 30 : 0;
	for (auto const& object : data.objects)
		highest = std::max(highest, codeNumber(object.code, letter));
	QString code = QString::number(highest + 1);
	if (!letter.isNull())
		code.prepend(letter);
	return code;
}


bool isValidCourseCode(const QString& code)
{
	if (code.isEmpty())
		return false;
	return std::none_of(code.begin(), code.end(), [](QChar c) { return c.isSpace(); });
}


QStringList coursesUsing(const CourseData& data, const QString& code)
{
	QStringList names;
	for (auto const& course : data.courses)
	{
		auto const uses = std::any_of(begin(course.items), end(course.items), [&code](const CourseItem& item) {
			return item.namesObject() && item.code == code;
		});
		if (uses)
			names.push_back(course.name);
	}
	return names;
}


bool renameCourseObject(CourseData& data, const QString& old_code, const QString& new_code)
{
	auto const found = std::find_if(begin(data.objects), end(data.objects), [&old_code](const CourseObject& object) {
		return object.code == old_code;
	});
	if (found == end(data.objects))
		return false;
	if (new_code == old_code)
		return true;
	if (!isValidCourseCode(new_code) || data.findObject(new_code))
		return false;
	
	found->code = new_code;
	for (auto& course : data.courses)
	{
		for (auto& item : course.items)
		{
			if (item.namesObject() && item.code == old_code)
				item.code = new_code;
		}
	}
	for (auto& override : data.overrides)
	{
		if (override.code_from == old_code)
			override.code_from = new_code;
		if (override.code_to == old_code)
			override.code_to = new_code;
	}
	return true;
}


QStringList removeCourseObject(CourseData& data, const QString& code)
{
	QStringList courses;
	data.objects.erase(std::remove_if(begin(data.objects), end(data.objects), [&code](const CourseObject& object) {
		return object.code == code;
	}), end(data.objects));
	for (auto& course : data.courses)
	{
		auto const last = std::remove_if(begin(course.items), end(course.items), [&code](const CourseItem& item) {
			return item.namesObject() && item.code == code;
		});
		if (last != end(course.items))
		{
			course.items.erase(last, end(course.items));
			courses.push_back(course.name);
		}
	}
	data.overrides.erase(std::remove_if(begin(data.overrides), end(data.overrides), [&code](const CourseOverride& override) {
		return override.code_from == code || override.code_to == code;
	}), end(data.overrides));
	return courses;
}


bool isValidCourseName(const CourseData& data, const QString& name)
{
	return isValidName(name) && !data.findCourse(name);
}


bool isValidClassName(const CourseData& data, const QString& name)
{
	return isValidName(name)
	       && std::none_of(begin(data.classes), end(data.classes), [&name](const CourseClass& c) { return c.name == name; });
}


bool addCourse(CourseData& data, const QString& name)
{
	if (!isValidCourseName(data, name))
		return false;
	Course course;
	course.name = name;
	data.courses.push_back(course);
	return true;
}


bool duplicateCourse(CourseData& data, const QString& original, const QString& name)
{
	auto const* found = data.findCourse(original);
	if (!found || !isValidCourseName(data, name))
		return false;
	Course course = *found;
	course.name = name;
	data.courses.push_back(course);
	return true;
}


bool renameCourse(CourseData& data, const QString& old_name, const QString& new_name)
{
	auto* found = findCourse(data, old_name);
	if (!found)
		return false;
	if (new_name == old_name)
		return true;
	if (!isValidCourseName(data, new_name))
		return false;
	found->name = new_name;
	for (auto& course_class : data.classes)
	{
		if (course_class.course == old_name)
			course_class.course = new_name;
	}
	for (auto& override : data.overrides)
	{
		if (override.course == old_name)
			override.course = new_name;
	}
	return true;
}


bool removeCourse(CourseData& data, const QString& name)
{
	auto const found = std::find_if(begin(data.courses), end(data.courses), [&name](const Course& course) {
		return course.name == name;
	});
	if (found == end(data.courses))
		return false;
	data.courses.erase(found);
	data.classes.erase(std::remove_if(begin(data.classes), end(data.classes), [&name](const CourseClass& c) {
		return c.course == name;
	}), end(data.classes));
	data.overrides.erase(std::remove_if(begin(data.overrides), end(data.overrides), [&name](const CourseOverride& override) {
		return override.course == name;
	}), end(data.overrides));
	return true;
}


bool canBeCourseItem(CourseObject::Kind kind, CourseItem::Type* type)
{
	auto item_type = CourseItem::Control;
	switch (kind)
	{
	case CourseObject::Start:        item_type = CourseItem::Start; break;
	case CourseObject::Control:      item_type = CourseItem::Control; break;
	case CourseObject::MarkedRoute:  item_type = CourseItem::MarkedRoute; break;
	case CourseObject::Finish:       item_type = CourseItem::Finish; break;
	case CourseObject::TextBlock:    item_type = CourseItem::TextBlock; break;
	case CourseObject::Description:
	case CourseObject::CourseTitle:
	case CourseObject::VariantCode:
	case CourseObject::StartNumber:
		return false;
	}
	if (type)
		*type = item_type;
	return true;
}


bool isVariationItem(CourseItem::Type type)
{
	switch (type)
	{
	case CourseItem::TeamForkBegin:
	case CourseItem::ForkBranch:
	case CourseItem::TeamForkEnd:
	case CourseItem::LegForkBegin:
	case CourseItem::LegBranch:
	case CourseItem::LegForkEnd:
		return true;
	default:
		return false;
	}
}


bool insertCourseItem(CourseData& data, const QString& course, int index, const QString& code)
{
	auto* found = findCourse(data, course);
	auto const* object = data.findObject(code);
	if (!found || !object || index < 0 || std::size_t(index) > found->items.size())
		return false;
	CourseItem item;
	if (!canBeCourseItem(object->kind, &item.type))
		return false;
	item.code = code;
	found->items.insert(begin(found->items) + index, item);
	return true;
}


bool removeCourseItem(CourseData& data, const QString& course, int index)
{
	auto* found = findCourse(data, course);
	if (!found || !isValidIndex(*found, index) || isVariationItem(found->items[std::size_t(index)].type))
		return false;
	found->items.erase(begin(found->items) + index);
	return true;
}


bool moveCourseItem(CourseData& data, const QString& course, int from, int to)
{
	auto* found = findCourse(data, course);
	if (!found || !isValidIndex(*found, from) || !isValidIndex(*found, to) || from == to
	    || isVariationItem(found->items[std::size_t(from)].type))
		return false;
	auto const item = found->items[std::size_t(from)];
	found->items.erase(begin(found->items) + from);
	found->items.insert(begin(found->items) + to, item);
	return true;
}


}  // namespace OpenOrienteering
