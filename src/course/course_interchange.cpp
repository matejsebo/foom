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


#include "course_interchange.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <Qt>
#include <QBuffer>
#include <QChar>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSaveFile>
#include <QSizeF>
#include <QUuid>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "mapper_config.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"
#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_edit.h"
#include "course/course_map.h"
#include "course/course_relay.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "undo/map_data_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "util/util.h"

#ifdef MAPPER_USE_GDAL
#  include "gdal/kmz_archive.h"
#  include "gdal/kmz_groundoverlay_export.h"
#endif

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseInterchange", text);
}

const QLatin1String iof_namespace("http://www.orienteering.org/datastandard/3.0");

QString creator()
{
	return QStringLiteral("FOOM " APP_VERSION);
}

/// Start, control and finish: what the standard calls controls.
bool isControlKind(CourseObject::Kind kind)
{
	return kind == CourseObject::Start || kind == CourseObject::Control || kind == CourseObject::Finish;
}

bool isControlItem(CourseItem::Type type)
{
	return type == CourseItem::Start || type == CourseItem::Control || type == CourseItem::Finish;
}

QLatin1String typeName(CourseObject::Kind kind)
{
	switch (kind)
	{
	case CourseObject::Start:   return QLatin1String("Start");
	case CourseObject::Finish:  return QLatin1String("Finish");
	default:                    return QLatin1String("Control");
	}
}

QLatin1String typeName(CourseItem::Type type)
{
	switch (type)
	{
	case CourseItem::Start:   return QLatin1String("Start");
	case CourseItem::Finish:  return QLatin1String("Finish");
	default:                  return QLatin1String("Control");
	}
}

bool isGeoreferenced(const Map& map)
{
	return map.getGeoreferencing().getState() == Georeferencing::Geospatial;
}

bool isFunnel(const CourseObject& object)
{
	return std::any_of(begin(object.other), end(object.other), [](const CourseField& field) {
		return field.letter == QLatin1Char('m') && field.value == QLatin1String("f");
	});
}

