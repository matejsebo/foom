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


#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "global.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_interchange.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const QString iof_namespace = QStringLiteral("http://www.orienteering.org/datastandard/3.0");

/// The starts, controls and finishes of a forest course.
constexpr int SAMPLE_CONTROLS = 48;

/// An element of an XML file, for looking at what was written.
struct Node
{
	QString name;
	QString name_space;
	std::map<QString, QString> attributes;
	QString text;
	std::vector<Node> children;
	
	std::vector<const Node*> all(const char* child_name) const
	{
		std::vector<const Node*> result;
		for (auto const& child : children)
		{
			if (child.name == QLatin1String(child_name))
				result.push_back(&child);
		}
		return result;
	}
	
	const Node* first(const char* child_name) const
	{
		auto const found = all(child_name);
		return found.empty() ? nullptr : found.front();
	}
	
	QString textOf(const char* child_name) const
	{
		auto const* child = first(child_name);
		return child ? child->text : QString{};
	}
	
	bool has(const char* attribute) const
	{
		return attributes.count(QString::fromLatin1(attribute)) > 0;
	}
	
	QString attribute(const char* attribute) const
	{
		auto const found = attributes.find(QString::fromLatin1(attribute));
		return found == attributes.end() ? QString{} : found->second;
	}
};

Node readNode(QXmlStreamReader& xml)
{
	Node node;
	node.name = xml.name().toString();
	node.name_space = xml.namespaceUri().toString();
	for (auto const& attribute : xml.attributes())
		node.attributes[attribute.name().toString()] = attribute.value().toString();
	while (!xml.atEnd())
	{
		auto const token = xml.readNext();
		if (token == QXmlStreamReader::StartElement)
			node.children.push_back(readNode(xml));
		else if (token == QXmlStreamReader::Characters && !xml.isWhitespace())
			node.text += xml.text();
		else if (token == QXmlStreamReader::EndElement)
			break;
	}
	return node;
}

/// The root element of a file; its name is empty when the file is no XML.
Node parse(const QByteArray& bytes)
{
	QXmlStreamReader xml(bytes);
	Node root;
	if (xml.readNextStartElement())
		root = readNode(xml);
	if (xml.hasError())
		root.name.clear();
	return root;
}

/// The children which the schema of the IOF allows, in its order.
QStringList allowedChildren(const QString& parent, const QString& name)
{
	auto const list = [](std::initializer_list<const char*> names) {
		QStringList result;
		for (auto const* entry : names)
			result.push_back(QString::fromLatin1(entry));
		return result;
	};
	if (name == QLatin1String("CourseData"))
		return list({ "Event", "RaceCourseData", "Extensions" });
	if (name == QLatin1String("Event"))
		return list({ "Id", "Name", "StartTime", "EndTime" });
	if (name == QLatin1String("RaceCourseData"))
		return list({ "Map", "Control", "Course", "ClassCourseAssignment", "PersonCourseAssignment", "TeamCourseAssignment", "Extensions" });
	if (name == QLatin1String("Map"))
		return list({ "Id", "Image", "Scale", "MapPositionTopLeft", "MapPositionBottomRight", "Extensions" });
	if (name == QLatin1String("Control") && parent == QLatin1String("RaceCourseData"))
		return list({ "Id", "PunchingUnitId", "Name", "Position", "MapPosition", "Extensions" });
	if (name == QLatin1String("Course"))
		return list({ "Id", "Name", "CourseFamily", "Length", "Climb", "CourseControl", "MapId", "Extensions" });
	if (name == QLatin1String("CourseControl"))
		return list({ "Control", "MapText", "MapTextPosition", "LegLength", "Score", "Extensions" });
	if (name == QLatin1String("ClassCourseAssignment"))
		return list({ "ClassId", "ClassName", "AllowedOnLeg", "CourseName", "CourseFamily", "Extensions" });
	return {};
}

/// Says what in a tree is not as the schema has it; empty when all is.
QString schemaFault(const Node& node, const QString& parent = {})
{
	if (node.name_space != iof_namespace)
		return QStringLiteral("namespace of ") + node.name;
	auto const allowed = allowedChildren(parent, node.name);
	auto last = 0;
	for (auto const& child : node.children)
	{
		auto const index = allowed.indexOf(child.name);
		if (index < 0)
			return child.name + QStringLiteral(" in ") + node.name;
		if (index < last)
			return child.name + QStringLiteral(" out of order in ") + node.name;
		last = index;
		auto const fault = schemaFault(child, node.name);
		if (!fault.isEmpty())
			return fault;
	}
	return {};
}

Georeferencing utm32Georeferencing()
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	return georef;
}

struct Symbols
{
	PointSymbol* start = nullptr;
	PointSymbol* control = nullptr;
	PointSymbol* finish = nullptr;
	LineSymbol* route = nullptr;
};

