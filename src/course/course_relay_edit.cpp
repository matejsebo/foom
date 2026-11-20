/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "course_relay_edit.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include <QChar>
#include <QCoreApplication>
#include <QLatin1Char>

#include "course/course_edit.h"
#include "course/course_relay.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseRelay", text);
}

void say(QString* why, const QString& text)
{
	if (why)
		*why = text;
}

Course* findCourse(CourseData& data, const QString& name)
{
	auto const found = std::find_if(begin(data.courses), end(data.courses), [&name](const Course& course) {
		return course.name == name;
	});
	return found == end(data.courses) ? nullptr : &*found;
}

bool isBegin(CourseItem::Type type)
{
	return type == CourseItem::TeamForkBegin || type == CourseItem::LegForkBegin;
}

bool isEnd(CourseItem::Type type)
{
	return type == CourseItem::TeamForkEnd || type == CourseItem::LegForkEnd;
}

/// The legs of a branch of a leg variation; none for a text which does not name legs.
std::vector<int> legsOf(const QString& text)
{
	std::vector<int> legs;
	for (auto const& word : text.split(QLatin1Char('-'), QString::SkipEmptyParts))
	{
		auto ok = false;
		auto const leg = word.trimmed().toInt(&ok);
		if (!ok || leg < 1)
			return {};
		legs.push_back(leg);
	}
	return legs;
}

/// Puts the items into the course and keeps them when the course has no fault with them.
bool insertVariation(CourseData& data, const QString& course, int index, const std::vector<CourseItem>& items, QString* why)
{
	auto changed = data;
	auto* found = findCourse(changed, course);
	if (!found)
	{
		say(why, tr("There is no such course."));
		return false;
	}
	if (index < 0 || std::size_t(index) > found->items.size())
	{
		say(why, tr("There is no such place in the course."));
		return false;
	}
	found->items.insert(begin(found->items) + index, begin(items), end(items));
	auto const structure = relayStructure(*found);
	if (!structure.isValid())
	{
		say(why, structure.faults.constFirst());
		return false;
	}
	data = std::move(changed);
	return true;
}

}  // namespace



QString relayFaultOfChange(const CourseData& before, const CourseData& after, const QString& course)
{
	auto const* changed = after.findCourse(course);
	if (!changed)
		return {};
	auto const* original = before.findCourse(course);
	if (original && !relayStructure(*original).isValid())
		return {};
	auto const structure = relayStructure(*changed);
	return structure.isValid() ? QString{} : structure.faults.constFirst();
}


std::vector<int> relayLegsAt(const Course& course, int index)
{
	if (course.type == Course::Normal || course.legs < 2)
		return {};
	std::vector<int> all;
	for (int leg = 1; leg <= course.legs; ++leg)
		all.push_back(leg);
	// The legs of the leg variations which are open, the innermost last.
	std::vector<std::vector<int>> open = { all };
	auto const stop = std::min(index, int(course.items.size()));
	for (int i = 0; i < stop; ++i)
	{
		auto const& item = course.items[std::size_t(i)];
		switch (item.type)
		{
		case CourseItem::LegForkBegin:
			open.push_back(open.back());
			break;
		case CourseItem::LegBranch:
			if (open.size() > 1)
				open.back() = legsOf(item.legs);
			break;
		case CourseItem::LegForkEnd:
			if (open.size() > 1)
				open.pop_back();
			break;
		default:
			break;
		}
	}
	auto legs = open.back();
	std::sort(begin(legs), end(legs));
	return legs;
}