double distance(const QPointF& a, const QPointF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// Where a course object lies.
struct Place
{
	bool known = false;
	MapCoordF entry;
	MapCoordF exit;
	/// The length of a marked route along its path, in mm.
	double path_mm = 0.0;
	/// The point, or the points of the path.
	std::vector<MapCoordF> points;
};

Place placeOf(const Map& map, const CourseData& data, const QString& code)
{
	Place place;
	auto const* course_object = data.findObject(code);
	if (!course_object || course_object->object.isNull())
		return place;
	auto const* object = map.findObject(course_object->object);
	if (!object)
		return place;
	if (object->getType() == Object::Point)
	{
		place.points.push_back(object->asPoint()->getCoordF());
	}
	else if (object->getType() == Object::Path)
	{
		auto const* path = object->asPath();
		path->update();
		for (auto const& part : path->parts())
		{
			for (auto const& path_coord : part.path_coords)
				place.points.push_back(path_coord.pos);
		}
		place.path_mm = path->getPaperLength();
	}
	if (place.points.empty())
		return place;
	place.known = true;
	place.entry = place.points.front();
	place.exit = place.points.back();
	return place;
}

/// A start, a control or a finish in a course, with what lies before it.
struct Visit
{
	QString code;
	CourseItem::Type type = CourseItem::Control;
	/// From the visit before, the marked routes along their paths, in m; negative when it is not known or there is none before.
	double leg_m = -1.0;
	/// The marked routes before it, in m; negative when there is none.
	double taped_m = -1.0;
	bool funnel = false;
	bool crossing = false;
	bool passage = false;
};

/// The visits of a course without variations, and the points of its track.
std::vector<Visit> visitsOf(const Map& map, const CourseData& data, const Course& course, std::vector<MapCoordF>* track = nullptr)
{
	std::vector<Visit> visits;
	auto const metres_per_mm = map.getScaleDenominator() / 1000.0;
	QPointF last_exit;
	auto last_known = false;
	auto chain_known = true;
	auto pending_mm = 0.0;
	auto route_mm = 0.0;
	auto route = false;
	auto funnel = false;
	auto crossing = false;
	auto passage = false;
	
	for (auto const& item : course.items)
	{
		if (item.type == CourseItem::CrossingPoint)
		{
			crossing = true;
		}
		else if (item.type == CourseItem::Passage)
		{
			passage = true;
		}
		else if (item.type == CourseItem::MarkedRoute)
		{
			route = true;
			if (auto const* object = data.findObject(item.code))
				funnel = funnel || isFunnel(*object);
			auto const place = placeOf(map, data, item.code);
			if (!place.known)
			{
				chain_known = false;
				continue;
			}
			if (last_known)
				pending_mm += distance(last_exit, place.entry);
			else
				chain_known = false;
			pending_mm += place.path_mm;
			route_mm += place.path_mm;
			last_exit = place.exit;
			last_known = true;
			if (track)
				track->insert(track->end(), place.points.begin(), place.points.end());
		}
		else if (isControlItem(item.type))
		{
			Visit visit;
			visit.code = item.code;
			visit.type = item.type;
			visit.funnel = funnel;
			visit.crossing = crossing;
			visit.passage = passage;
			if (route)
				visit.taped_m = route_mm * metres_per_mm;
			auto const place = placeOf(map, data, item.code);
			if (!visits.empty() && chain_known && last_known && place.known)
				visit.leg_m = (pending_mm + distance(last_exit, place.entry)) * metres_per_mm;
			visits.push_back(visit);
			
			last_known = place.known;
			if (place.known)
			{
				last_exit = place.exit;
				if (track)
					track->push_back(place.entry);
			}
			chain_known = true;
			pending_mm = 0.0;
			route_mm = 0.0;
			route = false;
			funnel = false;
			crossing = false;
			passage = false;
		}
	}
	return visits;
}

QString number(double value, int decimals)
{
	return QString::number(value, 'f', decimals);
}

/// Writes a position of the map. y grows upwards in the file, as other
/// programs write and read it, against the wording of the schema; the Map element
/// of the file, whose top is above its bottom in numbers, says so.
void writeMapPosition(QXmlStreamWriter& xml, const QString& name, const QPointF& position)
{
	auto const up = position.y() == 0.0 ? 0.0 : -position.y();
	xml.writeEmptyElement(name);
	xml.writeAttribute(QStringLiteral("x"), number(position.x(), 2));
	xml.writeAttribute(QStringLiteral("y"), number(up, 2));
	xml.writeAttribute(QStringLiteral("unit"), QStringLiteral("mm"));
}


/// A course as it is written: a course without variations, or a variant of a relay.
struct OutCourse
{
	QString name;
	/// The relay of which it is a variant; empty for a course of its own.
	QString family;
	Course course;
};

/// The teams of a relay with the courses which they run.
struct OutRelay
{
	QString course;
	QString class_name;
	bool one_man = false;
	int first_number = 1;
	/// For every team the names of its courses: one for each leg, or the one of a one-man relay.
	std::vector<QStringList> teams;
};

struct OutData
{
	std::vector<OutCourse> courses;
	std::vector<OutRelay> relays;
	/// The courses whose variations have a fault.
	QStringList faulty;
};

/// The courses as the format has them: every variant of a relay a course named by the relay, an
/// underscore and the code; of a one-man relay one course for every combination of a runner,
/// the loops one behind the other and only the last with its finish.
OutData collectCourses(const CourseData& data, const QString& only_course = {})
{
	OutData out;
	for (auto const& course : data.courses)
	{
		if (!only_course.isEmpty() && course.name != only_course)
			continue;
		auto const structure = relayStructure(course);
		if (!structure.isValid())
		{
			out.faulty.push_back(course.name);
			continue;
		}
		if (course.type == Course::Normal)
		{
			out.courses.push_back({ course.name, {}, course });
			continue;
		}
		
		auto const variant_name = [&course](const RelayVariant& variant) {
			return QString(course.name + QLatin1Char('_') + relayVariantCode(variant).remove(QLatin1Char(' ')));
		};
		auto const teams = relayTeams(data, course.name);
		auto const allocation = allocateRelay(structure, teams.count);
		OutRelay relay;
		relay.course = course.name;
		relay.class_name = teams.class_name;
		relay.one_man = course.type == Course::OneManRelay;
		relay.first_number = teams.first_number;
		
		if (!relay.one_man)
		{
			for (auto const& variant : relayVariants(structure))
			{
				auto written = relayVariantCourse(course, structure, variant);
				written.name = variant_name(variant);
				out.courses.push_back({ written.name, course.name, written });
			}
			for (auto const& team : allocation.teams)
			{
				QStringList names;
				for (auto const& variant : team)
					names.push_back(variant_name(variant));
				relay.teams.push_back(names);
			}
		}
		else
		{
			QStringList known;
			for (auto const& team : allocation.teams)
			{
				OutCourse written;
				written.family = course.name;
				written.course = course;
				written.course.type = Course::Normal;
				written.course.legs = 0;
				written.course.items.clear();
				written.name = course.name + QLatin1Char('_');
				for (std::size_t loop = 0; loop < team.size(); ++loop)
				{
					written.name += relayVariantCode(team[loop]).remove(QLatin1Char(' '));
					for (auto const& item : relayVariantItems(course, structure, team[loop]))
					{
						if (item.type != CourseItem::Finish || loop + 1 == team.size())
							written.course.items.push_back(item);
					}
				}
				written.course.name = written.name;
				if (!known.contains(written.name))
				{
					known.push_back(written.name);
					out.courses.push_back(written);
				}
				relay.teams.push_back(QStringList{ written.name });
			}
		}
		out.relays.push_back(relay);
	}
	return out;
}


/// What a GPX or a KML file holds.
struct Waypoint
{
	QString code;
	QString kind;
	QString text;
	LatLon place;
};

struct Track
{
	QString name;
	std::vector<LatLon> points;
};

struct Geodata
{
	QString title;
	std::vector<Waypoint> waypoints;
	std::vector<Track> tracks;
};

/// Collects the waypoints and the tracks; the report says why there are none.
Geodata collectGeodata(const Map& map, const QString& only_course, CourseExportReport& report)
{
	Geodata geodata;
	auto const& data = map.courseData();
	auto const& georef = map.getGeoreferencing();
	geodata.title = data.options.eventTitle();
	
	if (!isGeoreferenced(map))
	{
		report.error = tr("The map is not georeferenced: it has no real-world coordinates or no coordinate reference system.");
		return geodata;
	}
	auto const* chosen = only_course.isEmpty() ? nullptr : data.findCourse(only_course);
	if (!only_course.isEmpty() && !chosen)
	{
		report.error = tr("There is no course %1.").arg(only_course);
		return geodata;
	}
	auto const out = collectCourses(data, only_course);
	if (chosen && out.courses.empty())
	{
		report.error = tr("The variations of the course %1 have a fault; it cannot be exported.").arg(only_course);
		return geodata;
	}
	report.skipped_courses = out.faulty;
	
	for (auto const& written : out.courses)
	{
		Track track;
		track.name = written.name;
		std::vector<MapCoordF> points;
		visitsOf(map, data, written.course, &points);
		for (auto const& point : points)
		{
			auto ok = false;
			auto const place = georef.toGeographicCoords(point, &ok);
			if (ok)
				track.points.push_back(place);
		}
		geodata.tracks.push_back(track);
	}
	
	for (auto const& object : data.objects)
	{
		if (!isControlKind(object.kind))
			continue;
		if (chosen && !coursesUsing(data, object.code).contains(chosen->name))
			continue;
		auto const place = placeOf(map, data, object.code);
		if (!place.known)
			continue;
		auto ok = false;
		Waypoint waypoint;
		waypoint.place = georef.toGeographicCoords(place.entry, &ok);
		if (!ok)
			continue;
		waypoint.code = object.code;
		waypoint.kind = typeName(object.kind);
		waypoint.text = courseObjectText(map, object);
		geodata.waypoints.push_back(waypoint);
	}
	if (geodata.waypoints.empty())
	{
		report.error = tr("There are no course objects to export.");
		return geodata;
	}
	
	report.ok = true;
	report.controls = int(geodata.waypoints.size());
	report.courses = int(geodata.tracks.size());
	return geodata;
}


/// What a file of the IOF holds, as far as it is read.
struct InControl
{
	QString id;
	QString type;
	QStringList punch_ids;
	bool has_position = false;
	double lng = 0.0;
	double lat = 0.0;
	bool has_map_position = false;
	double x = 0.0;
	double y = 0.0;
};

struct InCourseControl
{
	QString type;
	QString control;
	QString special;
};

struct InCourse
{
	QString name;
	QString family;
	bool has_length = false;
	double length = 0.0;
	bool has_climb = false;
	double climb = 0.0;
	std::vector<InCourseControl> controls;
};

struct InClass
{
	QString name;
	QString course;
	int competitors = -1;
};

struct InFile
{
	double scale = 0.0;
	/// The Map of the file has its top above its bottom in numbers.
	bool y_up = false;
	std::vector<InControl> controls;
	std::vector<InCourse> courses;
	std::vector<InClass> classes;
};

void readMap(QXmlStreamReader& xml, InFile& file)
{
	auto has_top = false;
	auto has_bottom = false;
	auto top = 0.0;
	auto bottom = 0.0;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Scale"))
		{
			file.scale = xml.readElementText().toDouble();
			continue;
		}
		if (xml.name() == QLatin1String("MapPositionTopLeft"))
			top = xml.attributes().value(QLatin1String("y")).toDouble(&has_top);
		else if (xml.name() == QLatin1String("MapPositionBottomRight"))
			bottom = xml.attributes().value(QLatin1String("y")).toDouble(&has_bottom);
		xml.skipCurrentElement();
	}
	file.y_up = has_top && has_bottom && top > bottom;
}

InControl readControl(QXmlStreamReader& xml)
{
	InControl control;
	control.type = xml.attributes().value(QLatin1String("type")).toString();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Id"))
		{
			control.id = xml.readElementText().trimmed();
			continue;
		}
		if (xml.name() == QLatin1String("PunchingUnitId"))
		{
			auto const id = xml.readElementText().trimmed();
			if (!id.isEmpty())
				control.punch_ids.push_back(id);
			continue;
		}
		auto const attributes = xml.attributes();
		if (xml.name() == QLatin1String("Position"))
		{
			auto lng_ok = false;
			auto lat_ok = false;
			control.lng = attributes.value(QLatin1String("lng")).toDouble(&lng_ok);
			control.lat = attributes.value(QLatin1String("lat")).toDouble(&lat_ok);
			control.has_position = lng_ok && lat_ok;
		}
		else if (xml.name() == QLatin1String("MapPosition"))
		{
			auto x_ok = false;
			auto y_ok = false;
			control.x = attributes.value(QLatin1String("x")).toDouble(&x_ok);
			control.y = attributes.value(QLatin1String("y")).toDouble(&y_ok);
			auto const unit = attributes.value(QLatin1String("unit"));
			control.has_map_position = x_ok && y_ok && (unit.isEmpty() || unit == QLatin1String("mm"));
		}
		xml.skipCurrentElement();
	}
	return control;
}

