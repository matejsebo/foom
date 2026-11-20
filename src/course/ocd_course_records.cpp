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


#include "ocd_course_records.h"

#include <QtGlobal>
#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>

#include <algorithm>
#include <utility>

#include "core/extents.h"
#include "core/foreign_data.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"


namespace OpenOrienteering {

namespace OcdCourse {

namespace {

const QChar tab = QLatin1Char('\t');

/// The letter in the Y field of a course object.
char kindLetter(CourseObject::Kind kind)
{
	switch (kind)
	{
	case CourseObject::Start:        return 's';
	case CourseObject::Control:      return 'c';
	case CourseObject::MarkedRoute:  return 'm';
	case CourseObject::Finish:       return 'f';
	case CourseObject::Description:  return 'd';
	case CourseObject::TextBlock:    return 't';
	case CourseObject::CourseTitle:  return 'n';
	case CourseObject::VariantCode:  return 'v';
	case CourseObject::StartNumber:  return 'u';
	}
	return 0;
}

bool isKind(int value)
{
	return value >= 0 && value <= 80 && value % 10 == 0;
}

/// A number which may be absent: -1 for an empty field and for one which is no number.
int optionalNumber(const QString& value)
{
	auto ok = false;
	auto const number = value.trimmed().toInt(&ok);
	return ok ? number : -1;
}

QString optionalNumberText(int number)
{
	return number < 0 ? QString{} : QString::number(number);
}

/// The letter of a course item which names a course object; 0 for the other types.
char itemObjectLetter(CourseItem::Type type)
{
	switch (type)
	{
	case CourseItem::Start:        return 's';
	case CourseItem::Control:      return 'c';
	case CourseItem::MarkedRoute:  return 'm';
	case CourseItem::Finish:       return 'f';
	case CourseItem::TextBlock:    return 't';
	default:                       return 0;
	}
}

QString itemText(const CourseItem& item)
{
	if (auto const letter = itemObjectLetter(item.type))
		return QLatin1Char(letter) + item.code;
	switch (item.type)
	{
	case CourseItem::CrossingPoint:  return QStringLiteral("k");
	case CourseItem::Passage:        return QStringLiteral("w");
	case CourseItem::MapExchange:    return QStringLiteral("g");
	case CourseItem::MapFlip:        return QStringLiteral("x");
	case CourseItem::TeamForkBegin:  return QLatin1Char('r') + QString::number(item.count);
	case CourseItem::ForkBranch:     return QStringLiteral("v");
	case CourseItem::TeamForkEnd:    return QStringLiteral("q");
	case CourseItem::LegForkBegin:   return QStringLiteral("l");
	case CourseItem::LegBranch:      return QLatin1Char('b') + item.legs;
	case CourseItem::LegForkEnd:     return QStringLiteral("p");
	default:                         return {};
	}
}

/// Reads a field of a course record as an item. Returns false when its letter is none of an item.
bool itemFromField(QChar letter, const QString& value, CourseItem& item)
{
	item = {};
	switch (letter.unicode())
	{
	case 's': item.type = CourseItem::Start; item.code = value; return true;
	case 'c': item.type = CourseItem::Control; item.code = value; return true;
	case 'm': item.type = CourseItem::MarkedRoute; item.code = value; return true;
	case 'f': item.type = CourseItem::Finish; item.code = value; return true;
	case 't': item.type = CourseItem::TextBlock; item.code = value; return true;
	case 'k': item.type = CourseItem::CrossingPoint; return true;
	case 'w': item.type = CourseItem::Passage; return true;
	case 'g': item.type = CourseItem::MapExchange; return true;
	case 'x': item.type = CourseItem::MapFlip; return true;
	case 'r': item.type = CourseItem::TeamForkBegin; item.count = value.toInt(); return true;
	case 'v': item.type = CourseItem::ForkBranch; return true;
	case 'q': item.type = CourseItem::TeamForkEnd; return true;
	case 'l': item.type = CourseItem::LegForkBegin; return true;
	case 'b': item.type = CourseItem::LegBranch; item.legs = value; return true;
	case 'p': item.type = CourseItem::LegForkEnd; return true;
	default:  return false;
	}
}

void appendOther(QString& record, const std::vector<CourseField>& other)
{
	for (auto const& field : other)
		record += tab + field.letter + field.value;
}

/// The text of a parameter string as the importer keeps it: UTF-8 up to the terminating zero.
QString foreignText(const QByteArray& data)
{
	auto const end = data.indexOf('\0');
	return QString::fromUtf8(data.constData(), end < 0 ? data.size() : end);
}

QString problem(const char* text, const QString& record)
{
	return QCoreApplication::translate("OpenOrienteering::OcdCourse", text).arg(record);
}

}  // namespace



int symbolNumberFromText(const QString& text)
{
	auto const dot = text.indexOf(QLatin1Char('.'));
	auto ok = false;
	auto const major = (dot < 0 ? text : text.left(dot)).toInt(&ok);
	if (!ok || major < 0)
		return 0;
	auto minor = 0;
	if (dot >= 0)
	{
		// Three digits behind the point: "5.01" is 5.010.
		auto const digits = text.mid(dot + 1).leftJustified(3, QLatin1Char('0'), true);
		minor = digits.toInt(&ok);
		if (!ok || minor < 0)
			return 0;
	}
	return major * 1000 + minor;
}

QString symbolNumberToText(int number)
{
	return QString::number(number / 1000) + QLatin1Char('.') + QString::number(number % 1000).rightJustified(3, QLatin1Char('0'));
}


bool parseObject(const QString& record, CourseObject& object)
{
	object = {};
	auto const fields = record.split(tab);
	auto const head = fields.front();
	if (head.size() < 2 || !head[0].isDigit() || !head[1].isDigit())
		return false;
	auto const kind = head.leftRef(2).toInt();
	if (!isKind(kind))
		return false;
	object.kind = CourseObject::Kind(kind);
	object.code = head.mid(2);
	
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (field.isEmpty())
			continue;
		auto const letter = field[0];
		auto const value = field.mid(1);
		switch (letter.unicode())
		{
		case 'Y':
			// The kind once more, as a letter.
			break;
		case 'a':
			if (object.code.isEmpty())
				object.code = value;
			break;
		case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
			object.cell[std::size_t(letter.unicode() - 'c')] = symbolNumberFromText(value);
			break;
		case 't':
			object.text = value;
			break;
		case 'i':
			// The punching unit ids of a start, a control or a finish, joined by semicolons.
			if (object.kind == CourseObject::Start || object.kind == CourseObject::Control || object.kind == CourseObject::Finish)
			{
				for (auto const& id : value.split(QLatin1Char(';'), QString::SkipEmptyParts))
					object.punch_ids.push_back(id.trimmed());
			}
			else
			{
				object.other.push_back({ letter, value });
			}
			break;
		default:
			object.other.push_back({ letter, value });
			break;
		}
	}
	return true;
}

QString formatObject(const CourseObject& object)
{
	QString record = QString::number(int(object.kind)).rightJustified(2, QLatin1Char('0')) + object.code;
	if (auto const letter = kindLetter(object.kind))
	{
		record += tab + QLatin1Char('Y') + QLatin1Char(letter);
		record += tab + QLatin1Char('a') + object.code;
	}
	for (std::size_t column = 0; column < object.cell.size(); ++column)
	{
		if (object.cell[column])
			record += tab + QLatin1Char(char('c' + column)) + symbolNumberToText(object.cell[column]);
	}
	if (!object.text.isEmpty())
		record += tab + QLatin1Char('t') + object.text;
	if (!object.punch_ids.isEmpty())
		record += tab + QLatin1Char('i') + object.punch_ids.join(QLatin1Char(';'));
	appendOther(record, object.other);
	return record;
}


bool parseCourse(const QString& record, Course& course)
{
	course = {};
	auto const fields = record.split(tab);
	course.name = fields.front();
	if (course.name.isEmpty())
		return false;
	
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (field.isEmpty())
			continue;
		auto const letter = field[0];
		auto const value = field.mid(1);
		CourseItem item;
		if (itemFromField(letter, value, item))
		{
			course.items.push_back(item);
			continue;
		}
		switch (letter.unicode())
		{
		case 'Y':
			if (value == QLatin1String("s"))
				course.type = Course::Relay;
			else if (value == QLatin1String("o"))
				course.type = Course::OneManRelay;
			else
				course.type = Course::Normal;
			break;
		case 'L':
			course.legs = qMax(0, value.toInt());
			break;
		case 'C':
			course.climb_m = value.toInt();
			course.climb_manual = true;
			break;
		case 'E':
			course.extra_km = value.toDouble();
			course.extra_set = true;
			break;
		default:
			course.other.push_back({ letter, value });
			break;
		}
	}
	return true;
}