template <class SymbolType>
SymbolType* addSymbol(Map& map, int number, Symbol::CourseRole role)
{
	auto* symbol = new SymbolType();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

/// Gives a map the scale and the symbols of the cases; a map of the
/// scale 0 keeps its own.
Symbols fillSymbols(Map& map, bool with_start = true)
{
	map.setScaleDenominator(10000);
	Symbols symbols;
	if (with_start)
		symbols.start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	symbols.control = addSymbol<PointSymbol>(map, 703, Symbol::CourseControl);
	symbols.finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	symbols.route = addSymbol<LineSymbol>(map, 707, Symbol::CourseMarkedRoute);
	return symbols;
}

/// Adds an object as the drawing tools do.
void push(Map& map, Object* object)
{
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	push(map, object);
}

CourseItem item(CourseItem::Type type, const QString& code = {}, int count = 0)
{
	CourseItem result;
	result.type = type;
	result.code = code;
	result.count = count;
	return result;
}

void addCourseOf(CourseData& data, const QString& name, const QString& codes)
{
	QVERIFY(addCourse(data, name));
	auto index = 0;
	for (auto const& code : codes.split(QLatin1Char(' '), QString::SkipEmptyParts))
		QVERIFY(insertCourseItem(data, name, index++, code));
}

void addClass(CourseData& data, const QString& name, const QString& course, int runners)
{
	CourseClass course_class;
	course_class.name = name;
	course_class.course = course;
	course_class.runners = runners;
	data.classes.push_back(course_class);
}

/// The map of the acceptance criteria.
void fillInterchangeMap(Map& map, bool georeferenced = true)
{
	auto const symbols = fillSymbols(map);
	if (georeferenced)
		map.setGeoreferencing(utm32Georeferencing());
	drawPoint(map, symbols.start, 0.0, 0.0);
	drawPoint(map, symbols.control, 10.0, 0.0);
	drawPoint(map, symbols.control, 20.0, 0.0);
	drawPoint(map, symbols.control, 20.0, 10.0);
	drawPoint(map, symbols.finish, 30.0, 0.0);
	push(map, new PathObject(symbols.route, { MapCoord(20.0, 10.0), MapCoord(25.0, 8.0), MapCoord(29.0, 1.0) }));
	
	auto data = map.courseData();
	data.options.setValue(QLatin1Char('e'), QStringLiteral("Test event"));
	for (auto& object : data.objects)
	{
		if (object.code == QLatin1String("31"))
			object.punch_ids = QStringList{ QStringLiteral("131") };
	}
	addCourseOf(data, QStringLiteral("A"), QStringLiteral("S1 31 32 33 M1 F1"));
	data.courses.back().climb_m = 50;
	data.courses.back().climb_manual = true;
	Course second;
	second.name = QStringLiteral("B");
	second.items = {
	    item(CourseItem::Start, QStringLiteral("S1")),
	    item(CourseItem::CrossingPoint),
	    item(CourseItem::Control, QStringLiteral("32")),
	    item(CourseItem::Finish, QStringLiteral("F1")),
	};
	data.courses.push_back(second);
	Course relay;
	relay.name = QStringLiteral("R");
	relay.type = Course::Relay;
	relay.legs = 2;
	relay.items = {
	    item(CourseItem::Start, QStringLiteral("S1")),
	    item(CourseItem::TeamForkBegin, {}, 2),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("31")),
	    item(CourseItem::ForkBranch),
	    item(CourseItem::Control, QStringLiteral("32")),
	    item(CourseItem::TeamForkEnd),
	    item(CourseItem::Finish, QStringLiteral("F1")),
	};
	data.courses.push_back(relay);
	addCourseOf(data, QStringLiteral("Short"), QStringLiteral("31"));
	addClass(data, QStringLiteral("H21"), QStringLiteral("A"), 40);
	addClass(data, QStringLiteral("H20"), QStringLiteral("A"), -1);
	addClass(data, QStringLiteral("Teams"), QStringLiteral("R"), 12);
	changeCourseData(map, data);
}

QByteArray iofOf(const Map& map, CourseExportReport* report = nullptr)
{
	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	auto const result = writeIofCourseData(map, buffer);
	if (report)
		*report = result;
	return buffer.data();
}

IofImportReport importBytes(Map& map, QByteArray bytes, const IofImportOptions& options = {})
{
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::ReadOnly);
	return importIofCourseData(map, buffer, options);
}

/// A CourseData file with the given content of RaceCourseData.
QByteArray courseDataFile(const char* content)
{
	return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	                  "<CourseData xmlns=\"http://www.orienteering.org/datastandard/3.0\" iofVersion=\"3.0\">\n"
	                  "<Event><Name>E</Name></Event>\n<RaceCourseData>\n")
	       + content + "\n</RaceCourseData>\n</CourseData>\n";
}

QString itemsOf(const CourseData& data, const QString& name)
{
	auto const* found = data.findCourse(name);
	if (!found)
		return QStringLiteral("?");
	QStringList codes;
	for (auto const& entry : found->items)
		codes.push_back(entry.namesObject() ? entry.code : entry.type == CourseItem::CrossingPoint ? QStringLiteral("+") : QStringLiteral("-"));
	return codes.join(QLatin1Char(' '));
}

QString objectsOf(const CourseData& data)
{
	QStringList codes;
	for (auto const& object : data.objects)
		codes.push_back(object.code);
	return codes.join(QLatin1Char(' '));
}

/// The place of a course object in mm; far away when it has none.
MapCoordF placeOf(const Map& map, const QString& code)
{
	auto const* object = map.courseData().findObject(code);
	auto const* map_object = object ? map.findObject(object->object) : nullptr;
	if (!map_object || map_object->getType() != Object::Point)
		return MapCoordF(-9999.0, -9999.0);
	return map_object->asPoint()->getCoordF();
}