InCourse readCourse(QXmlStreamReader& xml)
{
	InCourse course;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Name"))
		{
			course.name = xml.readElementText().trimmed();
		}
		else if (xml.name() == QLatin1String("CourseFamily"))
		{
			course.family = xml.readElementText().trimmed();
		}
		else if (xml.name() == QLatin1String("Climb"))
		{
			course.climb = xml.readElementText().toDouble(&course.has_climb);
		}
		else if (xml.name() == QLatin1String("Length"))
		{
			course.length = xml.readElementText().toDouble(&course.has_length);
		}
		else if (xml.name() == QLatin1String("CourseControl"))
		{
			InCourseControl control;
			control.type = xml.attributes().value(QLatin1String("type")).toString();
			control.special = xml.attributes().value(QLatin1String("specialInstruction")).toString();
			auto first = true;
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("Control") && first)
				{
					control.control = xml.readElementText().trimmed();
					first = false;
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
			course.controls.push_back(control);
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	return course;
}

InClass readClass(QXmlStreamReader& xml)
{
	InClass course_class;
	auto ok = false;
	auto const competitors = xml.attributes().value(QLatin1String("numberOfCompetitors")).toInt(&ok);
	if (ok && competitors >= 0)
		course_class.competitors = competitors;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("ClassName"))
			course_class.name = xml.readElementText().trimmed();
		else if (xml.name() == QLatin1String("CourseName"))
			course_class.course = xml.readElementText().trimmed();
		else
			xml.skipCurrentElement();
	}
	return course_class;
}

/// Reads a file; returns why it is refused, or nothing.
QString readFile(QIODevice& device, InFile& file)
{
	QXmlStreamReader xml(&device);
	if (!xml.readNextStartElement())
		return tr("The file is not an XML file.");
	if (xml.name() != QLatin1String("CourseData") || xml.namespaceUri() != iof_namespace)
		return tr("The file is not course data of the IOF Data Standard 3.0.");
	
	auto race_read = false;
	while (xml.readNextStartElement())
	{
		if (xml.name() != QLatin1String("RaceCourseData") || race_read)
		{
			xml.skipCurrentElement();
			continue;
		}
		race_read = true;
		while (xml.readNextStartElement())
		{
			if (xml.name() == QLatin1String("Map"))
				readMap(xml, file);
			else if (xml.name() == QLatin1String("Control"))
				file.controls.push_back(readControl(xml));
			else if (xml.name() == QLatin1String("Course"))
				file.courses.push_back(readCourse(xml));
			else if (xml.name() == QLatin1String("ClassCourseAssignment"))
				file.classes.push_back(readClass(xml));
			else
				xml.skipCurrentElement();
		}
	}
	if (xml.hasError())
		return tr("The file cannot be read: %1 (line %2).").arg(xml.errorString()).arg(xml.lineNumber());
	return {};
}

/// The kind of a control of a file; false for a type which is no course object.
bool kindOf(const InFile& file, const InControl& control, CourseObject::Kind& kind)
{
	auto type = control.type;
	if (type.isEmpty() || type == QLatin1String("Control"))
	{
		// The courses may say what the control does not.
		for (auto const& course : file.courses)
		{
			for (auto const& course_control : course.controls)
			{
				if (course_control.control == control.id
				    && (course_control.type == QLatin1String("Start") || course_control.type == QLatin1String("Finish")))
					type = course_control.type;
			}
		}
	}
	if (type.isEmpty() || type == QLatin1String("Control"))
		kind = CourseObject::Control;
	else if (type == QLatin1String("Start"))
		kind = CourseObject::Start;
	else if (type == QLatin1String("Finish"))
		kind = CourseObject::Finish;
	else
		return false;
	return true;
}

const Symbol* symbolFor(const Map& map, CourseObject::Kind kind)
{
	auto const role = kind == CourseObject::Start ? Symbol::CourseStart
	                  : kind == CourseObject::Finish ? Symbol::CourseFinish : Symbol::CourseControl;
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Point && symbol->courseRole() == role && !symbol->isHidden())
			return symbol;
	}
	return nullptr;
}

/// The part into which course objects are put: not that of the overrides.
int partForObjects(const Map& map)
{
	auto part_index = int(map.getCurrentPartIndex());
	if (map.getPart(std::size_t(part_index))->role() == MapPart::Role::Course)
	{
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			if (map.getPart(std::size_t(p))->role() != MapPart::Role::Course)
				return p;
		}
	}
	return part_index;
}

void addOnce(QStringList& list, const QString& entry)
{
	if (!list.contains(entry))
		list.push_back(entry);
}

QString fileNamePart(QString name)
{
	for (auto const c : QStringLiteral("\\/:*?\"<>|"))
		name.replace(c, QLatin1Char('_'));
	return name.trimmed();
}

/// Writes the folders of the course objects and of the courses, for KML and KMZ.
void writeKmlFolders(QXmlStreamWriter& xml, const Geodata& geodata)
{
	auto const tuple = [](const LatLon& place) {
		return QString(number(place.longitude(), 7) + QLatin1Char(',') + number(place.latitude(), 7) + QLatin1String(",0"));
	};

	xml.writeStartElement(QStringLiteral("Folder"));
	xml.writeTextElement(QStringLiteral("name"), tr("Course objects"));
	for (auto const& waypoint : geodata.waypoints)
	{
		xml.writeStartElement(QStringLiteral("Placemark"));
		xml.writeTextElement(QStringLiteral("name"), waypoint.code);
		if (!waypoint.text.isEmpty())
			xml.writeTextElement(QStringLiteral("description"), waypoint.text);
		xml.writeStartElement(QStringLiteral("Point"));
		xml.writeTextElement(QStringLiteral("coordinates"), tuple(waypoint.place));
		xml.writeEndElement();
		xml.writeEndElement();
	}
	xml.writeEndElement();

	xml.writeStartElement(QStringLiteral("Folder"));
	xml.writeTextElement(QStringLiteral("name"), tr("Courses"));
	for (auto const& track : geodata.tracks)
	{
		QStringList tuples;
		for (auto const& point : track.points)
			tuples.push_back(tuple(point));
		xml.writeStartElement(QStringLiteral("Placemark"));
		xml.writeTextElement(QStringLiteral("name"), track.name);
		xml.writeStartElement(QStringLiteral("LineString"));
		xml.writeTextElement(QStringLiteral("tessellate"), QStringLiteral("1"));
		xml.writeTextElement(QStringLiteral("coordinates"), tuples.join(QLatin1Char(' ')));
		xml.writeEndElement();
		xml.writeEndElement();
	}
	xml.writeEndElement();
}