bool setCourseType(CourseData& data, const QString& course, Course::Type type, int legs, QString* why)
{
	auto changed = data;
	auto* found = findCourse(changed, course);
	if (!found)
	{
		say(why, tr("There is no such course."));
		return false;
	}
	found->type = type;
	found->legs = type == Course::Normal ? 0 : legs;
	if (type != Course::Normal)
	{
		if (legs < 2)
		{
			say(why, tr("A relay has at least two legs."));
			return false;
		}
		if (legs > max_relay_legs)
		{
			say(why, tr("A relay has %1 legs at most.").arg(max_relay_legs));
			return false;
		}
		auto const classes = std::count_if(begin(data.classes), end(data.classes), [&course](const CourseClass& entry) {
			return entry.course == course;
		});
		if (classes > 1)
		{
			say(why, tr("A relay or a one-man relay can have one class only."));
			return false;
		}
	}
	auto const fault = relayFaultOfChange(data, changed, course);
	if (!fault.isEmpty())
	{
		say(why, fault);
		return false;
	}
	data = std::move(changed);
	return true;
}


bool insertTeamVariation(CourseData& data, const QString& course, int index, int branches, QString* why)
{
	branches = std::max(branches, 0);
	std::vector<CourseItem> items;
	CourseItem item;
	item.type = CourseItem::TeamForkBegin;
	item.count = branches;
	items.push_back(item);
	item.count = 0;
	item.type = CourseItem::ForkBranch;
	items.insert(items.end(), std::size_t(branches), item);
	item.type = CourseItem::TeamForkEnd;
	items.push_back(item);
	return insertVariation(data, course, index, items, why);
}


bool insertLegVariation(CourseData& data, const QString& course, int index, const QStringList& branch_legs, QString* why)
{
	std::vector<CourseItem> items;
	CourseItem item;
	item.type = CourseItem::LegForkBegin;
	items.push_back(item);
	item.type = CourseItem::LegBranch;
	for (auto const& legs : branch_legs)
	{
		item.legs = legs;
		item.legs.remove(QLatin1Char(' '));
		item.legs.remove(QLatin1Char('\t'));
		items.push_back(item);
	}
	item.legs.clear();
	item.type = CourseItem::LegForkEnd;
	items.push_back(item);
	return insertVariation(data, course, index, items, why);
}


bool variationRange(const Course& course, int index, int& first, int& last)
{
	auto const size = int(course.items.size());
	auto const typeOf = [&course](int i) { return course.items[std::size_t(i)].type; };
	if (index < 0 || index >= size || !isVariationItem(typeOf(index)))
		return false;
	first = index;
	last = index;
	
	// The begin of the variation.
	auto begin_index = -1;
	if (isBegin(typeOf(index)))
	{
		begin_index = index;
	}
	else
	{
		auto depth = 0;
		for (int i = index - 1; i >= 0 && begin_index < 0; --i)
		{
			if (isEnd(typeOf(i)))
				++depth;
			else if (isBegin(typeOf(i)) && depth-- == 0)
				begin_index = i;
		}
	}
	if (begin_index < 0)
		return true;
	// The marker is of the kind of the begin.
	auto const of_team = typeOf(begin_index) == CourseItem::TeamForkBegin;
	auto const own = typeOf(index);
	auto const fits = index == begin_index
	                  || (of_team ? own == CourseItem::ForkBranch || own == CourseItem::TeamForkEnd
	                              : own == CourseItem::LegBranch || own == CourseItem::LegForkEnd);
	if (!fits)
		return true;
	
	// Its end.
	auto end_index = -1;
	auto depth = 0;
	for (int i = begin_index; i < size && end_index < 0; ++i)
	{
		if (isBegin(typeOf(i)))
			++depth;
		else if (isEnd(typeOf(i)) && --depth == 0)
			end_index = i;
	}
	if (end_index < index || typeOf(end_index < 0 ? index : end_index) != (of_team ? CourseItem::TeamForkEnd : CourseItem::LegForkEnd))
		return true;
	first = begin_index;
	last = end_index;
	return true;
}


bool removeVariation(CourseData& data, const QString& course, int index)
{
	auto* found = findCourse(data, course);
	auto first = -1;
	auto last = -1;
	if (!found || !variationRange(*found, index, first, last))
		return false;
	found->items.erase(begin(found->items) + first, begin(found->items) + last + 1);
	return true;
}


}  // namespace OpenOrienteering