bool near(const MapCoordF& place, double x, double y, double tolerance = 0.011)
{
	return std::abs(place.x() - x) < tolerance && std::abs(place.y() - y) < tolerance;
}

}  // namespace



/**
 * @test Tests the interchange of courses: IOF XML 3.0 out and in, GPX and KML.
 *
 * See the design notes, "Interchange: IOF XML, GPX and KML".
 */
class CourseInterchangeTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void iofExportTest();
	void localExportTest();
	void roundTripTest();
	void importIntoExistingTest();
	void positionTest();
	void refusedTest();
	void gpxTest();
	void kmlTest();
	void filesTest();
	void editorTest();
	void schemaTest();
};


void CourseInterchangeTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseInterchangeTest::iofExportTest()
{
	Map map;
	fillInterchangeMap(map);
	CourseExportReport report;
	auto const bytes = iofOf(map, &report);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.controls, 5);
	QCOMPARE(report.courses, 2);
	QCOMPARE(report.classes, 2);
	QCOMPARE(report.skipped_courses, (QStringList{ QStringLiteral("R"), QStringLiteral("Short") }));
	
	auto const root = parse(bytes);
	QCOMPARE(root.name, QStringLiteral("CourseData"));
	QCOMPARE(root.name_space, iof_namespace);
	QCOMPARE(root.attribute("iofVersion"), QStringLiteral("3.0"));
	QVERIFY(root.attribute("creator").startsWith(QStringLiteral("FOOM")));
	QVERIFY(root.has("createTime"));
	QCOMPARE(schemaFault(root), QString{});
	QVERIFY(root.first("Event"));
	QCOMPARE(root.first("Event")->textOf("Name"), QStringLiteral("Test event"));
	
	auto const* race = root.first("RaceCourseData");
	QVERIFY(race);
	QCOMPARE(int(race->all("Map").size()), 1);
	QCOMPARE(race->first("Map")->textOf("Scale"), QStringLiteral("10000"));
	QVERIFY(race->first("Map")->first("MapPositionTopLeft"));
	QVERIFY(race->first("Map")->first("MapPositionBottomRight"));
	// y grows downwards: the top has the smaller number. The places of the objects are inside.
	QVERIFY(race->first("Map")->first("MapPositionTopLeft")->attribute("y").toDouble() <= 0.0);
	QVERIFY(race->first("Map")->first("MapPositionBottomRight")->attribute("y").toDouble() >= 10.0);
	QVERIFY(race->first("Map")->first("MapPositionTopLeft")->attribute("x").toDouble() <= 0.0);
	QVERIFY(race->first("Map")->first("MapPositionBottomRight")->attribute("x").toDouble() >= 30.0);
	
	auto const controls = race->all("Control");
	QCOMPARE(int(controls.size()), 5);
	QStringList ids;
	QStringList types;
	for (auto const* control : controls)
	{
		ids.push_back(control->textOf("Id"));
		types.push_back(control->attribute("type"));
	}
	QCOMPARE(ids, (QStringList{ QStringLiteral("S1"), QStringLiteral("31"), QStringLiteral("32"), QStringLiteral("33"), QStringLiteral("F1") }));
	QCOMPARE(types, (QStringList{ QStringLiteral("Start"), QStringLiteral("Control"), QStringLiteral("Control"), QStringLiteral("Control"), QStringLiteral("Finish") }));
	
	auto const* control_31 = controls[1];
	QCOMPARE(control_31->textOf("PunchingUnitId"), QStringLiteral("131"));
	QVERIFY(control_31->first("Position"));
	auto const expected = map.getGeoreferencing().toGeographicCoords(MapCoordF(10.0, 0.0));
	QVERIFY(std::abs(control_31->first("Position")->attribute("lng").toDouble() - expected.longitude()) < 1e-6);
	QVERIFY(std::abs(control_31->first("Position")->attribute("lat").toDouble() - expected.latitude()) < 1e-6);
	QVERIFY(control_31->first("MapPosition"));
	QCOMPARE(control_31->first("MapPosition")->attribute("x"), QStringLiteral("10.00"));
	QCOMPARE(control_31->first("MapPosition")->attribute("y"), QStringLiteral("0.00"));
	QCOMPARE(control_31->first("MapPosition")->attribute("unit"), QStringLiteral("mm"));
	QVERIFY(!controls[0]->first("PunchingUnitId"));
	
	auto const courses = race->all("Course");
	QCOMPARE(int(courses.size()), 2);
	auto const* course_a = courses[0];
	QCOMPARE(course_a->textOf("Name"), QStringLiteral("A"));
	auto const length = measureCourse(map, map.courseData(), *map.courseData().findCourse(QStringLiteral("A")));
	QVERIFY(length.known);
	QCOMPARE(course_a->textOf("Length"), QString::number(qRound(length.total_m)));
	QCOMPARE(course_a->textOf("Climb"), QStringLiteral("50"));
	auto const course_controls = course_a->all("CourseControl");
	QCOMPARE(int(course_controls.size()), 5);
	QCOMPARE(course_controls[0]->attribute("type"), QStringLiteral("Start"));
	QCOMPARE(course_controls[0]->textOf("Control"), QStringLiteral("S1"));
	QVERIFY(!course_controls[0]->first("LegLength"));
	QCOMPARE(course_controls[1]->attribute("type"), QStringLiteral("Control"));
	QCOMPARE(course_controls[1]->textOf("Control"), QStringLiteral("31"));
	QCOMPARE(course_controls[1]->textOf("LegLength"), QStringLiteral("100"));
	QVERIFY(!course_controls[1]->has("specialInstruction"));
	
	// The marked route: its path of 5.385 and 8.062 mm, and 1.414 mm to the finish.
	auto const* to_finish = course_controls[4];
	QCOMPARE(to_finish->attribute("type"), QStringLiteral("Finish"));
	QCOMPARE(to_finish->textOf("Control"), QStringLiteral("F1"));
	QCOMPARE(to_finish->attribute("specialInstruction"), QStringLiteral("TapedRoute"));
	QCOMPARE(to_finish->attribute("tapedRouteLength"), QStringLiteral("134"));
	QCOMPARE(to_finish->textOf("LegLength"), QStringLiteral("149"));
	
	auto const* course_b = courses[1];
	QCOMPARE(course_b->textOf("Name"), QStringLiteral("B"));
	QVERIFY(!course_b->first("Climb"));
	auto const b_controls = course_b->all("CourseControl");
	QCOMPARE(int(b_controls.size()), 3);
	QCOMPARE(b_controls[1]->textOf("Control"), QStringLiteral("32"));
	QCOMPARE(b_controls[1]->attribute("specialInstruction"), QStringLiteral("MandatoryCrossingPoint"));
	QCOMPARE(b_controls[1]->textOf("LegLength"), QStringLiteral("200"));
	
	auto const classes = race->all("ClassCourseAssignment");
	QCOMPARE(int(classes.size()), 2);
	QCOMPARE(classes[0]->textOf("ClassName"), QStringLiteral("H21"));
	QCOMPARE(classes[0]->textOf("CourseName"), QStringLiteral("A"));
	QCOMPARE(classes[0]->attribute("numberOfCompetitors"), QStringLiteral("40"));
	QCOMPARE(classes[1]->textOf("ClassName"), QStringLiteral("H20"));
	QVERIFY(!classes[1]->has("numberOfCompetitors"));
	
	// A funnel.
	auto data = map.courseData();
	for (auto& object : data.objects)
	{
		if (object.code == QLatin1String("M1"))
			object.other.push_back({ QLatin1Char('m'), QStringLiteral("f") });
	}
	changeCourseData(map, data);
	auto const funnel = parse(iofOf(map));
	QCOMPARE(funnel.first("RaceCourseData")->all("Course")[0]->all("CourseControl")[4]->attribute("specialInstruction"), QStringLiteral("FunnelTapedRoute"));
}