/// Puts the objects which were made and the course data into the map, as one undo step.
void commitImport(Map& map, CourseData data, const QByteArray& before, const std::vector<int>& object_indices, int part_index)
{
	if (object_indices.empty())
	{
		changeCourseData(map, std::move(data));
		return;
	}
	auto* object_step = new DeleteObjectsUndoStep(&map);
	object_step->setPartIndex(part_index);
	for (auto const index : object_indices)
		object_step->addObject(index);
	auto* combined = new CombinedUndoStep(&map);
	combined->push(object_step);
	combined->push(new MapDataUndoStep(&map, MapDataUndoStep::Courses, before));
	map.setCourseData(std::move(data));
	map.setObjectsDirty();
	map.setOtherDirty();
	map.push(combined);
}


/// What a GPX or a KML file holds, as far as it is read.
struct InWaypoint
{
	QString name;
	QString type;
	LatLon place;
};

struct InTrack
{
	QString name;
	std::vector<LatLon> points;
};

struct InGeodata
{
	std::vector<InWaypoint> waypoints;
	std::vector<InTrack> tracks;
};

bool readLatLon(const QXmlStreamAttributes& attributes, LatLon& place)
{
	auto lat_ok = false;
	auto lon_ok = false;
	auto const lat = attributes.value(QLatin1String("lat")).toDouble(&lat_ok);
	auto const lon = attributes.value(QLatin1String("lon")).toDouble(&lon_ok);
	if (!lat_ok || !lon_ok)
		return false;
	place = LatLon(lat, lon);
	return true;
}

/// Reads a point of a track or a route and what it holds.
void readGpxPoint(QXmlStreamReader& xml, std::vector<LatLon>& points)
{
	LatLon place;
	if (readLatLon(xml.attributes(), place))
		points.push_back(place);
	xml.skipCurrentElement();
}

QString readGpx(QIODevice& device, InGeodata& file)
{
	QXmlStreamReader xml(&device);
	if (!xml.readNextStartElement())
		return tr("The file is not an XML file.");
	if (xml.name() != QLatin1String("gpx") || !xml.namespaceUri().startsWith(QLatin1String("http://www.topografix.com/GPX/1/")))
		return tr("The file is not a GPX file.");
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("wpt"))
		{
			InWaypoint waypoint;
			auto const has_place = readLatLon(xml.attributes(), waypoint.place);
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("name"))
					waypoint.name = xml.readElementText().trimmed();
				else if (xml.name() == QLatin1String("type"))
					waypoint.type = xml.readElementText().trimmed();
				else
					xml.skipCurrentElement();
			}
			if (has_place)
				file.waypoints.push_back(waypoint);
		}
		else if (xml.name() == QLatin1String("trk") || xml.name() == QLatin1String("rte"))
		{
			auto const route = xml.name() == QLatin1String("rte");
			InTrack track;
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("name"))
				{
					track.name = xml.readElementText().trimmed();
				}
				else if (route && xml.name() == QLatin1String("rtept"))
				{
					readGpxPoint(xml, track.points);
				}
				else if (!route && xml.name() == QLatin1String("trkseg"))
				{
					// The segments one behind the other.
					while (xml.readNextStartElement())
					{
						if (xml.name() == QLatin1String("trkpt"))
							readGpxPoint(xml, track.points);
						else
							xml.skipCurrentElement();
					}
				}
				else
				{
					xml.skipCurrentElement();
				}
			}
			file.tracks.push_back(track);
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	if (xml.hasError())
		return tr("The file cannot be read: %1 (line %2).").arg(xml.errorString()).arg(xml.lineNumber());
	return {};
}

/// The places of the coordinates of KML: tuples of longitude, latitude and height.
std::vector<LatLon> kmlCoordinates(const QString& text)
{
	std::vector<LatLon> places;
	for (auto const& tuple : text.simplified().split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		auto const values = tuple.split(QLatin1Char(','));
		if (values.size() < 2)
			continue;
		auto lon_ok = false;
		auto lat_ok = false;
		auto const lon = values[0].toDouble(&lon_ok);
		auto const lat = values[1].toDouble(&lat_ok);
		if (lon_ok && lat_ok)
			places.push_back(LatLon(lat, lon));
	}
	return places;
}

/// Reads a geometry of a placemark: the first point and the first line string, also in a MultiGeometry.
void readKmlGeometry(QXmlStreamReader& xml, std::vector<LatLon>& point, std::vector<LatLon>& line)
{
	auto const name = xml.name().toString();
	if (name == QLatin1String("MultiGeometry"))
	{
		while (xml.readNextStartElement())
			readKmlGeometry(xml, point, line);
		return;
	}
	if (name != QLatin1String("Point") && name != QLatin1String("LineString"))
	{
		xml.skipCurrentElement();
		return;
	}
	std::vector<LatLon> places;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("coordinates"))
			places = kmlCoordinates(xml.readElementText());
		else
			xml.skipCurrentElement();
	}
	auto& target = name == QLatin1String("Point") ? point : line;
	if (target.empty())
		target = places;
}

/// The code of an ExtendedData as a KMZ of courses has it: `cs:control-code`.
QString readKmlControlCode(QXmlStreamReader& xml)
{
	QString code;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Data") && xml.attributes().value(QLatin1String("name")) == QLatin1String("cs:control-code"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("value"))
					code = xml.readElementText().trimmed();
				else
					xml.skipCurrentElement();
			}
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	return code;
}

/**
 * Reads the placemarks of a Document or a Folder and of those in it. A
 * folder Coordinates whose points have the code of the ExtendedData is,
 * as in a KMZ of courses, the course of the folder around it: a track
 * through its points.
 */
void readKmlContainer(QXmlStreamReader& xml, InGeodata& file, const QString& outer_name = {})
{
	QString container_name;
	InTrack coded;
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("Document") || xml.name() == QLatin1String("Folder"))
		{
			readKmlContainer(xml, file, container_name);
		}
		else if (xml.name() == QLatin1String("name"))
		{
			container_name = xml.readElementText().trimmed();
		}
		else if (xml.name() == QLatin1String("Placemark"))
		{
			QString name;
			QString code;
			std::vector<LatLon> point;
			std::vector<LatLon> line;
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("name"))
					name = xml.readElementText().trimmed();
				else if (xml.name() == QLatin1String("ExtendedData"))
					code = readKmlControlCode(xml);
				else
					readKmlGeometry(xml, point, line);
			}
			if (!point.empty())
			{
				file.waypoints.push_back({ code.isEmpty() ? name : code, {}, point.front() });
				if (!code.isEmpty())
					coded.points.push_back(point.front());
			}
			if (!line.empty())
				file.tracks.push_back({ name, line });
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	if (container_name == QLatin1String("Coordinates") && !outer_name.isEmpty() && !coded.points.empty())
	{
		coded.name = outer_name;
		file.tracks.push_back(std::move(coded));
	}
}

QString readKml(QIODevice& device, InGeodata& file)
{
	QXmlStreamReader xml(&device);
	if (!xml.readNextStartElement())
		return tr("The file is not an XML file.");
	auto const name_space = xml.namespaceUri();
	if (xml.name() != QLatin1String("kml")
	    || !(name_space.startsWith(QLatin1String("http://www.opengis.net/kml/2")) || name_space.startsWith(QLatin1String("http://earth.google.com/kml/2"))))
		return tr("The file is not a KML file.");
	readKmlContainer(xml, file);
	if (xml.hasError())
		return tr("The file cannot be read: %1 (line %2).").arg(xml.errorString()).arg(xml.lineNumber());
	return {};
}

