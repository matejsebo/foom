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


#include "course_report.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <tuple>
#include <utility>

#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>
#include <QUuid>
#include <QVector>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "course/course_edit.h"
#include "course/course_parts.h"
#include "course/course_relay.h"
#include "course/course_render.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseReport", text);
}

const QString& listSeparator()
{
	static const QString separator = QStringLiteral(", ");
	return separator;
}

/// The description symbols which the rules name, as the cells have them.
constexpr int crossing = 10001;
constexpr int junction = 10002;
constexpr int between = 12014;

bool hasVariation(const Course& course)
{
	return std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) {
		return isVariationItem(item.type);
	});
}

/// The items which have a place in the map.
bool hasPlace(CourseItem::Type type)
{
	return type == CourseItem::Start || type == CourseItem::Control
	       || type == CourseItem::MarkedRoute || type == CourseItem::Finish;
}

bool hasPlace(CourseObject::Kind kind)
{
	return kind == CourseObject::Start || kind == CourseObject::Control
	       || kind == CourseObject::MarkedRoute || kind == CourseObject::Finish;
}

int runnersOf(const CourseData& data, const QString& course)
{
	auto sum = 0;
	for (auto const& course_class : data.classes)
	{
		if (course_class.course == course)
			sum += std::max(0, course_class.runners);
	}
	return sum;
}

/// The number at the end of a code; 0 when there is none.
int trailingNumber(const QString& code)
{
	auto first = code.size();
	while (first > 0 && code.at(first - 1).isDigit())
		--first;
	return code.mid(first).toInt();
}

bool codeLess(const QString& lhs, const QString& rhs)
{
	return std::make_tuple(trailingNumber(lhs), lhs) < std::make_tuple(trailingNumber(rhs), rhs);
}

/// Where a course object is entered and where it is left.
struct Place
{
	bool known = false;
	QPointF entry;
	QPointF exit;
	QUuid uuid;
};

Place placeOf(const Map& map, const CourseData& data, const QString& code)
{
	Place place;
	auto const* course_object = data.findObject(code);
	if (!course_object || course_object->object.isNull())
		return place;
	auto const* object = map.findObject(course_object->object);
	if (!object || object->getRawCoordinateVector().empty())
		return place;
	auto const& coords = object->getRawCoordinateVector();
	place.known = true;
	place.uuid = course_object->object;
	place.entry = MapCoordF(coords.front());
	place.exit = object->getType() == Object::Path ? QPointF(MapCoordF(coords.back())) : place.entry;
	return place;
}

double distance(const QPointF& a, const QPointF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QString metres(double length)
{
	return length < 0.0 ? QStringLiteral("-") : tr("%1 m").arg(qRound(length));
}

/// The codes of the control items of a course, in their order.
QStringList controlsOf(const Course& course)
{
	QStringList codes;
	for (auto const& item : course.items)
	{
		if (item.type == CourseItem::Control)
			codes.push_back(item.code);
	}
	return codes;
}

Issue courseIssue(const char* rule, Issue::Severity severity, const QString& course, const QString& message)
{
	Issue issue;
	issue.rule = rule;
	issue.severity = severity;
	issue.subject = course;
	issue.message = message;
	return issue;
}

Issue controlIssue(const char* rule, const Map& map, const CourseData& data, const QString& code, const QString& message)
{
	Issue issue;
	issue.rule = rule;
	issue.severity = Issue::Warning;
	issue.subject = code;
	issue.message = message;
	auto const place = placeOf(map, data, code);
	if (place.known)
	{
		issue.objects.append(place.uuid);
		issue.where = MapCoordF(place.entry);
	}
	return issue;
}

Issue pairIssue(const char* rule, const Map& map, const CourseData& data, const QString& first, const QString& second, const QString& message)
{
	Issue issue;
	issue.rule = rule;
	issue.severity = Issue::Warning;
	issue.subject = first + listSeparator() + second;
	issue.message = message;
	auto const a = placeOf(map, data, first);
	auto const b = placeOf(map, data, second);
	if (a.known)
		issue.objects.append(a.uuid);
	if (b.known)
		issue.objects.append(b.uuid);
	if (a.known && b.known)
		issue.where = MapCoordF((a.entry + b.entry) / 2.0);
	return issue;
}

/// A feature of the columns D and E: a symbol of the groups 1 to 7.
bool isFeature(int cell)
{
	return cell >= 1000 && cell < 8000;
}

}  // namespace