/// Accept (2)
void CourseInterchangeTest::localExportTest()
{
	Map map;
	fillInterchangeMap(map, false);
	CourseExportReport report;
	auto const root = parse(iofOf(map, &report));
	QVERIFY(report.ok);
	QCOMPARE(schemaFault(root), QString{});
	auto const controls = root.first("RaceCourseData")->all("Control");
	QCOMPARE(int(controls.size()), 5);
	for (auto const* control : controls)
		QVERIFY(!control->first("Position"));
	QCOMPARE(controls[3]->first("MapPosition")->attribute("x"), QStringLiteral("20.00"));
	QCOMPARE(controls[3]->first("MapPosition")->attribute("y"), QStringLiteral("10.00"));
}


/// Accept (3)
void CourseInterchangeTest::roundTripTest()
{
	Map source;
	fillInterchangeMap(source);
	auto const bytes = iofOf(source);
	
	Map map;
	fillSymbols(map);
	auto const steps = map.undoManager().undoStepCount();
	auto const report = importBytes(map, bytes);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.objects_added, 5);
	QCOMPARE(report.objects_found, 0);
	QCOMPARE(report.courses_added, 2);
	QCOMPARE(report.classes_added, 2);
	QCOMPARE(report.taped_routes, 1);
	QVERIFY(report.objects_not_made.isEmpty());
	QVERIFY(report.codes_missing.isEmpty());
	
	auto const& data = map.courseData();
	QCOMPARE(objectsOf(data), QStringLiteral("S1 31 32 33 F1"));
	QCOMPARE(data.findObject(QStringLiteral("S1"))->kind, CourseObject::Start);
	QCOMPARE(data.findObject(QStringLiteral("31"))->kind, CourseObject::Control);
	QCOMPARE(data.findObject(QStringLiteral("F1"))->kind, CourseObject::Finish);
	QCOMPARE(data.findObject(QStringLiteral("31"))->punch_ids, QStringList{ QStringLiteral("131") });
	QCOMPARE(map.getNumObjects(), 5);
	QVERIFY(near(placeOf(map, QStringLiteral("S1")), 0.0, 0.0));
	QVERIFY(near(placeOf(map, QStringLiteral("31")), 10.0, 0.0));
	QVERIFY(near(placeOf(map, QStringLiteral("33")), 20.0, 10.0));
	QVERIFY(near(placeOf(map, QStringLiteral("F1")), 30.0, 0.0));
	auto const* start_object = map.findObject(data.findObject(QStringLiteral("S1"))->object);
	QCOMPARE(start_object->getSymbol()->courseRole(), Symbol::CourseStart);
	
	QCOMPARE(int(data.courses.size()), 2);
	QCOMPARE(itemsOf(data, QStringLiteral("A")), QStringLiteral("S1 31 32 33 F1"));
	QCOMPARE(data.findCourse(QStringLiteral("A"))->climb_m, 50);
	QCOMPARE(itemsOf(data, QStringLiteral("B")), QStringLiteral("S1 + 32 F1"));
	QCOMPARE(data.findCourse(QStringLiteral("B"))->items[0].type, CourseItem::Start);
	QCOMPARE(data.findCourse(QStringLiteral("B"))->items[3].type, CourseItem::Finish);
	
	QCOMPARE(int(data.classes.size()), 2);
	QCOMPARE(data.classes[0].name, QStringLiteral("H21"));
	QCOMPARE(data.classes[0].course, QStringLiteral("A"));
	QCOMPARE(data.classes[0].runners, 40);
	QCOMPARE(data.classes[1].name, QStringLiteral("H20"));
	QCOMPARE(data.classes[1].runners, -1);
	
	// One undo step.
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QVERIFY(map.undoManager().undo());
	QCOMPARE(map.getNumObjects(), 0);
	QVERIFY(map.courseData().isEmpty());
	QVERIFY(map.undoManager().redo());
	QCOMPARE(map.getNumObjects(), 5);
	QCOMPARE(itemsOf(map.courseData(), QStringLiteral("A")), QStringLiteral("S1 31 32 33 F1"));
	
	// What was imported is written as it was read.
	auto const again = parse(iofOf(map));
	QCOMPARE(int(again.first("RaceCourseData")->all("Control").size()), 5);
	QCOMPARE(again.first("RaceCourseData")->all("Course")[1]->all("CourseControl")[1]->attribute("specialInstruction"), QStringLiteral("MandatoryCrossingPoint"));
}


