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


#include "course_storage.h"

#include <iterator>
#include <utility>

#include <QChar>
#include <QCoreApplication>
#include <QLatin1String>
#include <QString>
#include <QStringRef>
#include <QUuid>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_part.h"
#include "undo/map_data_undo.h"


namespace OpenOrienteering {

namespace {

namespace course_literal
{
	const QLatin1String course_data("course_data");
	const QLatin1String options("options");
	const QLatin1String option("option");
	const QLatin1String course_object("course_object");
	const QLatin1String course("course");
	const QLatin1String item("item");
	const QLatin1String course_class("class");
	const QLatin1String override_element("override");
	const QLatin1String field("field");
	const QLatin1String text("text");
	const QLatin1String comment("comment");
	const QLatin1String punch("punch");
	
	const QLatin1String key("key");
	const QLatin1String value("value");
	const QLatin1String kind("kind");
	const QLatin1String code("code");
	const QLatin1String object("object");
	const QLatin1String locked("locked");
	const QLatin1String id("id");
	const QLatin1String name("name");
	const QLatin1String type("type");
	const QLatin1String legs("legs");
	const QLatin1String count("count");
	const QLatin1String climb("climb");
	const QLatin1String extra("extra");
	const QLatin1String runners("runners");
	const QLatin1String first_no("first_no");
	const QLatin1String last_no("last_no");
	const QLatin1String what("what");
	const QLatin1String from("from");
	const QLatin1String to("to");
	const QLatin1String true_value("true");
	const QLatin1String number("number");
	const QLatin1String line("line");
	const QLatin1String relay("relay");
	const QLatin1String one_man_relay("one_man_relay");
}

struct KindName
{
	CourseObject::Kind kind;
	const char* name;
};

const KindName kind_names[] = {
    { CourseObject::Start,       "start" },
    { CourseObject::Control,     "control" },
    { CourseObject::MarkedRoute, "marked_route" },
    { CourseObject::Finish,      "finish" },
    { CourseObject::Description, "description" },
    { CourseObject::CourseTitle, "course_title" },
    { CourseObject::VariantCode, "variant_code" },
    { CourseObject::StartNumber, "start_number" },
    { CourseObject::TextBlock,   "text_block" },
};

/// The names of CourseItem::Type, in the order of its values.
const char* const item_type_names[] = {
    "start", "control", "marked_route", "finish", "text_block",
    "crossing_point", "passage", "map_exchange", "map_flip",
    "team_fork_begin", "fork_branch", "team_fork_end",
    "leg_fork_begin", "leg_branch", "leg_fork_end",
};

/// The attributes c to h of a course object, in the order of its cells.
const char* const cell_names[] = { "c", "d", "e", "f", "g", "h" };

QString kindName(CourseObject::Kind kind)
{
	for (auto const& entry : kind_names)
	{
		if (entry.kind == kind)
			return QString::fromLatin1(entry.name);
	}
	return {};
}

bool kindFromName(const QStringRef& name, CourseObject::Kind& kind)
{
	for (auto const& entry : kind_names)
	{
		if (name == QLatin1String(entry.name))
		{
			kind = entry.kind;
			return true;
		}
	}
	return false;
}

bool itemTypeFromName(const QStringRef& name, CourseItem::Type& type)
{
	for (std::size_t i = 0; i < std::size_t(std::end(item_type_names) - std::begin(item_type_names)); ++i)
	{
		if (name == QLatin1String(item_type_names[i]))
		{
			type = CourseItem::Type(i);
			return true;
		}
	}
	return false;
}

void saveFields(const std::vector<CourseField>& fields, const QLatin1String& element, QXmlStreamWriter& xml)
{
	for (auto const& field : fields)
	{
		xml.writeEmptyElement(element);
		xml.writeAttribute(course_literal::key, QString(field.letter));
		xml.writeAttribute(course_literal::value, field.value);
	}
}

/// Reads a field or option element, and leaves the reader behind it.
bool loadField(QXmlStreamReader& xml, CourseField& field)
{
	auto const attributes = xml.attributes();
	auto const key = attributes.value(course_literal::key);
	auto const ok = key.size() == 1;
	if (ok)
		field = { key.at(0), attributes.value(course_literal::value).toString() };
	xml.skipCurrentElement();
	return ok;
}

void report(QStringList* problems, const char* text, const QString& what)
{
	if (problems)
		problems->push_back(QCoreApplication::translate("OpenOrienteering::CourseData", text).arg(what));
}

}  // namespace



void saveCourseData(const CourseData& data, QXmlStreamWriter& xml)
{
	xml.writeStartElement(course_literal::course_data);
	
	if (!data.options.fields.empty())
	{
		xml.writeStartElement(course_literal::options);
		saveFields(data.options.fields, course_literal::option, xml);
		xml.writeEndElement();
	}
	if (!data.canvases.isEmpty())
		saveCanvases(data.canvases, xml);
	if (!data.route.isDefault())
		saveRouteSettings(data.route, xml);
	
	for (auto const& object : data.objects)
	{
		xml.writeStartElement(course_literal::course_object);
		xml.writeAttribute(course_literal::kind, kindName(object.kind));
		xml.writeAttribute(course_literal::code, object.code);
		if (!object.object.isNull())
			xml.writeAttribute(course_literal::object, object.object.toString());
		for (std::size_t column = 0; column < object.cell.size(); ++column)
		{
			if (object.cell[column])
				xml.writeAttribute(QLatin1String(cell_names[column]), QString::number(object.cell[column]));
		}
		if (object.locked)
			xml.writeAttribute(course_literal::locked, course_literal::true_value);
		// As an element: the text of a block may have line breaks.
		if (!object.text.isEmpty())
			xml.writeTextElement(course_literal::text, object.text);
		for (auto const& id : object.punch_ids)
		{
			xml.writeEmptyElement(course_literal::punch);
			xml.writeAttribute(course_literal::id, id);
		}
		saveFields(object.other, course_literal::field, xml);
		xml.writeEndElement();
	}
	
	for (auto const& course : data.courses)
	{
		xml.writeStartElement(course_literal::course);
		xml.writeAttribute(course_literal::name, course.name);
		if (course.type == Course::Relay)
			xml.writeAttribute(course_literal::type, course_literal::relay);
		else if (course.type == Course::OneManRelay)
			xml.writeAttribute(course_literal::type, course_literal::one_man_relay);
		if (course.legs > 0)
			xml.writeAttribute(course_literal::legs, QString::number(course.legs));
		if (course.climb_manual)
			xml.writeAttribute(course_literal::climb, QString::number(course.climb_m));
		if (course.extra_set)
			xml.writeAttribute(course_literal::extra, QString::number(course.extra_km, 'g', 10));
		for (auto const& item : course.items)
		{
			xml.writeEmptyElement(course_literal::item);
			xml.writeAttribute(course_literal::type, QLatin1String(item_type_names[int(item.type)]));
			if (!item.code.isEmpty())
				xml.writeAttribute(course_literal::code, item.code);
			if (!item.legs.isEmpty())
				xml.writeAttribute(course_literal::legs, item.legs);
			if (item.count)
				xml.writeAttribute(course_literal::count, QString::number(item.count));
		}
		saveFields(course.other, course_literal::field, xml);
		xml.writeEndElement();
	}
	
	for (auto const& course_class : data.classes)
	{
		xml.writeStartElement(course_literal::course_class);
		xml.writeAttribute(course_literal::name, course_class.name);
		xml.writeAttribute(course_literal::course, course_class.course);
		if (course_class.runners >= 0)
			xml.writeAttribute(course_literal::runners, QString::number(course_class.runners));
		if (course_class.first_no >= 0)
			xml.writeAttribute(course_literal::first_no, QString::number(course_class.first_no));
		if (course_class.last_no >= 0)
			xml.writeAttribute(course_literal::last_no, QString::number(course_class.last_no));
		if (!course_class.comment.isEmpty())
			xml.writeTextElement(course_literal::comment, course_class.comment);
		saveFields(course_class.other, course_literal::field, xml);
		xml.writeEndElement();
	}
	
	for (auto const& override : data.overrides)
	{
		xml.writeEmptyElement(course_literal::override_element);
		xml.writeAttribute(course_literal::course, override.course);
		xml.writeAttribute(course_literal::what, override.what == CourseOverride::Line ? course_literal::line : course_literal::number);
		xml.writeAttribute(course_literal::from, override.code_from);
		if (!override.code_to.isEmpty())
			xml.writeAttribute(course_literal::to, override.code_to);
		if (!override.object.isNull())
			xml.writeAttribute(course_literal::object, override.object.toString());
	}
	
	xml.writeEndElement();
}

CourseData loadCourseData(QXmlStreamReader& xml, QStringList* problems)
{
	CourseData data;
	while (xml.readNextStartElement())
	{
		auto const element = xml.name();
		auto const attributes = xml.attributes();
		if (element == course_literal::options)
		{
			while (xml.readNextStartElement())
			{
				CourseField field;
				if (xml.name() == course_literal::option)
				{
					if (loadField(xml, field))
						data.options.fields.push_back(field);
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
		}
		else if (element == course_literal::course_object)
		{
			CourseObject object;
			auto const known = kindFromName(attributes.value(course_literal::kind), object.kind);
			object.code = attributes.value(course_literal::code).toString();
			object.object = QUuid(attributes.value(course_literal::object).toString());
			for (std::size_t column = 0; column < object.cell.size(); ++column)
				object.cell[column] = attributes.value(QLatin1String(cell_names[column])).toInt();
			object.locked = attributes.value(course_literal::locked) == course_literal::true_value;
			while (xml.readNextStartElement())
			{
				CourseField field;
				if (xml.name() == course_literal::text)
					object.text = xml.readElementText();
				else if (xml.name() == course_literal::punch)
				{
					object.punch_ids.push_back(xml.attributes().value(course_literal::id).toString());
					xml.skipCurrentElement();
				}
				else if (xml.name() == course_literal::field)
				{
					if (loadField(xml, field))
						object.other.push_back(field);
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
			if (known)
				data.objects.push_back(std::move(object));
			else
				report(problems, QT_TRANSLATE_NOOP("OpenOrienteering::CourseData", "Course object of a kind which is not known: %1"), object.code);
		}
		else if (element == course_literal::course)
		{
			Course course;
			course.name = attributes.value(course_literal::name).toString();
			auto const type = attributes.value(course_literal::type);
			if (type == course_literal::relay)
				course.type = Course::Relay;
			else if (type == course_literal::one_man_relay)
				course.type = Course::OneManRelay;
			course.legs = attributes.value(course_literal::legs).toInt();
			if (attributes.hasAttribute(course_literal::climb))
			{
				course.climb_m = attributes.value(course_literal::climb).toInt();
				course.climb_manual = true;
			}
			if (attributes.hasAttribute(course_literal::extra))
			{
				course.extra_km = attributes.value(course_literal::extra).toDouble();
				course.extra_set = true;
			}
			while (xml.readNextStartElement())
			{
				CourseField field;
				if (xml.name() == course_literal::item)
				{
					auto const item_attributes = xml.attributes();
					CourseItem item;
					if (itemTypeFromName(item_attributes.value(course_literal::type), item.type))
					{
						item.code = item_attributes.value(course_literal::code).toString();
						item.legs = item_attributes.value(course_literal::legs).toString();
						item.count = item_attributes.value(course_literal::count).toInt();
						course.items.push_back(item);
					}
					else
					{
						report(problems, QT_TRANSLATE_NOOP("OpenOrienteering::CourseData", "Item of a type which is not known, in the course %1"), course.name);
					}
					xml.skipCurrentElement();
				}
				else if (xml.name() == course_literal::field)
				{
					if (loadField(xml, field))
						course.other.push_back(field);
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
			data.courses.push_back(std::move(course));
		}
		else if (element == course_literal::course_class)
		{
			CourseClass course_class;
			course_class.name = attributes.value(course_literal::name).toString();
			course_class.course = attributes.value(course_literal::course).toString();
			if (attributes.hasAttribute(course_literal::runners))
				course_class.runners = attributes.value(course_literal::runners).toInt();
			if (attributes.hasAttribute(course_literal::first_no))
				course_class.first_no = attributes.value(course_literal::first_no).toInt();
			if (attributes.hasAttribute(course_literal::last_no))
				course_class.last_no = attributes.value(course_literal::last_no).toInt();
			while (xml.readNextStartElement())
			{
				CourseField field;
				if (xml.name() == course_literal::comment)
					course_class.comment = xml.readElementText();
				else if (xml.name() == course_literal::field)
				{
					if (loadField(xml, field))
						course_class.other.push_back(field);
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
			data.classes.push_back(std::move(course_class));
		}
		else if (element == QLatin1String("canvases"))
		{
			data.canvases = loadCanvases(xml);
		}
		else if (element == QLatin1String("route_settings"))
		{
			data.route = loadRouteSettings(xml);
		}
		else if (element == course_literal::override_element)
		{
			CourseOverride override;
			override.course = attributes.value(course_literal::course).toString();
			override.what = attributes.value(course_literal::what) == course_literal::line ? CourseOverride::Line : CourseOverride::Number;
			override.code_from = attributes.value(course_literal::from).toString();
			override.code_to = attributes.value(course_literal::to).toString();
			override.object = QUuid(attributes.value(course_literal::object).toString());
			xml.skipCurrentElement();
			data.overrides.push_back(std::move(override));
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	return data;
}

QByteArray courseDataToXml(const CourseData& data)
{
	QByteArray bytes;
	QXmlStreamWriter xml(&bytes);
	saveCourseData(data, xml);
	return bytes;
}

CourseData courseDataFromXml(const QByteArray& xml_data)
{
	QXmlStreamReader xml(xml_data);
	if (xml.readNextStartElement() && xml.name() == course_literal::course_data)
		return loadCourseData(xml);
	return {};
}

int findCourseOverridesPart(const Map& map)
{
	return map.findPartIndexWithRole(MapPart::Role::Course);
}

MapPart* ensureCourseOverridesPart(Map& map)
{
	auto const index = findCourseOverridesPart(map);
	if (index >= 0)
		return map.getPart(std::size_t(index));
	auto* part = new MapPart(QCoreApplication::translate("OpenOrienteering::CourseData", "Course overrides"), &map);
	part->setRole(MapPart::Role::Course);
	// Hidden before it is in the map, so that no change of the map is signalled.
	part->setVisible(false);
	map.addPart(part, std::size_t(map.getNumParts()));
	return part;
}

void changeCourseData(Map& map, CourseData data)
{
	if (data == map.courseData())
		return;
	auto* step = new MapDataUndoStep(&map, MapDataUndoStep::Courses, MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses));
	map.setCourseData(std::move(data));
	map.push(step);
	map.setOtherDirty();
}


}  // namespace OpenOrienteering