double statisticsControlDistance(const Map& map)
{
	return map.getScaleDenominator() <= 5000 ? 30.0 : 60.0;
}


double checkControlDistance(const Map& map)
{
	return map.getScaleDenominator() <= 5000 ? 25.0 : 30.0;
}


CourseStatistics courseStatistics(const Map& map, const CourseData& data, double close_limit_m)
{
	CourseStatistics statistics;
	auto const metres_per_mm = map.getScaleDenominator() / 1000.0;
	
	// The courses.
	for (auto const& course : data.courses)
	{
		CourseStatistics::CourseRow row;
		row.name = course.name;
		row.type = course.type;
		row.has_variation = hasVariation(course);
		for (auto const& course_class : data.classes)
		{
			if (course_class.course == course.name)
				row.classes.push_back(course_class.name);
		}
		row.runners = runnersOf(data, course.name);
		auto const length = measureCourse(map, data, course);
		row.measured = length.known;
		row.length_m = length.total_m;
		row.climb_m = course.climb_m;
		row.controls = controlsOf(course).size();
		statistics.courses.push_back(row);
		
		auto const has_start = std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) { return item.type == CourseItem::Start; });
		auto const has_finish = std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) { return item.type == CourseItem::Finish; });
		if (!has_start || !has_finish)
			statistics.without_start_or_finish.push_back(course.name);
		if (row.has_variation)
			statistics.not_verified.push_back(course.name);
	}
	
	// The course objects.
	for (auto const& object : data.objects)
	{
		if (!hasPlace(object.kind))
			continue;
		CourseStatistics::ObjectRow row;
		row.code = object.code;
		row.kind = object.kind;
		row.courses = coursesUsing(data, object.code);
		for (auto const& course : qAsConst(row.courses))
			row.runners += runnersOf(data, course);
		statistics.objects.push_back(row);
	}
	std::stable_sort(begin(statistics.objects), end(statistics.objects), [](const CourseStatistics::ObjectRow& lhs, const CourseStatistics::ObjectRow& rhs) {
		return std::make_tuple(int(lhs.kind), trailingNumber(lhs.code), lhs.code) < std::make_tuple(int(rhs.kind), trailingNumber(rhs.code), rhs.code);
	});
	
	// The legs of the courses without variations.
	std::map<std::pair<QString, QString>, std::size_t> leg_index;
	for (auto const& course : data.courses)
	{
		if (hasVariation(course))
			continue;
		const CourseItem* previous = nullptr;
		for (auto const& item : course.items)
		{
			if (!hasPlace(item.type))
				continue;
			if (previous)
			{
				auto const key = std::make_pair(previous->code, item.code);
				auto found = leg_index.find(key);
				if (found == leg_index.end())
				{
					CourseStatistics::LegRow row;
					row.from = previous->code;
					row.to = item.code;
					auto const from = placeOf(map, data, row.from);
					auto const to = placeOf(map, data, row.to);
					if (from.known && to.known)
						row.length_m = distance(from.exit, to.entry) * metres_per_mm;
					found = leg_index.emplace(key, statistics.legs.size()).first;
					statistics.legs.push_back(row);
				}
				auto& row = statistics.legs[found->second];
				if (!row.courses.contains(course.name))
				{
					row.courses.push_back(course.name);
					row.runners += runnersOf(data, course.name);
				}
			}
			previous = &item;
		}
	}
	
	// The directions, before the legs are put in their order.
	for (auto const& leg : statistics.legs)
	{
		if (!codeLess(leg.from, leg.to))
			continue;
		auto const back = leg_index.find(std::make_pair(leg.to, leg.from));
		if (back == leg_index.end())
			continue;
		auto const* from = data.findObject(leg.from);
		auto const* to = data.findObject(leg.to);
		if (!from || !to || from->kind != CourseObject::Control || to->kind != CourseObject::Control)
			continue;
		CourseStatistics::OppositeLegs opposite;
		opposite.first = leg.from;
		opposite.second = leg.to;
		opposite.forward = leg.courses;
		opposite.backward = statistics.legs[back->second].courses;
		statistics.opposite.push_back(opposite);
	}
	std::stable_sort(begin(statistics.opposite), end(statistics.opposite), [](const CourseStatistics::OppositeLegs& lhs, const CourseStatistics::OppositeLegs& rhs) {
		return codeLess(lhs.first, rhs.first);
	});
	std::stable_sort(begin(statistics.legs), end(statistics.legs), [](const CourseStatistics::LegRow& lhs, const CourseStatistics::LegRow& rhs) {
		return lhs.courses.size() > rhs.courses.size();
	});
	
	// The controls and the courses.
	for (auto const& object : statistics.objects)
	{
		if (object.kind == CourseObject::Control)
			statistics.matrix_controls.push_back(object.code);
	}
	for (auto const& course : data.courses)
		statistics.matrix_courses.push_back(course.name);
	for (auto const& control : qAsConst(statistics.matrix_controls))
	{
		QStringList row;
		for (auto const& course : data.courses)
		{
			auto const codes = controlsOf(course);
			QStringList positions;
			for (int i = 0; i < codes.size(); ++i)
			{
				if (codes[i] == control)
					positions.push_back(QString::number(i + 1));
			}
			if (positions.isEmpty())
				row.push_back(QString{});
			else if (hasVariation(course))
				row.push_back(QStringLiteral("x"));
			else
				row.push_back(positions.join(QLatin1Char('/')));
		}
		statistics.matrix.push_back(row);
	}
	
	// The event.
	statistics.control_count = statistics.matrix_controls.size();
	statistics.course_count = int(data.courses.size());
	statistics.class_count = int(data.classes.size());
	
	statistics.close_limit_m = close_limit_m > 0.0 ? close_limit_m : statisticsControlDistance(map);
	std::vector<std::pair<QString, Place>> places;
	for (auto const& control : qAsConst(statistics.matrix_controls))
	{
		auto const place = placeOf(map, data, control);
		if (place.known)
			places.emplace_back(control, place);
	}
	for (std::size_t i = 0; i < places.size(); ++i)
	{
		for (std::size_t j = i + 1; j < places.size(); ++j)
		{
			auto const apart = distance(places[i].second.entry, places[j].second.entry) * metres_per_mm;
			if (apart >= statistics.close_limit_m)
				continue;
			CourseStatistics::ClosePair pair;
			pair.first = places[i].first;
			pair.second = places[j].first;
			pair.distance_m = apart;
			statistics.close_pairs.push_back(pair);
		}
	}
	std::stable_sort(begin(statistics.close_pairs), end(statistics.close_pairs), [](const CourseStatistics::ClosePair& lhs, const CourseStatistics::ClosePair& rhs) {
		return lhs.distance_m < rhs.distance_m;
	});
	
	std::vector<bool> grouped(data.courses.size(), false);
	for (std::size_t i = 0; i < data.courses.size(); ++i)
	{
		if (grouped[i] || data.courses[i].items.empty())
			continue;
		QStringList group { data.courses[i].name };
		for (std::size_t j = i + 1; j < data.courses.size(); ++j)
		{
			if (!grouped[j] && data.courses[j].items == data.courses[i].items)
			{
				grouped[j] = true;
				group.push_back(data.courses[j].name);
			}
		}
		if (group.size() > 1)
			statistics.identical.push_back(group);
	}
	
	return statistics;
}