/// Accept (4)
void CourseInterchangeTest::importIntoExistingTest()
{
	Map map;
	fillInterchangeMap(map);
	auto const bytes = iofOf(map);
	auto const before = map.courseData();
	auto const steps = map.undoManager().undoStepCount();
	auto const objects = map.getNumObjects();
	
	auto report = importBytes(map, bytes);
	QVERIFY(report.ok);
	QCOMPARE(report.objects_added, 0);
	QCOMPARE(report.objects_found, 5);
	QCOMPARE(report.courses_added, 0);
	QCOMPARE(report.courses_existing, (QStringList{ QStringLiteral("A"), QStringLiteral("B") }));
	QCOMPARE(report.classes_added, 0);
	QVERIFY(map.courseData() == before);
	QCOMPARE(map.getNumObjects(), objects);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
	
	// The course objects alone.
	Map empty;
	fillSymbols(empty);
	IofImportOptions options;
	options.courses = false;
	options.classes = false;
	report = importBytes(empty, bytes, options);
	QVERIFY(report.ok);
	QCOMPARE(report.objects_added, 5);
	QCOMPARE(report.courses_added, 0);
	QCOMPARE(report.classes_added, 0);
	QCOMPARE(objectsOf(empty.courseData()), QStringLiteral("S1 31 32 33 F1"));
	QVERIFY(empty.courseData().courses.empty());
	QVERIFY(empty.courseData().classes.empty());
	
	// The courses and classes alone, into a map which has the objects.
	options.objects = false;
	options.courses = true;
	options.classes = true;
	report = importBytes(empty, bytes, options);
	QVERIFY(report.ok);
	QCOMPARE(report.objects_added, 0);
	QCOMPARE(report.courses_added, 2);
	QCOMPARE(report.classes_added, 2);
	QCOMPARE(empty.getNumObjects(), 5);
	
	// Without the objects, the courses have nothing to name.
	Map bare;
	fillSymbols(bare);
	report = importBytes(bare, bytes, options);
	QVERIFY(report.ok);
	QCOMPARE(report.objects_added, 0);
	QCOMPARE(bare.getNumObjects(), 0);
	QVERIFY(report.codes_missing.contains(QStringLiteral("31")));
	QCOMPARE(itemsOf(bare.courseData(), QStringLiteral("A")), QString{});
}