/// The kind of a waypoint: by its type as FOOM writes it, else by its name.
CourseObject::Kind kindOf(const InWaypoint& waypoint)
{
	if (waypoint.type == QLatin1String("Start"))
		return CourseObject::Start;
	if (waypoint.type == QLatin1String("Finish"))
		return CourseObject::Finish;
	if (waypoint.type == QLatin1String("Control"))
		return CourseObject::Control;
	auto const name = waypoint.name;
	auto const digits_after_letter = name.size() > 1 && std::all_of(name.begin() + 1, name.end(), [](QChar c) { return c.isDigit(); });
	if (digits_after_letter && name.startsWith(QLatin1Char('S')))
		return CourseObject::Start;
	if (digits_after_letter && name.startsWith(QLatin1Char('F')))
		return CourseObject::Finish;
	return CourseObject::Control;
}

/// True for the name of a variant of a relay of the map: the relay, an underscore and more.
bool isRelayVariantName(const CourseData& data, const QString& name)
{
	return std::any_of(begin(data.courses), end(data.courses), [&name](const Course& course) {
		return course.type != Course::Normal && name.size() > course.name.size() + 1
		       && name.startsWith(course.name) && name.at(course.name.size()) == QLatin1Char('_');
	});
}

/// Takes the waypoints and the tracks of a file into the map.
CourseGeodataImportReport importGeodata(Map& map, const InGeodata& file, const CourseGeodataImportOptions& options)
{
	CourseGeodataImportReport report;
	auto data = map.courseData();
	auto const before = MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses);
	auto const& georef = map.getGeoreferencing();
	auto const part_index = partForObjects(map);

	// The course objects.
	std::vector<int> object_indices;
	if (options.objects)
	{
		QStringList read;  // A code of more than one waypoint, as in such files, counts once.
		for (auto const& waypoint : file.waypoints)
		{
			if (waypoint.name.isEmpty() || read.contains(waypoint.name))
				continue;
			read.push_back(waypoint.name);
			if (data.findObject(waypoint.name))
			{
				++report.objects_found;
				continue;
			}
			auto const kind = kindOf(waypoint);
			auto const* symbol = symbolFor(map, kind);
			auto has_place = false;
			auto const place = georef.toMapCoordF(waypoint.place, &has_place);
			if (!has_place || !symbol || !isValidCourseCode(waypoint.name))
			{
				addOnce(report.objects_not_made, waypoint.name);
				continue;
			}
			auto* object = new PointObject(symbol);
			object->setPosition(place);
			object_indices.push_back(map.addObject(object, part_index));
			CourseObject course_object;
			course_object.object = object->uuid();
			course_object.kind = kind;
			course_object.code = waypoint.name;
			data.objects.push_back(course_object);
			++report.objects_added;
		}
	}

	// The courses: the tracks which start and end at a start, a control or a finish.
	if (options.courses)
	{
		struct Stop { QString code; MapCoordF place; };
		std::vector<Stop> stops;
		for (auto const& object : data.objects)
		{
			if (!isControlKind(object.kind))
				continue;
			auto const place = placeOf(map, data, object.code);
			if (place.known)
				stops.push_back({ object.code, place.entry });
		}
		// 1 m on the ground.
		auto const tolerance_mm = 1000.0 / map.getScaleDenominator();
		auto const stopAt = [&stops, tolerance_mm](const MapCoordF& point) {
			const Stop* nearest = nullptr;
			auto nearest_mm = tolerance_mm;
			for (auto const& stop : stops)
			{
				auto const d = distance(point, stop.place);
				if (d <= nearest_mm)
				{
					nearest = &stop;
					nearest_mm = d;
				}
			}
			return nearest;
		};

		for (std::size_t t = 0; t < file.tracks.size(); ++t)
		{
			auto const& track = file.tracks[t];
			auto const name = track.name.isEmpty() ? tr("Track %1").arg(t + 1) : track.name;
			if (isRelayVariantName(map.courseData(), name))
			{
				++report.relay_variants;
				continue;
			}
			if (data.findCourse(name))
			{
				addOnce(report.courses_existing, name);
				continue;
			}
			std::vector<const Stop*> at;
			for (auto const& point : track.points)
			{
				auto ok = false;
				auto const place = georef.toMapCoordF(point, &ok);
				at.push_back(ok ? stopAt(place) : nullptr);
			}
			if (at.empty() || !at.front() || !at.back())
			{
				addOnce(report.tracks_not_courses, name);
				continue;
			}
			Course course;
			course.name = name;
			const Stop* last = nullptr;
			for (auto const* stop : at)
			{
				if (!stop || stop == last)
					continue;
				last = stop;
				CourseItem item;
				auto const* object = data.findObject(stop->code);
				if (!object || !canBeCourseItem(object->kind, &item.type))
					continue;
				item.code = stop->code;
				course.items.push_back(item);
			}
			data.courses.push_back(course);
			++report.courses_added;
		}
	}

	commitImport(map, std::move(data), before, object_indices, part_index);
	report.ok = true;
	return report;
}

CourseGeodataImportReport refusedImport(const QString& error)
{
	CourseGeodataImportReport report;
	report.error = error;
	return report;
}

QString notGeoreferenced()
{
	return tr("The map is not georeferenced: it has no real-world coordinates or no coordinate reference system.");
}


}  // namespace