QString courseEventText(const CourseStatistics& statistics)
{
	QStringList lines;
	const QLatin1Char tab('\t');
	
	lines << tr("Event");
	lines << tr("Number of controls:") + tab + QString::number(statistics.control_count);
	lines << tr("Number of courses:") + tab + QString::number(statistics.course_count);
	lines << tr("Number of classes:") + tab + QString::number(statistics.class_count);
	
	lines << QString{} << tr("Controls which are closer to each other than %1").arg(metres(statistics.close_limit_m));
	for (auto const& pair : statistics.close_pairs)
		lines << pair.first + tab + pair.second + tab + metres(pair.distance_m);
	if (statistics.close_pairs.empty())
		lines << tr("None.");
	
	lines << QString{} << tr("Legs between two controls in both directions");
	for (auto const& opposite : statistics.opposite)
		lines << opposite.first + tab + opposite.second + tab + opposite.forward.join(listSeparator()) + tab + opposite.backward.join(listSeparator());
	if (statistics.opposite.empty())
		lines << tr("None.");
	
	lines << QString{} << tr("Identical courses");
	for (auto const& group : statistics.identical)
		lines << group.join(listSeparator());
	if (statistics.identical.empty())
		lines << tr("None.");
	
	lines << QString{} << tr("Courses without start or finish");
	for (auto const& course : statistics.without_start_or_finish)
		lines << course;
	if (statistics.without_start_or_finish.isEmpty())
		lines << tr("None.");
	
	if (!statistics.not_verified.isEmpty())
	{
		lines << QString{} << tr("Courses with variations, which are not looked at for legs and directions");
		for (auto const& course : statistics.not_verified)
			lines << course;
	}
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}