/// Accept (5)
void CourseInterchangeTest::positionTest()
{
	{
		// y upwards, as the Map element shows.
		Map map;
		fillSymbols(map);
		auto const report = importBytes(map, courseDataFile(
		    "<Map><Scale>10000</Scale><MapPositionTopLeft x=\"-50\" y=\"80\"/><MapPositionBottomRight x=\"50\" y=\"-80\"/></Map>"
		    "<Control><Id>41</Id><MapPosition x=\"5\" y=\"10\" unit=\"mm\"/></Control>"));
		QVERIFY2(report.ok, qPrintable(report.error));
		QCOMPARE(report.objects_added, 1);
		QVERIFY(near(placeOf(map, QStringLiteral("41")), 5.0, -10.0));
		QCOMPARE(map.courseData().findObject(QStringLiteral("41"))->kind, CourseObject::Control);
	}
	{
		// Another scale.
		Map map;
		fillSymbols(map);
		auto const report = importBytes(map, courseDataFile(
		    "<Map><Scale>5000</Scale><MapPositionTopLeft x=\"-50\" y=\"-80\"/><MapPositionBottomRight x=\"50\" y=\"80\"/></Map>"
		    "<Control><Id>41</Id><MapPosition x=\"20\" y=\"6\"/></Control>"));
		QVERIFY(report.ok);
		QVERIFY(near(placeOf(map, QStringLiteral("41")), 10.0, 3.0));
	}
	{
		// The geographic position goes before the position in the map.
		Map map;
		fillSymbols(map);
		map.setGeoreferencing(utm32Georeferencing());
		auto const place = map.getGeoreferencing().toGeographicCoords(MapCoordF(12.0, 7.0));
		auto const content = QStringLiteral("<Control><Id>41</Id><Position lng=\"%1\" lat=\"%2\"/><MapPosition x=\"99\" y=\"99\"/></Control>")
		                     .arg(place.longitude(), 0, 'f', 8).arg(place.latitude(), 0, 'f', 8);
		auto const report = importBytes(map, courseDataFile(content.toUtf8().constData()));
		QVERIFY(report.ok);
		QVERIFY(near(placeOf(map, QStringLiteral("41")), 12.0, 7.0));
		
		// Without georeferencing it is the position in the map.
		Map local;
		fillSymbols(local);
		QVERIFY(importBytes(local, courseDataFile(content.toUtf8().constData())).ok);
		QVERIFY(near(placeOf(local, QStringLiteral("41")), 99.0, 99.0));
	}
	{
		// No place, no symbol, and the kind which a course gives.
		Map map;
		fillSymbols(map, false);
		auto const report = importBytes(map, courseDataFile(
		    "<Control><Id>41</Id><MapPosition x=\"5\" y=\"5\"/></Control>"
		    "<Control><Id>42</Id></Control>"
		    "<Control><Id>43</Id><MapPosition x=\"7\" y=\"7\" unit=\"px\"/></Control>"
		    "<Control type=\"Start\"><Id>S9</Id><MapPosition x=\"1\" y=\"1\"/></Control>"
		    "<Control><Id>F5</Id><MapPosition x=\"9\" y=\"9\"/></Control>"
		    "<Control type=\"CrossingPoint\"><Id>X1</Id><MapPosition x=\"3\" y=\"3\"/></Control>"
		    "<Course><Name>X</Name>"
		    "<CourseControl type=\"Start\"><Control>S9</Control></CourseControl>"
		    "<CourseControl type=\"Control\"><Control>41</Control></CourseControl>"
		    "<CourseControl type=\"Control\" specialInstruction=\"MandatoryOutOfBoundsAreaPassage\"><Control>42</Control></CourseControl>"
		    "<CourseControl type=\"Finish\" specialInstruction=\"FunnelTapedRoute\" tapedRouteLength=\"80\"><Control>F5</Control></CourseControl>"
		    "</Course>"
		    "<ClassCourseAssignment numberOfCompetitors=\"7\"><ClassName>K</ClassName><CourseName>X</CourseName></ClassCourseAssignment>"
		    "<ClassCourseAssignment><ClassName>L</ClassName><CourseName>Nowhere</CourseName></ClassCourseAssignment>"));
		QVERIFY2(report.ok, qPrintable(report.error));
		QCOMPARE(report.objects_added, 2);
		QCOMPARE(objectsOf(map.courseData()), QStringLiteral("41 F5"));
		QCOMPARE(map.courseData().findObject(QStringLiteral("F5"))->kind, CourseObject::Finish);
		QVERIFY(near(placeOf(map, QStringLiteral("F5")), 9.0, 9.0));
		QCOMPARE(report.objects_not_made, (QStringList{ QStringLiteral("42"), QStringLiteral("43"), QStringLiteral("S9"), QStringLiteral("X1") }));
		QCOMPARE(report.codes_missing, (QStringList{ QStringLiteral("S9"), QStringLiteral("42") }));
		QCOMPARE(report.taped_routes, 1);
		QCOMPARE(report.courses_added, 1);
		QCOMPARE(int(map.courseData().findCourse(QStringLiteral("X"))->items.size()), 2);
		QCOMPARE(itemsOf(map.courseData(), QStringLiteral("X")), QStringLiteral("41 F5"));
		QCOMPARE(report.classes_added, 1);
		QCOMPARE(map.courseData().classes[0].name, QStringLiteral("K"));
		QCOMPARE(map.courseData().classes[0].runners, 7);
		QCOMPARE(report.classes_skipped, QStringList{ QStringLiteral("L") });
	}
}


/// Accept (6)
void CourseInterchangeTest::refusedTest()
{
	Map map;
	fillInterchangeMap(map);
	auto const before = map.courseData();
	auto const steps = map.undoManager().undoStepCount();
	
	auto report = importBytes(map, QByteArray("This is no XML."));
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	report = importBytes(map, QByteArray("<?xml version=\"1.0\"?><gpx xmlns=\"http://www.topografix.com/GPX/1/1\"/>"));
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	// Cut off in the middle.
	auto bytes = iofOf(map);
	bytes.truncate(bytes.size() / 2);
	Map empty;
	fillSymbols(empty);
	report = importBytes(empty, bytes);
	QVERIFY(!report.ok);
	QCOMPARE(empty.getNumObjects(), 0);
	QVERIFY(empty.courseData().isEmpty());
	
	QVERIFY(map.courseData() == before);
	QCOMPARE(map.undoManager().undoStepCount(), steps);
}