CourseExportReport writeIofCourseData(const Map& map, QIODevice& device)
{
	CourseExportReport report;
	auto const& data = map.courseData();
	auto const& georef = map.getGeoreferencing();
	auto const georeferenced = isGeoreferenced(map);
	
	QXmlStreamWriter xml(&device);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeDefaultNamespace(iof_namespace);
	xml.writeStartElement(QStringLiteral("CourseData"));
	xml.writeAttribute(QStringLiteral("iofVersion"), QStringLiteral("3.0"));
	xml.writeAttribute(QStringLiteral("createTime"), QDateTime::currentDateTime().toString(Qt::ISODate));
	xml.writeAttribute(QStringLiteral("creator"), creator());
	
	xml.writeStartElement(QStringLiteral("Event"));
	xml.writeTextElement(QStringLiteral("Name"), data.options.eventTitle());
	xml.writeEndElement();
	
	xml.writeStartElement(QStringLiteral("RaceCourseData"));
	
	// The map: its scale and the extent of its objects. The places of the
	// course objects are in it whatever their symbols draw, and it has a
	// height, so that a reader sees which way y runs.
	auto extent = map.calculateExtent();
	for (auto const& object : data.objects)
	{
		for (auto const& point : placeOf(map, data, object.code).points)
		{
			if (extent.isValid())
				rectInclude(extent, point);
			else
				extent = QRectF(point, QSizeF(0.0001, 0.0001));
		}
	}
	if (!extent.isValid())
		extent = QRectF(0.0, 0.0, 0.0001, 0.0001);
	if (extent.height() < 1.0 || extent.width() < 1.0)
		extent.adjust(-1.0, -1.0, 1.0, 1.0);
	xml.writeStartElement(QStringLiteral("Map"));
	xml.writeTextElement(QStringLiteral("Scale"), QString::number(map.getScaleDenominator()));
	writeMapPosition(xml, QStringLiteral("MapPositionTopLeft"), extent.topLeft());
	writeMapPosition(xml, QStringLiteral("MapPositionBottomRight"), extent.bottomRight());
	xml.writeEndElement();
	
	for (auto const& object : data.objects)
	{
		if (!isControlKind(object.kind))
			continue;
		xml.writeStartElement(QStringLiteral("Control"));
		xml.writeAttribute(QStringLiteral("type"), typeName(object.kind));
		xml.writeTextElement(QStringLiteral("Id"), object.code);
		for (auto const& id : object.punch_ids)
			xml.writeTextElement(QStringLiteral("PunchingUnitId"), id);
		auto const place = placeOf(map, data, object.code);
		if (place.known)
		{
			auto ok = false;
			auto const geographic = georeferenced ? georef.toGeographicCoords(place.entry, &ok) : LatLon{};
			if (ok)
			{
				xml.writeEmptyElement(QStringLiteral("Position"));
				xml.writeAttribute(QStringLiteral("lng"), number(geographic.longitude(), 7));
				xml.writeAttribute(QStringLiteral("lat"), number(geographic.latitude(), 7));
			}
			writeMapPosition(xml, QStringLiteral("MapPosition"), place.entry);
		}
		xml.writeEndElement();
		++report.controls;
	}
	
	auto const out = collectCourses(data);
	QStringList written;
	QStringList left_out;
	for (auto const& entry : out.courses)
	{
		auto const& course = entry.course;
		auto const visits = visitsOf(map, data, course);
		if (visits.size() < 2)
		{
			left_out.push_back(entry.name);
			continue;
		}
		
		xml.writeStartElement(QStringLiteral("Course"));
		xml.writeTextElement(QStringLiteral("Name"), entry.name);
		if (!entry.family.isEmpty())
			xml.writeTextElement(QStringLiteral("CourseFamily"), entry.family);
		auto const length = measureCourse(map, data, course);
		if (length.known)
			xml.writeTextElement(QStringLiteral("Length"), QString::number(qRound(length.total_m)));
		if (course.climb_m != 0)
			xml.writeTextElement(QStringLiteral("Climb"), QString::number(course.climb_m));
		for (auto const& visit : visits)
		{
			xml.writeStartElement(QStringLiteral("CourseControl"));
			xml.writeAttribute(QStringLiteral("type"), typeName(visit.type));
			if (visit.taped_m >= 0.0)
			{
				xml.writeAttribute(QStringLiteral("specialInstruction"), visit.funnel ? QStringLiteral("FunnelTapedRoute") : QStringLiteral("TapedRoute"));
				xml.writeAttribute(QStringLiteral("tapedRouteLength"), QString::number(qRound(visit.taped_m)));
			}
			else if (visit.crossing)
			{
				xml.writeAttribute(QStringLiteral("specialInstruction"), QStringLiteral("MandatoryCrossingPoint"));
			}
			else if (visit.passage)
			{
				xml.writeAttribute(QStringLiteral("specialInstruction"), QStringLiteral("MandatoryOutOfBoundsAreaPassage"));
			}
			xml.writeTextElement(QStringLiteral("Control"), visit.code);
			if (visit.leg_m >= 0.0)
				xml.writeTextElement(QStringLiteral("LegLength"), QString::number(qRound(visit.leg_m)));
			xml.writeEndElement();
		}
		xml.writeEndElement();
		written.push_back(entry.name);
		++report.courses;
	}
	// In the order of the courses of the map: what was left out for its length, and what for a fault.
	for (auto const& course : data.courses)
	{
		if (left_out.contains(course.name) || out.faulty.contains(course.name))
			report.skipped_courses.push_back(course.name);
	}
	for (auto const& name : qAsConst(left_out))
	{
		if (!report.skipped_courses.contains(name))
			report.skipped_courses.push_back(name);
	}
	
	for (auto const& course_class : data.classes)
	{
		if (!written.contains(course_class.course))
			continue;
		xml.writeStartElement(QStringLiteral("ClassCourseAssignment"));
		if (course_class.runners >= 0)
			xml.writeAttribute(QStringLiteral("numberOfCompetitors"), QString::number(course_class.runners));
		xml.writeTextElement(QStringLiteral("ClassName"), course_class.name);
		xml.writeTextElement(QStringLiteral("CourseName"), course_class.course);
		xml.writeEndElement();
		++report.classes;
	}
	
	// The runners of the one-man relays, then the teams of the relays, as the schema orders them.
	for (auto const& relay : out.relays)
	{
		if (!relay.one_man)
			continue;
		for (std::size_t t = 0; t < relay.teams.size(); ++t)
		{
			if (!written.contains(relay.teams[t].front()))
				continue;
			xml.writeStartElement(QStringLiteral("PersonCourseAssignment"));
			xml.writeTextElement(QStringLiteral("BibNumber"), QString::number(relay.first_number + int(t)));
			xml.writeTextElement(QStringLiteral("CourseName"), relay.teams[t].front());
			xml.writeTextElement(QStringLiteral("CourseFamily"), relay.course);
			xml.writeEndElement();
			++report.teams;
		}
	}
	for (auto const& relay : out.relays)
	{
		if (relay.one_man)
			continue;
		for (std::size_t t = 0; t < relay.teams.size(); ++t)
		{
			xml.writeStartElement(QStringLiteral("TeamCourseAssignment"));
			xml.writeTextElement(QStringLiteral("BibNumber"), QString::number(relay.first_number + int(t)));
			if (!relay.class_name.isEmpty())
				xml.writeTextElement(QStringLiteral("ClassName"), relay.class_name);
			for (int leg = 0; leg < relay.teams[t].size(); ++leg)
			{
				xml.writeStartElement(QStringLiteral("TeamMemberCourseAssignment"));
				xml.writeTextElement(QStringLiteral("Leg"), QString::number(leg + 1));
				xml.writeTextElement(QStringLiteral("CourseName"), relay.teams[t][leg]);
				xml.writeTextElement(QStringLiteral("CourseFamily"), relay.course);
				xml.writeEndElement();
			}
			xml.writeEndElement();
			++report.teams;
		}
	}
	
	xml.writeEndElement();  // RaceCourseData
	xml.writeEndElement();  // CourseData
	xml.writeEndDocument();
	
	report.ok = !xml.hasError();
	if (!report.ok)
		report.error = tr("The file could not be written.");
	return report;
}



CourseExportReport writeCoursesGpx(const Map& map, QIODevice& device, const QString& course)
{
	CourseExportReport report;
	auto const geodata = collectGeodata(map, course, report);
	if (!report.ok)
		return report;
	
	QXmlStreamWriter xml(&device);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
	xml.writeStartElement(QStringLiteral("gpx"));
	xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
	xml.writeAttribute(QStringLiteral("creator"), creator());
	if (!geodata.title.isEmpty())
	{
		xml.writeStartElement(QStringLiteral("metadata"));
		xml.writeTextElement(QStringLiteral("name"), geodata.title);
		xml.writeEndElement();
	}
	for (auto const& waypoint : geodata.waypoints)
	{
		xml.writeStartElement(QStringLiteral("wpt"));
		xml.writeAttribute(QStringLiteral("lat"), number(waypoint.place.latitude(), 7));
		xml.writeAttribute(QStringLiteral("lon"), number(waypoint.place.longitude(), 7));
		xml.writeTextElement(QStringLiteral("name"), waypoint.code);
		if (!waypoint.text.isEmpty())
			xml.writeTextElement(QStringLiteral("desc"), waypoint.text);
		xml.writeTextElement(QStringLiteral("type"), waypoint.kind);
		xml.writeEndElement();
	}
	for (auto const& track : geodata.tracks)
	{
		xml.writeStartElement(QStringLiteral("trk"));
		xml.writeTextElement(QStringLiteral("name"), track.name);
		xml.writeStartElement(QStringLiteral("trkseg"));
		for (auto const& point : track.points)
		{
			xml.writeEmptyElement(QStringLiteral("trkpt"));
			xml.writeAttribute(QStringLiteral("lat"), number(point.latitude(), 7));
			xml.writeAttribute(QStringLiteral("lon"), number(point.longitude(), 7));
		}
		xml.writeEndElement();
		xml.writeEndElement();
	}
	xml.writeEndElement();
	xml.writeEndDocument();
	
	if (xml.hasError())
	{
		report.ok = false;
		report.error = tr("The file could not be written.");
	}
	return report;
}