QString courseStatisticsText(const CourseStatistics& statistics)
{
	QStringList lines;
	const QLatin1Char tab('\t');
	
	lines << tr("Course and event statistics") << QString{};
	
	lines << tr("Courses");
	lines << QStringList({ tr("Course"), tr("Classes"), tr("Runners"), tr("Length"), tr("Climb"), tr("Controls") }).join(tab);
	for (auto const& course : statistics.courses)
	{
		lines << QStringList({ course.name, course.classes.join(listSeparator()), QString::number(course.runners),
		                       course.measured ? formatCourseLength(course.length_m) : QStringLiteral("-"),
		                       metres(course.climb_m), QString::number(course.controls) }).join(tab);
	}
	
	lines << QString{} << tr("Course objects");
	lines << QStringList({ tr("Code"), tr("Courses"), tr("Runners"), tr("In the courses") }).join(tab);
	for (auto const& object : statistics.objects)
		lines << QStringList({ object.code, QString::number(object.courses.size()), QString::number(object.runners), object.courses.join(listSeparator()) }).join(tab);
	
	lines << QString{} << tr("Legs");
	lines << QStringList({ tr("From"), tr("To"), tr("Length"), tr("Courses"), tr("Runners"), tr("In the courses") }).join(tab);
	for (auto const& leg : statistics.legs)
		lines << QStringList({ leg.from, leg.to, metres(leg.length_m), QString::number(leg.courses.size()), QString::number(leg.runners), leg.courses.join(listSeparator()) }).join(tab);
	
	lines << QString{} << tr("Controls and courses");
	lines << (QStringList(tr("Control")) + statistics.matrix_courses).join(tab);
	for (int i = 0; i < statistics.matrix_controls.size(); ++i)
		lines << (QStringList(statistics.matrix_controls[i]) + statistics.matrix[std::size_t(i)]).join(tab);
	
	lines << QString{};
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n') + courseEventText(statistics);
}