QString formatCourse(const Course& course)
{
	auto record = course.name;
	if (course.type == Course::Relay)
		record += tab + QStringLiteral("Ys");
	else if (course.type == Course::OneManRelay)
		record += tab + QStringLiteral("Yo");
	if (course.legs > 0)
		record += tab + QLatin1Char('L') + QString::number(course.legs);
	for (auto const& item : course.items)
		record += tab + itemText(item);
	if (course.climb_manual)
		record += tab + QLatin1Char('C') + QString::number(course.climb_m);
	if (course.extra_set)
		record += tab + QLatin1Char('E') + QString::number(course.extra_km, 'f', 2);
	appendOther(record, course.other);
	return record;
}


bool parseClass(const QString& record, CourseClass& course_class)
{
	course_class = {};
	auto const fields = record.split(tab);
	course_class.name = fields.front();
	if (course_class.name.isEmpty())
		return false;
	
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (field.isEmpty())
			continue;
		auto const letter = field[0];
		auto const value = field.mid(1);
		switch (letter.unicode())
		{
		case 'c':
			course_class.course = value;
			break;
		case 'r':
			course_class.runners = optionalNumber(value);
			break;
		case 'f':
			course_class.first_no = optionalNumber(value);
			break;
		case 't':
			course_class.last_no = optionalNumber(value);
			break;
		default:
			course_class.other.push_back({ letter, value });
			break;
		}
	}
	return true;
}