CourseExportReport writeCoursesKml(const Map& map, QIODevice& device, const QString& course)
{
	CourseExportReport report;
	auto const geodata = collectGeodata(map, course, report);
	if (!report.ok)
		return report;

	QXmlStreamWriter xml(&device);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeDefaultNamespace(QStringLiteral("http://www.opengis.net/kml/2.2"));
	xml.writeStartElement(QStringLiteral("kml"));
	xml.writeComment(QLatin1String("Generator: ") + creator());
	xml.writeStartElement(QStringLiteral("Document"));
	xml.writeTextElement(QStringLiteral("name"), geodata.title);
	writeKmlFolders(xml, geodata);
	xml.writeEndElement();  // Document
	xml.writeEndElement();  // kml
	xml.writeEndDocument();

	if (xml.hasError())
	{
		report.ok = false;
		report.error = tr("The file could not be written.");
	}
	return report;
}



CourseFilesReport writeCourseFiles(Map& map, const QString& path, CourseFileKind kind, bool per_course, const CourseKmzOptions& kmz)
{
	CourseFilesReport report;
	auto const write_one = [&map, kind, &kmz, &report](const QString& file_path, const QString& course) {
		if (kind == CourseFileKind::Kmz)  // the archive is written whole by the writer
		{
			auto options = kmz;
			options.course = course;
			auto const written = writeCoursesKmz(map, file_path, options);
			if (!written.ok)
			{
				report.error = written.error;
				return false;
			}
			report.files.push_back(file_path);
			for (auto const& skipped : written.skipped_courses)
				addOnce(report.skipped_courses, skipped);
			return true;
		}
		QSaveFile file(file_path);
		if (!file.open(QIODevice::WriteOnly))
		{
			report.error = tr("The file %1 cannot be written.").arg(file_path);
			return false;
		}
		CourseExportReport written;
		switch (kind)
		{
		case CourseFileKind::IofXml:  written = writeIofCourseData(map, file); break;
		case CourseFileKind::Gpx:     written = writeCoursesGpx(map, file, course); break;
		case CourseFileKind::Kml:     written = writeCoursesKml(map, file, course); break;
		case CourseFileKind::Kmz:     break;
		}
		if (!written.ok)
		{
			file.cancelWriting();
			report.error = written.error;
			return false;
		}
		if (!file.commit())
		{
			report.error = tr("The file %1 cannot be written.").arg(file_path);
			return false;
		}
		report.files.push_back(file_path);
		for (auto const& skipped : written.skipped_courses)
			addOnce(report.skipped_courses, skipped);
		return true;
	};
	
	if (!per_course || kind == CourseFileKind::IofXml)
	{
		report.ok = write_one(path, {});
		return report;
	}
	
	auto const info = QFileInfo(path);
	for (auto const& course : map.courseData().courses)
	{
		if (!relayStructure(course).isValid())
		{
			report.skipped_courses.push_back(course.name);
			continue;
		}
		auto const name = QString(info.completeBaseName() + QLatin1Char('.') + fileNamePart(course.name) + QLatin1Char('.') + info.suffix());
		if (!write_one(info.dir().filePath(name), course.name))
			return report;
	}
	if (report.files.isEmpty())
	{
		report.error = tr("There is no course to export.");
		return report;
	}
	report.ok = true;
	return report;
}



IofImportReport importIofCourseData(Map& map, QIODevice& device, const IofImportOptions& options)
{
	IofImportReport report;
	InFile file;
	report.error = readFile(device, file);
	if (!report.error.isEmpty())
		return report;
	
	auto data = map.courseData();
	auto const before = MapDataUndoStep::currentXml(map, MapDataUndoStep::Courses);
	auto const& georef = map.getGeoreferencing();
	auto const georeferenced = isGeoreferenced(map);
	auto const scale_factor = file.scale > 0.0 ? file.scale / double(map.getScaleDenominator()) : 1.0;
	auto const part_index = partForObjects(map);
	
	// The course objects.
	std::vector<int> object_indices;
	if (options.objects)
	{
		for (auto const& control : file.controls)
		{
			if (control.id.isEmpty())
				continue;
			if (data.findObject(control.id))
			{
				++report.objects_found;
				continue;
			}
			auto kind = CourseObject::Control;
			auto has_place = false;
			MapCoordF place;
			if (control.has_position && georeferenced)
				place = georef.toMapCoordF(LatLon(control.lat, control.lng), &has_place);
			if (!has_place && control.has_map_position)
			{
				place = MapCoordF(control.x * scale_factor, (file.y_up ? -control.y : control.y) * scale_factor);
				has_place = true;
			}
			auto const* symbol = kindOf(file, control, kind) ? symbolFor(map, kind) : nullptr;
			if (!has_place || !symbol || !isValidCourseCode(control.id))
			{
				addOnce(report.objects_not_made, control.id);
				continue;
			}
			
			auto* object = new PointObject(symbol);
			object->setPosition(place);
			object_indices.push_back(map.addObject(object, part_index));
			CourseObject course_object;
			course_object.object = object->uuid();
			course_object.kind = kind;
			course_object.code = control.id;
			course_object.punch_ids = control.punch_ids;
			data.objects.push_back(course_object);
			++report.objects_added;
		}
	}
	
	// The courses.
	if (options.courses)
	{
		for (auto const& in_course : file.courses)
		{
			if (in_course.name.isEmpty())
				continue;
			// A variant of a course which the map has is not a course beside it.
			if (!in_course.family.isEmpty() && in_course.family != in_course.name && map.courseData().findCourse(in_course.family))
			{
				++report.relay_variants;
				continue;
			}
			if (data.findCourse(in_course.name))
			{
				report.courses_existing.push_back(in_course.name);
				continue;
			}
			Course course;
			course.name = in_course.name;
			if (in_course.has_climb)
			{
				course.climb_m = qRound(in_course.climb);
				course.climb_manual = true;
			}
			for (auto const& control : in_course.controls)
			{
				if (!control.type.isEmpty() && control.type != QLatin1String("Start")
				    && control.type != QLatin1String("Control") && control.type != QLatin1String("Finish"))
					continue;
				auto const* object = data.findObject(control.control);
				CourseItem item;
				if (!object || !isControlKind(object->kind) || !canBeCourseItem(object->kind, &item.type))
				{
					if (!control.control.isEmpty())
						addOnce(report.codes_missing, control.control);
					continue;
				}
				if (control.special == QLatin1String("TapedRoute") || control.special == QLatin1String("FunnelTapedRoute"))
				{
					++report.taped_routes;
				}
				else if (control.special == QLatin1String("MandatoryCrossingPoint"))
				{
					CourseItem crossing;
					crossing.type = CourseItem::CrossingPoint;
					course.items.push_back(crossing);
				}
				else if (control.special == QLatin1String("MandatoryOutOfBoundsAreaPassage"))
				{
					CourseItem passage;
					passage.type = CourseItem::Passage;
					course.items.push_back(passage);
				}
				item.code = control.control;
				course.items.push_back(item);
			}
			// What the length of the file has more than the course measures is its extra length, in steps of 10 m.
			if (in_course.has_length)
			{
				auto const measured = measureCourse(map, data, course);
				auto const extra = measured.known ? qRound((in_course.length - measured.measured_m) / 10.0) : 0;
				if (extra != 0)
				{
					course.extra_km = extra / 100.0;
					course.extra_set = true;
				}
			}
			data.courses.push_back(course);
			++report.courses_added;
		}
	}
	
	// The classes.
	if (options.classes)
	{
		for (auto const& in_class : file.classes)
		{
			if (in_class.name.isEmpty())
				continue;
			auto const known = std::any_of(begin(data.classes), end(data.classes), [&in_class](const CourseClass& c) { return c.name == in_class.name; });
			if (known || !data.findCourse(in_class.course))
			{
				report.classes_skipped.push_back(in_class.name);
				continue;
			}
			CourseClass course_class;
			course_class.name = in_class.name;
			course_class.course = in_class.course;
			course_class.runners = in_class.competitors;
			data.classes.push_back(course_class);
			++report.classes_added;
		}
	}
	
	// One undo step: the objects first and the course data behind them.
	commitImport(map, std::move(data), before, object_indices, part_index);
	
	report.ok = true;
	return report;
}