/// Accept (7)
void CourseInterchangeTest::gpxTest()
{
	Map map;
	fillInterchangeMap(map);
	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	auto report = writeCoursesGpx(map, buffer);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.controls, 5);
	QCOMPARE(report.courses, 3);
	QCOMPARE(report.skipped_courses, QStringList{ QStringLiteral("R") });
	
	auto const root = parse(buffer.data());
	QCOMPARE(root.name, QStringLiteral("gpx"));
	QCOMPARE(root.name_space, QStringLiteral("http://www.topografix.com/GPX/1/1"));
	QCOMPARE(root.attribute("version"), QStringLiteral("1.1"));
	QVERIFY(root.attribute("creator").startsWith(QStringLiteral("FOOM")));
	auto const waypoints = root.all("wpt");
	QCOMPARE(int(waypoints.size()), 5);
	QCOMPARE(waypoints[1]->textOf("name"), QStringLiteral("31"));
	auto const expected = map.getGeoreferencing().toGeographicCoords(MapCoordF(10.0, 0.0));
	QVERIFY(std::abs(waypoints[1]->attribute("lat").toDouble() - expected.latitude()) < 1e-6);
	QVERIFY(std::abs(waypoints[1]->attribute("lon").toDouble() - expected.longitude()) < 1e-6);
	
	auto const tracks = root.all("trk");
	QCOMPARE(int(tracks.size()), 3);
	QCOMPARE(tracks[0]->textOf("name"), QStringLiteral("A"));
	QCOMPARE(tracks[1]->textOf("name"), QStringLiteral("B"));
	QCOMPARE(tracks[2]->textOf("name"), QStringLiteral("Short"));
	QVERIFY(tracks[0]->first("trkseg"));
	auto const points = tracks[0]->first("trkseg")->all("trkpt");
	QCOMPARE(int(points.size()), 8);
	auto const last = map.getGeoreferencing().toGeographicCoords(MapCoordF(30.0, 0.0));
	QVERIFY(std::abs(points[7]->attribute("lat").toDouble() - last.latitude()) < 1e-6);
	QVERIFY(std::abs(points[7]->attribute("lon").toDouble() - last.longitude()) < 1e-6);
	QCOMPARE(int(tracks[1]->first("trkseg")->all("trkpt").size()), 3);
	
	// One course.
	QBuffer second;
	second.open(QIODevice::WriteOnly);
	report = writeCoursesGpx(map, second, QStringLiteral("B"));
	QVERIFY(report.ok);
	auto const one = parse(second.data());
	QCOMPARE(int(one.all("wpt").size()), 3);
	QCOMPARE(one.all("wpt")[1]->textOf("name"), QStringLiteral("32"));
	QCOMPARE(int(one.all("trk").size()), 1);
	
	// A relay, a course which is not there, and a map without georeferencing.
	QBuffer third;
	third.open(QIODevice::WriteOnly);
	QVERIFY(!writeCoursesGpx(map, third, QStringLiteral("R")).ok);
	QVERIFY(!writeCoursesGpx(map, third, QStringLiteral("Nowhere")).ok);
	Map local;
	fillInterchangeMap(local, false);
	report = writeCoursesGpx(local, third);
	QVERIFY(!report.ok);
	QVERIFY(!report.error.isEmpty());
	QVERIFY(third.data().isEmpty());
}


/// Accept (8)
void CourseInterchangeTest::kmlTest()
{
	Map map;
	fillInterchangeMap(map);
	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	auto const report = writeCoursesKml(map, buffer);
	QVERIFY2(report.ok, qPrintable(report.error));
	
	auto const root = parse(buffer.data());
	QCOMPARE(root.name, QStringLiteral("kml"));
	QCOMPARE(root.name_space, QStringLiteral("http://www.opengis.net/kml/2.2"));
	auto const* document = root.first("Document");
	QVERIFY(document);
	QCOMPARE(document->textOf("name"), QStringLiteral("Test event"));
	auto const folders = document->all("Folder");
	QCOMPARE(int(folders.size()), 2);
	
	auto const objects = folders[0]->all("Placemark");
	QCOMPARE(int(objects.size()), 5);
	QCOMPARE(objects[1]->textOf("name"), QStringLiteral("31"));
	QVERIFY(objects[1]->first("Point"));
	auto const expected = map.getGeoreferencing().toGeographicCoords(MapCoordF(10.0, 0.0));
	auto const tuple = objects[1]->first("Point")->textOf("coordinates").split(QLatin1Char(','));
	QCOMPARE(tuple.size(), 3);
	QVERIFY(std::abs(tuple[0].toDouble() - expected.longitude()) < 1e-6);
	QVERIFY(std::abs(tuple[1].toDouble() - expected.latitude()) < 1e-6);
	
	auto const courses = folders[1]->all("Placemark");
	QCOMPARE(int(courses.size()), 3);
	QCOMPARE(courses[0]->textOf("name"), QStringLiteral("A"));
	QVERIFY(courses[0]->first("LineString"));
	QCOMPARE(courses[0]->first("LineString")->textOf("coordinates").split(QLatin1Char(' '), QString::SkipEmptyParts).size(), 8);
	QCOMPARE(courses[1]->first("LineString")->textOf("coordinates").split(QLatin1Char(' '), QString::SkipEmptyParts).size(), 3);
	
	Map local;
	fillInterchangeMap(local, false);
	QBuffer second;
	second.open(QIODevice::WriteOnly);
	QVERIFY(!writeCoursesKml(local, second).ok);
}