QString formatClass(const CourseClass& course_class)
{
	auto record = course_class.name;
	record += tab + QLatin1Char('c') + course_class.course;
	// The three numbers are written also when they are empty.
	record += tab + QLatin1Char('r') + optionalNumberText(course_class.runners);
	record += tab + QLatin1Char('f') + optionalNumberText(course_class.first_no);
	record += tab + QLatin1Char('t') + optionalNumberText(course_class.last_no);
	appendOther(record, course_class.other);
	return record;
}


bool parseOverride(const QString& record, CourseOverride& override)
{
	override = {};
	auto const fields = record.split(tab);
	override.course = fields.front();
	if (override.course.isEmpty())
		return false;
	
	auto has_number = false;
	auto has_from = false;
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (field.isEmpty())
			continue;
		auto const value = field.mid(1);
		switch (field[0].unicode())
		{
		case 'd':
			override.what = CourseOverride::Number;
			override.code_from = value;
			has_number = true;
			break;
		case 'f':
			override.what = CourseOverride::Line;
			override.code_from = value;
			has_from = true;
			break;
		case 't':
			override.code_to = value;
			break;
		default:
			return false;
		}
	}
	if (has_number)
		return !has_from && override.code_to.isEmpty();
	return has_from && !override.code_to.isEmpty();
}

QString formatOverride(const CourseOverride& override)
{
	if (override.what == CourseOverride::Number)
		return override.course + tab + QLatin1Char('d') + override.code_from;
	return override.course + tab + QLatin1Char('f') + override.code_from + tab + QLatin1Char('t') + override.code_to;
}


CourseOptions parseOptions(const QString& record)
{
	CourseOptions options;
	auto const fields = record.split(tab);
	// The record begins with a tab: there is no name before the fields.
	for (int i = 1; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		if (!field.isEmpty())
			options.fields.push_back({ field[0], field.mid(1) });
	}
	return options;
}

QString formatOptions(const CourseOptions& options)
{
	QString record;
	appendOther(record, options.fields);
	return record;
}