std::vector<Issue> checkCourses(const Map& map, const CourseData& data, double close_limit_m)
{
	std::vector<Issue> issues;
	if (data.isEmpty())
		return issues;
	auto const limit = close_limit_m > 0.0 ? close_limit_m : checkControlDistance(map);
	auto const statistics = courseStatistics(map, data, limit);
	
	// The courses.
	for (auto const& course : data.courses)
	{
		auto const has_start = std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) { return item.type == CourseItem::Start; });
		auto const has_finish = std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) { return item.type == CourseItem::Finish; });
		if (!has_start)
			issues.push_back(courseIssue(rule::course_start_missing, Issue::Error, course.name, tr("The course %1 has no start.").arg(course.name)));
		if (!has_finish)
			issues.push_back(courseIssue(rule::course_finish_missing, Issue::Error, course.name, tr("The course %1 has no finish.").arg(course.name)));
		if (course.type == Course::Normal && !hasVariation(course) && course.climb_m == 0)
			issues.push_back(courseIssue(rule::course_climb_zero, Issue::Warning, course.name, tr("The climb of the course %1 is 0 m.").arg(course.name)));
		// the design notes, "Relays: variations, variants and their allocation"
		for (auto const& fault : relayStructure(course).faults)
			issues.push_back(courseIssue(rule::relay_structure, Issue::Error, course.name, tr("Course %1: %2").arg(course.name, fault)));
		// the design notes, "The parts of a course: map exchange and flip"
		auto const part_fault = coursePartFault(course);
		if (!part_fault.isEmpty())
			issues.push_back(courseIssue(rule::course_part, Issue::Warning, course.name, tr("Course %1: %2").arg(course.name, part_fault)));
	}
	for (auto const& group : statistics.identical)
		issues.push_back(courseIssue(rule::course_identical, Issue::Warning, group.join(listSeparator()), tr("These courses are identical: %1.").arg(group.join(listSeparator()))));
	for (auto const& course : statistics.not_verified)
		issues.push_back(courseIssue(rule::course_not_checked, Issue::Info, course, tr("The course %1 has variations; its legs, their directions and its punching units are not checked.").arg(course)));
	
	// The controls.
	for (auto const& object : statistics.objects)
	{
		if (object.kind == CourseObject::Control && object.courses.isEmpty())
			issues.push_back(controlIssue(rule::control_unused, map, data, object.code, tr("The control %1 is in no course.").arg(object.code)));
	}
	for (auto const& pair : statistics.close_pairs)
	{
		auto issue = pairIssue(rule::control_too_close, map, data, pair.first, pair.second,
		                       tr("The controls %1 and %2 are %3 apart, closer than %4.").arg(pair.first, pair.second, metres(pair.distance_m), metres(limit)));
		issue.measured = pair.distance_m;
		issue.limit = limit;
		issues.push_back(issue);
	}
	for (auto const& opposite : statistics.opposite)
	{
		issues.push_back(pairIssue(rule::leg_opposite, map, data, opposite.first, opposite.second,
		                           tr("Between the controls %1 and %2 courses run in both directions: %3 the one way, %4 the other.")
		                           .arg(opposite.first, opposite.second, opposite.forward.join(listSeparator()), opposite.backward.join(listSeparator()))));
	}
	
	// The descriptions of the controls.
	std::vector<Issue> de_missing;
	std::vector<Issue> fg_missing;
	for (auto const& row : statistics.objects)
	{
		if (row.kind != CourseObject::Control)
			continue;
		auto const& object = *data.findObject(row.code);
		auto const d = object.cell[CourseObject::ColumnD];
		auto const e = object.cell[CourseObject::ColumnE];
		auto const f = object.cell[CourseObject::ColumnF];
		auto const g = object.cell[CourseObject::ColumnG];
		auto const joins = f == crossing || f == junction || g == between;
		if (d == 0)
			issues.push_back(controlIssue(rule::description_d_empty, map, data, row.code, tr("Control %1: column D needs a symbol.").arg(row.code)));
		if (joins && (!isFeature(d) || !isFeature(e)))
			de_missing.push_back(controlIssue(rule::description_de_missing, map, data, row.code, tr("Control %1: columns D and E both need a feature, because column F says crossing or junction, or column G says between.").arg(row.code)));
		if (!joins && isFeature(d) && isFeature(e))
			fg_missing.push_back(controlIssue(rule::description_fg_missing, map, data, row.code, tr("Control %1: columns D and E both have a feature, so column F needs crossing or junction, or column G needs between.").arg(row.code)));
	}
	issues.insert(end(issues), begin(de_missing), end(de_missing));
	issues.insert(end(issues), begin(fg_missing), end(fg_missing));
	
	// The punching units, in the courses without variations.
	std::vector<Issue> repeated;
	for (auto const& course : data.courses)
	{
		if (hasVariation(course))
			continue;
		auto const controls = controlsOf(course);
		QStringList ids;
		for (auto const& code : controls)
		{
			if (auto const* object = data.findObject(code))
			{
				for (auto const& id : object->punch_ids)
				{
					if (!ids.contains(id))
						ids.push_back(id);
				}
			}
		}
		auto const has_id = [&data](const QString& code, const QString& id) {
			auto const* object = data.findObject(code);
			return object && object->punch_ids.contains(id);
		};
		for (auto const& id : qAsConst(ids))
		{
			QStringList pairs;
			QStringList with_id;
			for (int i = 0; i < controls.size(); ++i)
			{
				if (!has_id(controls[i], id))
					continue;
				if (!with_id.contains(controls[i]))
					with_id.push_back(controls[i]);
				if (i + 1 < controls.size() && controls[i + 1] != controls[i] && has_id(controls[i + 1], id))
					pairs.push_back(controls[i] + QLatin1Char('-') + controls[i + 1]);
			}
			if (!pairs.isEmpty())
				issues.push_back(courseIssue(rule::punch_consecutive, Issue::Error, course.name, tr("Course %1: the punching unit %2 is at controls which follow each other (%3).").arg(course.name, id, pairs.join(listSeparator()))));
			else if (with_id.size() > 1)
				repeated.push_back(courseIssue(rule::punch_repeated, Issue::Warning, course.name, tr("Course %1: the punching unit %2 is at more than one control (%3).").arg(course.name, id, with_id.join(listSeparator()))));
		}
	}
	issues.insert(end(issues), begin(repeated), end(repeated));
	
	return issues;
}


}  // namespace OpenOrienteering