// foom: the design notes, "Interchange: GPX and KML in, and a KMZ with the map".

CourseExportReport writeCoursesKmz(Map& map, const QString& path, const CourseKmzOptions& options)
{
	CourseExportReport report;
	auto const geodata = collectGeodata(map, options.course, report);
	if (!report.ok)
		return report;
	report.ok = false;

#if defined(MAPPER_USE_GDAL) && defined(QT_PRINTSUPPORT_LIB)
	// The placemarks and the lines, beside the folder of the pictures.
	QBuffer folders;
	folders.open(QIODevice::WriteOnly);
	{
		QXmlStreamWriter xml(&folders);
		xml.setAutoFormatting(true);
		writeKmlFolders(xml, geodata);
	}

	// The picture: of the map at its print area, or at what it draws and where its course objects lie.
	auto const& data = map.courseData();
	auto const has_config = map.hasPrinterConfig();
	QRectF area;
	if (!has_config)
	{
		area = map.calculateExtent(false, options.templates, options.view);
		for (auto const& object : data.objects)
		{
			for (auto const& point : placeOf(map, data, object.code).points)
			{
				if (area.isValid())
					rectInclude(area, point);
				else
					area = QRectF(point, QSizeF(0.0001, 0.0001));
			}
		}
		area.adjust(-5.0, -5.0, 5.0, 5.0);
	}
	MapPrinter printer(map, options.view);
	// The printer made the map print settings where it had none; they are not kept, so that the next file
	// is as this one, and the map is not changed by it.
	if (!has_config)
	{
		auto const unsaved = map.hasUnsavedChanges();
		map.resetPrinterConfig();
		if (!unsaved)
			map.setHasUnsavedChanges(false);
	}
	printer.setTarget(MapPrinter::kmzTarget());
	printer.setMode(MapPrinterOptions::Raster);
	printer.setScale(map.getScaleDenominator());
	printer.setResolution(qRound(options.dpi));
	printer.setPrintTemplates(options.templates);
	if (!has_config)
		printer.setPrintArea(area);

	// With a course without variations, the picture is the map of that course.
	CourseMapDrawing drawing;
	auto const* course = options.course.isEmpty() ? nullptr : data.findCourse(options.course);
	if (course && course->type == Course::Normal)
	{
		CourseMapSide side;
		side.course = course->name;
		drawing = drawCourseMap(map, data, side);
		printer.setOverlay([&map, &drawing](QPainter* painter, const RenderConfig& config, int /*side*/) {
			paintCourseMap(painter, &map, drawing, config.bounding_box, config.scaling);
		});
	}

	// Written beside the file, and put in its place when it is whole.
	auto const info = QFileInfo(path);
	auto const temporary = info.dir().filePath(QLatin1String(".foom-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1String(".kmz"));
	KmzGroundOverlayExport exporter(temporary, map);
	exporter.setDocument(geodata.title, folders.data());
	auto written = false;
	{
		std::unique_ptr<CourseSymbolsHider> hider;
		if (drawing.drawn)
			hider = std::make_unique<CourseSymbolsHider>(map);
		written = exporter.doExport(printer, options.tiles ? 512 : 0);
	}
	if (!written)
	{
		QFile::remove(temporary);
		report.error = exporter.errorString().isEmpty() ? tr("The file %1 cannot be written.").arg(path) : exporter.errorString();
		return report;
	}
	if ((QFile::exists(path) && !QFile::remove(path)) || !QFile::rename(temporary, path))
	{
		QFile::remove(temporary);
		report.error = tr("The file %1 cannot be written.").arg(path);
		return report;
	}
	report.ok = true;
#else
	Q_UNUSED(map)
	Q_UNUSED(path)
	Q_UNUSED(options)
	Q_UNUSED(geodata)
	report.error = tr("This program cannot write KMZ files.");
#endif
	return report;
}


QByteArray readKmzEntry(const QString& path, const QString& entry)
{
#ifdef MAPPER_USE_GDAL
	return readZipEntry(path, entry);
#else
	Q_UNUSED(path)
	Q_UNUSED(entry)
	return {};
#endif
}


CourseGeodataImportReport importCoursesGpx(Map& map, QIODevice& device, const CourseGeodataImportOptions& options)
{
	if (!isGeoreferenced(map))
		return refusedImport(notGeoreferenced());
	InGeodata file;
	auto const error = readGpx(device, file);
	if (!error.isEmpty())
		return refusedImport(error);
	return importGeodata(map, file, options);
}


CourseGeodataImportReport importCoursesKml(Map& map, QIODevice& device, const CourseGeodataImportOptions& options)
{
	if (!isGeoreferenced(map))
		return refusedImport(notGeoreferenced());
	InGeodata file;
	auto const error = readKml(device, file);
	if (!error.isEmpty())
		return refusedImport(error);
	return importGeodata(map, file, options);
}


CourseGeodataImportReport importCourseFile(Map& map, const QString& path, const CourseGeodataImportOptions& options)
{
	auto const suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QLatin1String("kmz"))
	{
		auto bytes = readKmzEntry(path);
		if (bytes.isEmpty())
			return refusedImport(tr("The file %1 is no KMZ archive with a KML document.").arg(path));
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::ReadOnly);
		return importCoursesKml(map, buffer, options);
	}
	if (suffix != QLatin1String("gpx") && suffix != QLatin1String("kml"))
		return refusedImport(tr("The file %1 is no GPX, KML or KMZ file.").arg(path));
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return refusedImport(tr("The file %1 cannot be read.").arg(path));
	return suffix == QLatin1String("gpx") ? importCoursesGpx(map, file, options) : importCoursesKml(map, file, options);
}


}  // namespace OpenOrienteering