CourseData readFromImportedMap(const Map& map, QStringList* problems)
{
	CourseData data;
	auto const report = [problems](const QString& text) {
		if (problems)
			problems->push_back(text);
	};
	
	auto const type_key = QStringLiteral("ocd:object_string_type");
	auto const string_key = QStringLiteral("ocd:object_string");
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* map_object = part->getObject(i);
			auto const record = map_object->getTag(string_key);
			if (record.isEmpty())
				continue;
			auto const type = map_object->getTag(type_key).toInt();
			if (type == 1)
			{
				CourseObject object;
				if (parseObject(record, object))
				{
					object.object = map_object->uuid();
					data.objects.push_back(std::move(object));
				}
				else
				{
					report(problem(QT_TRANSLATE_NOOP("OpenOrienteering::OcdCourse", "Course object which cannot be read: %1"), record));
				}
			}
			else if (type == 2)
			{
				CourseOverride override;
				if (parseOverride(record, override))
				{
					override.object = map_object->uuid();
					data.overrides.push_back(std::move(override));
				}
				else
				{
					report(problem(QT_TRANSLATE_NOOP("OpenOrienteering::OcdCourse", "Change of a course's graphics which cannot be read: %1"), record));
				}
			}
		}
	}
	
	for (auto const& string : map.foreignData().strings)
	{
		switch (string.type)
		{
		case 2:
			{
				Course course;
				auto const record = foreignText(string.data);
				if (parseCourse(record, course))
					data.courses.push_back(std::move(course));
				else
					report(problem(QT_TRANSLATE_NOOP("OpenOrienteering::OcdCourse", "Course which cannot be read: %1"), record));
			}
			break;
		case 3:
			{
				CourseClass course_class;
				auto const record = foreignText(string.data);
				if (parseClass(record, course_class))
					data.classes.push_back(std::move(course_class));
				else
					report(problem(QT_TRANSLATE_NOOP("OpenOrienteering::OcdCourse", "Class which cannot be read: %1"), record));
			}
			break;
		case 31:
			{
				CourseObject object;
				auto const record = foreignText(string.data);
				if (parseObject(record, object))
					data.objects.push_back(std::move(object));
				else
					report(problem(QT_TRANSLATE_NOOP("OpenOrienteering::OcdCourse", "Text block which cannot be read: %1"), record));
			}
			break;
		case 1031:
			data.options = parseOptions(foreignText(string.data));
			break;
		case 1093:
			// The canvases; course_canvas.h.
			canvasesFromOcdXml(string.data, data.canvases);
			break;
		default:
			break;
		}
	}
	return data;
}

void removeReadRecords(Map& map)
{
	auto const type_key = QStringLiteral("ocd:object_string_type");
	auto const string_key = QStringLiteral("ocd:object_string");
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* map_object = part->getObject(i);
			auto const record = map_object->getTag(string_key);
			if (record.isEmpty())
				continue;
			auto const type = map_object->getTag(type_key).toInt();
			CourseObject object;
			CourseOverride override;
			if ((type == 1 && parseObject(record, object)) || (type == 2 && parseOverride(record, override)))
			{
				map_object->removeTag(string_key);
				map_object->removeTag(type_key);
			}
		}
	}
	
	auto foreign = map.foreignData();
	auto const read = [](const ForeignString& string) {
		Course course;
		CourseClass course_class;
		CourseObject object;
		switch (string.type)
		{
		case 2:     return parseCourse(foreignText(string.data), course);
		case 3:     return parseClass(foreignText(string.data), course_class);
		case 31:    return parseObject(foreignText(string.data), object);
		case 1031:  return true;
		case 1093:
			{
				CourseCanvases canvases;
				return canvasesFromOcdXml(string.data, canvases);
			}
		default:    return false;
		}
	};
	foreign.strings.erase(std::remove_if(foreign.strings.begin(), foreign.strings.end(), read), foreign.strings.end());
	map.setForeignData(std::move(foreign));
}


int takeNamedExtents(Map& map)
{
	auto foreign = map.foreignData();
	auto extents = map.extents();
	auto taken = 0;
	auto const read = [&extents, &taken](const ForeignString& string) {
		Extent extent;
		if (string.type != 29 || !extentFromOcdRecord(foreignText(string.data), extent))
			return false;
		auto const known = std::any_of(extents.begin(), extents.end(), [&extent](const Extent& other) { return other.name == extent.name; });
		if (!known)
		{
			extents.push_back(extent);
			++taken;
		}
		return true;
	};
	auto const first = std::remove_if(foreign.strings.begin(), foreign.strings.end(), read);
	if (first == foreign.strings.end())
		return 0;
	foreign.strings.erase(first, foreign.strings.end());
	map.setForeignData(std::move(foreign));
	map.setExtents(std::move(extents));
	return taken;
}

}  // namespace OcdCourse


}  // namespace OpenOrienteering
