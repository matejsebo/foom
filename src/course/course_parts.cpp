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


#include "course_parts.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include <QCoreApplication>

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseParts", text);
}

bool endsPart(CourseItem::Type type)
{
	return type == CourseItem::MapExchange || type == CourseItem::MapFlip;
}

}  // namespace



std::vector<CoursePart> courseParts(const Course& course)
{
	std::vector<CoursePart> parts;
	CoursePart part;
	auto const size = int(course.items.size());
	for (int i = 0; i < size; ++i)
	{
		auto const type = course.items[std::size_t(i)].type;
		if (!endsPart(type))
			continue;
		part.end = i;
		part.ends_with = type == CourseItem::MapExchange ? CoursePart::MapExchange : CoursePart::MapFlip;
		parts.push_back(part);
		part = {};
		part.first = i + 1;
	}
	part.end = size;
	parts.push_back(part);
	return parts;
}


int coursePartOfItem(const Course& course, int index)
{
	if (index < 0 || std::size_t(index) >= course.items.size())
		return -1;
	return int(std::count_if(begin(course.items), begin(course.items) + index, [](const CourseItem& item) {
		return endsPart(item.type);
	}));
}


QString coursePartFault(const Course& course)
{
	auto const parts = courseParts(course);
	if (parts.size() < 2)
		return {};
	for (std::size_t p = 0; p < parts.size(); ++p)
	{
		auto const first = begin(course.items) + parts[p].first;
		auto const last = begin(course.items) + parts[p].end;
		auto const has_control = std::any_of(first, last, [](const CourseItem& item) { return item.type == CourseItem::Control; });
		if (!has_control)
			return tr("Part %1 has no control.").arg(p + 1);
		if (p == 0)
			continue;
		// A marked route may lead to the start of the part.
		auto const place = std::find_if(first, last, [](const CourseItem& item) {
			return item.type == CourseItem::Start || item.type == CourseItem::Control || item.type == CourseItem::Finish;
		});
		if (place->type != CourseItem::Start)
			return tr("Part %1 does not begin with a start.").arg(p + 1);
	}
	return {};
}


bool insertCourseMarker(CourseData& data, const QString& course, int index, CourseItem::Type type)
{
	switch (type)
	{
	case CourseItem::CrossingPoint:
	case CourseItem::Passage:
	case CourseItem::MapExchange:
	case CourseItem::MapFlip:
		break;
	default:
		return false;
	}
	auto const found = std::find_if(begin(data.courses), end(data.courses), [&course](const Course& entry) {
		return entry.name == course;
	});
	if (found == end(data.courses) || index < 0 || std::size_t(index) > found->items.size())
		return false;
	CourseItem item;
	item.type = type;
	found->items.insert(begin(found->items) + index, item);
	return true;
}


}  // namespace OpenOrienteering
