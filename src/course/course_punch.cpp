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


#include "course_punch.h"

#include <algorithm>
#include <iterator>
#include <tuple>

#include <QChar>
#include <QLatin1Char>
#include <QRegularExpression>

namespace OpenOrienteering {

namespace {

/// True for a text of the digits 0 to 9 alone.
bool isNumber(const QString& text)
{
	if (text.isEmpty())
		return false;
	return std::all_of(text.begin(), text.end(), [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
}

/// A number without its leading zeros; "0" stays.
QString withoutLeadingZeros(const QString& number)
{
	int first = 0;
	while (first + 1 < number.size() && number.at(first) == QLatin1Char('0'))
		++first;
	return number.mid(first);
}

/// The order of ids: numbers first, by their value, then the others by their text.
bool idLess(const QString& lhs, const QString& rhs)
{
	auto const lhs_number = isNumber(lhs);
	auto const rhs_number = isNumber(rhs);
	if (lhs_number != rhs_number)
		return lhs_number;
	if (!lhs_number)
		return lhs < rhs;
	auto const l = withoutLeadingZeros(lhs);
	auto const r = withoutLeadingZeros(rhs);
	return std::make_tuple(l.size(), l) < std::make_tuple(r.size(), r);
}

/// The number with which a code begins; 0 when it begins otherwise.
int leadingNumber(const QString& code)
{
	int end = 0;
	while (end < code.size() && code.at(end) >= QLatin1Char('0') && code.at(end) <= QLatin1Char('9'))
		++end;
	return code.left(std::min(end, 9)).toInt();
}

}  // namespace



bool parsePunchIds(const QString& text, QStringList* ids)
{
	static const QRegularExpression separators(QStringLiteral("[;,\\s]+"));
	QStringList result;
	for (auto const& word : text.split(separators, QString::SkipEmptyParts))
	{
		if (!isNumber(word))
			return false;
		result = addPunchId(result, withoutLeadingZeros(word));
	}
	if (ids)
		*ids = result;
	return true;
}


QString joinPunchIds(const QStringList& ids)
{
	return ids.join(QLatin1Char(';'));
}


QStringList addPunchId(const QStringList& ids, const QString& id)
{
	auto result = ids;
	if (!result.contains(id))
		result.push_back(id);
	std::stable_sort(result.begin(), result.end(), idLess);
	return result;
}


std::vector<int> offeredPunchIds()
{
	// 31 to 276 without these.
	static const int missing[] = { 68, 86, 89, 98, 99, 186, 189, 198, 199 };
	std::vector<int> result;
	for (int id = 31; id <= 276; ++id)
	{
		if (std::find(std::begin(missing), std::end(missing), id) == std::end(missing))
			result.push_back(id);
	}
	return result;
}


QString defaultPunchId(const CourseObject& control, PunchSystem /*system*/)
{
	// The code, with each of the systems.
	if (control.kind != CourseObject::Control || !isNumber(control.code))
		return {};
	return withoutLeadingZeros(control.code);
}


QStringList withDefaultPunchId(const CourseObject& control, const QStringList& ids, PunchDefaults mode, PunchSystem system)
{
	auto const id = defaultPunchId(control, system);
	if (id.isEmpty())
		return ids;
	switch (mode)
	{
	case PunchDefaults::OnlyWithout:
		return ids.isEmpty() ? QStringList{ id } : ids;
	case PunchDefaults::Replace:
		return QStringList{ id };
	case PunchDefaults::AddKeep:
		return addPunchId(ids, id);
	}
	return ids;
}


int addDefaultPunchIds(CourseData& data, PunchDefaults mode, PunchSystem system)
{
	int changed = 0;
	for (auto& object : data.objects)
	{
		if (object.kind != CourseObject::Control)
			continue;
		auto ids = withDefaultPunchId(object, object.punch_ids, mode, system);
		if (ids != object.punch_ids)
		{
			object.punch_ids = ids;
			++changed;
		}
	}
	return changed;
}


std::vector<std::size_t> punchControlOrder(const CourseData& data)
{
	std::vector<std::size_t> result;
	for (std::size_t i = 0; i < data.objects.size(); ++i)
	{
		if (data.objects[i].kind == CourseObject::Control)
			result.push_back(i);
	}
	std::stable_sort(begin(result), end(result), [&data](std::size_t lhs, std::size_t rhs) {
		auto const& l = data.objects[lhs].code;
		auto const& r = data.objects[rhs].code;
		return std::make_tuple(leadingNumber(l), l) < std::make_tuple(leadingNumber(r), r);
	});
	return result;
}


}  // namespace OpenOrienteering