/// Accept (9)
void CourseInterchangeTest::filesTest()
{
	Map map;
	fillInterchangeMap(map);
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	auto const dir = QDir(folder.path());
	
	auto report = writeCourseFiles(map, dir.filePath(QStringLiteral("event.kml")), CourseFileKind::Kml, true);
	QVERIFY2(report.ok, qPrintable(report.error));
	QCOMPARE(report.files.size(), 3);
	QVERIFY(dir.exists(QStringLiteral("event.A.kml")));
	QVERIFY(dir.exists(QStringLiteral("event.B.kml")));
	QVERIFY(dir.exists(QStringLiteral("event.Short.kml")));
	QVERIFY(!dir.exists(QStringLiteral("event.kml")));
	QVERIFY(!dir.exists(QStringLiteral("event.R.kml")));
	QCOMPARE(report.skipped_courses, QStringList{ QStringLiteral("R") });
	QFile file(dir.filePath(QStringLiteral("event.B.kml")));
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto const root = parse(file.readAll());
	QCOMPARE(int(root.first("Document")->all("Folder")[0]->all("Placemark").size()), 3);
	QCOMPARE(int(root.first("Document")->all("Folder")[1]->all("Placemark").size()), 1);
	
	report = writeCourseFiles(map, dir.filePath(QStringLiteral("all.kml")), CourseFileKind::Kml, false);
	QVERIFY(report.ok);
	QCOMPARE(report.files, QStringList{ dir.filePath(QStringLiteral("all.kml")) });
	QVERIFY(dir.exists(QStringLiteral("all.kml")));
	
	report = writeCourseFiles(map, dir.filePath(QStringLiteral("all.gpx")), CourseFileKind::Gpx, false);
	QVERIFY(report.ok);
	QVERIFY(dir.exists(QStringLiteral("all.gpx")));
	
	report = writeCourseFiles(map, dir.filePath(QStringLiteral("courses.xml")), CourseFileKind::IofXml, false);
	QVERIFY(report.ok);
	QFile xml_file(dir.filePath(QStringLiteral("courses.xml")));
	QVERIFY(xml_file.open(QIODevice::ReadOnly));
	QCOMPARE(parse(xml_file.readAll()).name, QStringLiteral("CourseData"));
	
	// What cannot be written leaves no file.
	Map local;
	fillInterchangeMap(local, false);
	report = writeCourseFiles(local, dir.filePath(QStringLiteral("local.gpx")), CourseFileKind::Gpx, false);
	QVERIFY(!report.ok);
	QVERIFY(!dir.exists(QStringLiteral("local.gpx")));
	report = writeCourseFiles(map, dir.filePath(QStringLiteral("nowhere/x.gpx")), CourseFileKind::Gpx, false);
	QVERIFY(!report.ok);
}


/// Accept (10)
void CourseInterchangeTest::editorTest()
{
	auto* map = new Map();
	fillInterchangeMap(*map);
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	for (auto const* id : { "course-export-iof", "course-export-gpx", "course-export-kml", "course-import-iof" })
		QVERIFY2(editor->getAction(id), id);
	window->deleteLater();
}



/// With the schema of the IOF at hand, xmllint validates what is written.
void CourseInterchangeTest::schemaTest()
{
	auto const schema = qEnvironmentVariable("FOOM_IOF_XSD");
	if (schema.isEmpty() || !QFileInfo::exists(schema))
		QSKIP("FOOM_IOF_XSD does not name the schema of the IOF.");
	auto const xmllint = QStandardPaths::findExecutable(QStringLiteral("xmllint"));
	if (xmllint.isEmpty())
		QSKIP("xmllint is not installed.");
	
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	for (auto const georeferenced : { true, false })
	{
		Map map;
		fillInterchangeMap(map, georeferenced);
		auto const path = QDir(folder.path()).filePath(georeferenced ? QStringLiteral("geo.xml") : QStringLiteral("local.xml"));
		QVERIFY(writeCourseFiles(map, path, CourseFileKind::IofXml, false).ok);
		QProcess process;
		process.start(xmllint, { QStringLiteral("--noout"), QStringLiteral("--schema"), schema, path });
		QVERIFY(process.waitForFinished(60000));
		QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
	}
}


/*
 * We don't need a real GUI window.
 *
 * But we discovered QTBUG-58768 macOS: Crash when using QPrinter
 * while running with "minimal" platform plugin.
 */
#ifndef Q_OS_MACOS
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "minimal");  // clazy:exclude=non-pod-global-static
}
#endif


QTEST_MAIN(CourseInterchangeTest)
#include "course_interchange_t.moc"  // IWYU pragma: keep
