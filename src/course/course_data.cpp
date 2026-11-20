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


#include "course_data.h"

#include <algorithm>

#include <QLatin1Char>


namespace OpenOrienteering {

bool operator==(const CourseField& lhs, const CourseField& rhs)
{
	return lhs.letter == rhs.letter && lhs.value == rhs.value;
}

bool operator==(const CourseObject& lhs, const CourseObject& rhs)
{
	return lhs.object == rhs.object
	       && lhs.kind == rhs.kind
	       && lhs.code == rhs.code
	       && lhs.cell == rhs.cell
	       && lhs.text == rhs.text
	       && lhs.punch_ids == rhs.punch_ids
	       && lhs.locked == rhs.locked
	       && lhs.other == rhs.other;
}

bool operator==(const CourseItem& lhs, const CourseItem& rhs)
{
	return lhs.type == rhs.type && lhs.code == rhs.code && lhs.legs == rhs.legs && lhs.count == rhs.count;
}

bool operator==(const Course& lhs, const Course& rhs)
{
	return lhs.name == rhs.name
	       && lhs.type == rhs.type
	       && lhs.legs == rhs.legs
	       && lhs.items == rhs.items
	       && lhs.extra_km == rhs.extra_km
	       && lhs.extra_set == rhs.extra_set
	       && lhs.climb_m == rhs.climb_m
	       && lhs.climb_manual == rhs.climb_manual
	       && lhs.other == rhs.other;
}

bool operator==(const CourseClass& lhs, const CourseClass& rhs)
{
	return lhs.name == rhs.name
	       && lhs.course == rhs.course
	       && lhs.runners == rhs.runners
	       && lhs.first_no == rhs.first_no
	       && lhs.last_no == rhs.last_no
	       && lhs.comment == rhs.comment
	       && lhs.other == rhs.other;
}

bool operator==(const CourseOverride& lhs, const CourseOverride& rhs)
{
	return lhs.course == rhs.course
	       && lhs.what == rhs.what
	       && lhs.code_from == rhs.code_from
	       && lhs.code_to == rhs.code_to
	       && lhs.object == rhs.object;
}

bool operator==(const CourseOptions& lhs, const CourseOptions& rhs)
{
	return lhs.fields == rhs.fields;
}

bool operator==(const CourseData& lhs, const CourseData& rhs)
{
	return lhs.objects == rhs.objects
	       && lhs.courses == rhs.courses
	       && lhs.classes == rhs.classes
	       && lhs.overrides == rhs.overrides
	       && lhs.options == rhs.options
	       && lhs.canvases == rhs.canvases
	       && lhs.route == rhs.route;
}



bool CourseOptions::has(QChar letter) const
{
	return std::any_of(fields.begin(), fields.end(), [letter](const CourseField& field) { return field.letter == letter; });
}

QString CourseOptions::value(QChar letter, const QString& fallback) const
{
	for (auto const& field : fields)
	{
		if (field.letter == letter)
			return field.value;
	}
	return fallback;
}

void CourseOptions::setValue(QChar letter, const QString& value)
{
	for (auto& field : fields)
	{
		if (field.letter == letter)
		{
			field.value = value;
			return;
		}
	}
	fields.push_back({ letter, value });
}

double CourseOptions::boxSize() const
{
	auto ok = false;
	auto const size = value(QLatin1Char('s')).toDouble(&ok);
	return ok && size > 0 ? size : 6.0;
}

int CourseOptions::maximumRows() const
{
	return value(QLatin1Char('i')).toInt();
}

int CourseOptions::titleMode() const
{
	return value(QLatin1Char('t')).toInt();
}

int CourseOptions::thickerRule() const
{
	return value(QLatin1Char('h')).toInt();
}

double CourseOptions::numberDistance() const
{
	auto ok = false;
	auto const distance = value(QLatin1Char('n')).toDouble(&ok);
	return ok ? distance : 1.0;
}

double CourseOptions::lineDistance() const
{
	return value(QLatin1Char('l')).toDouble();
}

bool CourseOptions::fullStopAfterNumber() const
{
	return value(QLatin1Char('p')).toInt() != 0;
}

bool CourseOptions::whiteBackground() const
{
	return value(QLatin1Char('b')).toInt() != 0;
}



bool CourseData::isEmpty() const
{
	return objects.empty() && courses.empty() && classes.empty() && overrides.empty() && options.fields.empty() && canvases.isEmpty() && route.isDefault();
}

const CourseObject* CourseData::findObject(const QString& code) const
{
	auto const found = std::find_if(objects.begin(), objects.end(), [&code](const CourseObject& object) { return object.code == code; });
	return found == objects.end() ? nullptr : &*found;
}

const Course* CourseData::findCourse(const QString& name) const
{
	auto const found = std::find_if(courses.begin(), courses.end(), [&name](const Course& course) { return course.name == name; });
	return found == courses.end() ? nullptr : &*found;
}


}  // namespace OpenOrienteering
