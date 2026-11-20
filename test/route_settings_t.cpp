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


#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <vector>

#include <Qt>
#include <QtEndian>
#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QIODevice>
#include <QObject>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "test_config.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/route_course.h"
#include "course/route_search.h"
#include "course/route_settings.h"
#include "course/route_terrain.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/map/route_settings_dialog.h"
#include "templates/template.h"
#include "undo/object_undo.h"
#include "undo/undo.h"

using namespace OpenOrienteering;


namespace {

RouteSettings sampleSettings()
{
	RouteSettings settings;
	settings.base_pace = 250;
	settings.use_elevation = false;
	settings.symbols.push_back({ QStringLiteral("406.000"), Runnability{ Runnability::Vegetation, 50 } });
	settings.symbols.push_back({ QStringLiteral("101.000"), Runnability{ Runnability::Hindrance, 90 } });
	return settings;
}

template <class S>
S* addSymbol(Map& map, int number, Symbol::CourseRole role = Symbol::NoCourseRole)
{
	auto* symbol = new S();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

void drawPoint(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

/// Open land of 20 by 10 mm with a start and a finish 160 m apart, and a course of the two.
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* open = addSymbol<AreaSymbol>(map, 401);
	addSymbol<AreaSymbol>(map, 406);
	addSymbol<LineSymbol>(map, 505);
	auto* start = addSymbol<PointSymbol>(map, 701, Symbol::CourseStart);
	auto* finish = addSymbol<PointSymbol>(map, 706, Symbol::CourseFinish);
	auto* area = new PathObject(open);
	area->addCoordinate(MapCoord(0.0, 0.0));
	area->addCoordinate(MapCoord(20.0, 0.0));
	area->addCoordinate(MapCoord(20.0, 10.0));
	area->addCoordinate(MapCoord(0.0, 10.0));
	area->parts().back().setClosed(true);
	map.addObject(area);
	drawPoint(map, start, 2.0, 5.0);
	drawPoint(map, finish, 18.0, 5.0);
	auto data = map.courseData();
	Course course;
	course.name = QStringLiteral("A");
	CourseItem item;
	item.type = CourseItem::Start;
	item.code = QStringLiteral("S1");
	course.items.push_back(item);
	item.type = CourseItem::Finish;
	item.code = QStringLiteral("F1");
	course.items.push_back(item);
	data.courses.push_back(course);
	changeCourseData(map, data);
}

/// Gives the map a terrain model which rises to the east by 1 m in 10 m, over the area of fillMap() and a little more.
void addSlope(Map& map)
{
	auto const& georeferencing = map.getGeoreferencing();
	auto const west = georeferencing.toProjectedCoords(MapCoordF(-1.0, 5.0));
	auto const east = georeferencing.toProjectedCoords(MapCoordF(21.0, 5.0));
	auto const north = georeferencing.toProjectedCoords(MapCoordF(5.0, -1.0));
	auto const south = georeferencing.toProjectedCoords(MapCoordF(5.0, 11.0));
	auto const cell = 2.0;
	auto const width = int(std::ceil(std::abs(east.x() - west.x()) / cell));
	auto const height = int(std::ceil(std::abs(north.y() - south.y()) / cell));
	std::vector<float> values(std::size_t(width) * std::size_t(height));
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
			values[std::size_t(row) * std::size_t(width) + std::size_t(col)] = float((col + 0.5) * cell * 0.1);
	}
	auto model = std::make_unique<ElevationModel>(width, height, QPointF(std::min(west.x(), east.x()), std::max(north.y(), south.y())), cell, values);
	map.elevationSet().setModel(ElevationSet::Terrain, std::move(model), QStringLiteral("slope.tif"));
}

QByteArray exportTo(const Map& map, const char* format_id)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return {};
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool importFrom(const QByteArray& data, const char* format_id, Map& map)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

/// The number of parameter strings of a type in an OCD file of version 9 or later.
int countStrings(const QByteArray& file, qint32 type)
{
	auto count = 0;
	auto const read = [&file](int offset) {
		return offset + 4 <= file.size() ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(file.constData()) + offset) : 0u;
	};
	auto block = int(read(32));
	while (block > 0 && block + 4 + 256 * 16 <= file.size())
	{
		for (int i = 0; i < 256; ++i)
		{
			auto const entry = block + 4 + 16 * i;
			if (read(entry) > 0 && read(entry + 4) > 0 && qint32(read(entry + 8)) == type)
				++count;
		}
		block = int(read(block));
	}
	return count;
}

}  // namespace



/**
 * @test Tests the settings of the route analysis, in the files and in
 * their dialog, and the heights of the elevation model in the search.
 *
 * See the design notes, "Route analysis: the settings and
 * the elevation".
 */
class RouteSettingsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void mapFileTest();
	void ocdTest();
	void heightsTest();
	void searchTest();
	void dialogTest();
};


void RouteSettingsTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RouteSettingsTest::mapFileTest()
{
	QVERIFY(RouteSettings{}.isDefault());
	QVERIFY(!sampleSettings().isDefault());
	QVERIFY(sampleSettings() == sampleSettings());
	QVERIFY(sampleSettings() != RouteSettings{});
	QCOMPARE(runnabilityKindName(Runnability::Hindrance), QStringLiteral("hindrance"));
	auto kind = Runnability::None;
	QVERIFY(runnabilityKindFromName(QStringLiteral("path"), kind));
	QCOMPARE(kind, Runnability::Path);
	QVERIFY(!runnabilityKindFromName(QStringLiteral("swamp"), kind));
	
	CourseData data;
	QVERIFY(data.isEmpty());
	QByteArray bytes;
	{
		QXmlStreamWriter xml(&bytes);
		saveCourseData(data, xml);
	}
	QVERIFY(!bytes.contains("route_settings"));
	
	data.route = sampleSettings();
	QVERIFY(!data.isEmpty());
	QVERIFY(data != CourseData{});
	bytes.clear();
	{
		QXmlStreamWriter xml(&bytes);
		saveCourseData(data, xml);
	}
	QVERIFY(bytes.contains("<route_settings"));
	QXmlStreamReader xml(bytes);
	QVERIFY(xml.readNextStartElement());
	auto const loaded = loadCourseData(xml);
	QVERIFY(!xml.hasError());
	QVERIFY(loaded.route == data.route);
	QCOMPARE(loaded.route.base_pace, 250);
	QVERIFY(!loaded.route.use_elevation);
	QVERIFY(loaded.route.use_runnability);
	QCOMPARE(int(loaded.route.symbols.size()), 2);
	QVERIFY(loaded.route.symbols[1].second == (Runnability{ Runnability::Hindrance, 90 }));
}


/// Accept (2)
void RouteSettingsTest::ocdTest()
{
	RouteSettings settings;
	QVERIFY(routeSettingsFromOcdRecord(QStringLiteral("C:\\somewhere\\event.ocdGraph\tb250\tr0\te1\tc0.50"), settings));
	QCOMPARE(settings.base_pace, 250);
	QVERIFY(!settings.use_runnability);
	QVERIFY(settings.use_elevation);
	QCOMPARE(settings.climb_factor, 0.5);
	QCOMPARE(routeSettingsToOcdRecord(settings), QStringLiteral("\tb250\tr0\te1\tc0.50"));
	QCOMPARE(routeSettingsToOcdRecord(RouteSettings{}), QStringLiteral("\tb200\tr1\te1\tc1.00"));
	auto untouched = settings;
	QVERIFY(!routeSettingsFromOcdRecord(QStringLiteral("graph\tr1\te1"), untouched));
	QVERIFY(untouched == settings);
	
	Map map;
	map.setScaleDenominator(10000);
	auto data = map.courseData();
	Course course;
	course.name = QStringLiteral("A");
	data.courses.push_back(course);
	data.route = settings;
	map.setCourseData(data);
	auto const written = exportTo(map, "OCD2018");
	QVERIFY(!written.isEmpty());
	QCOMPARE(countStrings(written, 1088), 1);
	Map reloaded;
	QVERIFY(importFrom(written, "OCD", reloaded));
	QVERIFY(reloaded.courseData().route == settings);
	// Written again there is still one.
	QCOMPARE(countStrings(exportTo(reloaded, "OCD2018"), 1088), 1);
	
	// With the default settings none is written.
	data.route = RouteSettings{};
	map.setCourseData(data);
	QCOMPARE(countStrings(exportTo(map, "OCD2018"), 1088), 0);
}


/// Accept (3)
void RouteSettingsTest::heightsTest()
{
	Map map;
	fillMap(map);
	QVERIFY(!routeHeightsOf(map));
	
	addSlope(map);
	auto const heights = routeHeightsOf(map);
	QVERIFY(bool(heights));
	auto const* model = map.elevationSet().loadedModel(ElevationSet::Terrain);
	QVERIFY(model);
	for (auto const& place : { MapCoordF(2.0, 5.0), MapCoordF(10.0, 2.0), MapCoordF(18.0, 8.0) })
	{
		auto const expected = model->at(map.getGeoreferencing().toProjectedCoords(place));
		QVERIFY(!std::isnan(expected));
		QCOMPARE(heights(place), expected);
	}
	// 1 m in 10 m to the east: 16 m over the 160 m of the course.
	QVERIFY(std::abs((heights(MapCoordF(18.0, 5.0)) - heights(MapCoordF(2.0, 5.0))) - 16.0) < 0.3);
	QVERIFY(std::isnan(heights(MapCoordF(100.0, 5.0))));
}


/// Accept (4)
void RouteSettingsTest::searchTest()
{
	Map map;
	fillMap(map);
	addSlope(map);
	auto const flat = 160.0 * 0.2 / 0.86;
	
	auto settings = map.courseData().route;
	auto terrain = buildRouteTerrain(map, settings, QRectF(0.0, 0.0, 20.0, 10.0));
	{
		RouteSearch search(terrain, settings, map.getScaleDenominator());
		search.setHeights(routeHeightsOf(map));
		QVERIFY(search.usesHeights());
		auto const routes = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search);
		QVERIFY(routes.analysed);
		QVERIFY(routes.with_heights);
		QVERIFY2(std::abs(routes.time_s / flat - slopeFactor(0.1)) < 0.03, qPrintable(QString::number(routes.time_s / flat)));
		QVERIFY2(std::abs(routes.climb_m - 16.0) < 1.0, qPrintable(QString::number(routes.climb_m)));
		auto const text = courseRoutesText(routes);
		QVERIFY(text.startsWith(QLatin1String("Course A\tFastest route (leg)\tFastest route (total)\tClimb along route\tTime along route\n")));
		QVERIFY2(text.contains(QStringLiteral("\t%1 m \t%2\n").arg(qRound(routes.climb_m)).arg(formatRouteTime(routes.time_s))), qPrintable(text));
		QVERIFY(text.contains(QStringLiteral("Climb along course = %1 m\nTime along course = ").arg(qRound(routes.climb_m))));
	}
	
	// Without the elevation in the settings.
	settings.use_elevation = false;
	{
		RouteSearch search(terrain, settings, map.getScaleDenominator());
		search.setHeights(routeHeightsOf(map));
		QVERIFY(!search.usesHeights());
		auto const routes = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search);
		QVERIFY(!routes.with_heights);
		QVERIFY2(std::abs(routes.time_s - flat) < 0.8, qPrintable(QString::number(routes.time_s)));
		QVERIFY(!courseRoutesText(routes).contains(QLatin1String("Climb")));
	}
	
	// The base pace of the settings.
	settings.base_pace = 400;
	{
		RouteSearch search(terrain, settings, map.getScaleDenominator());
		auto const routes = analyseCourseRoutes(map, map.courseData(), QStringLiteral("A"), -1, search);
		QVERIFY2(std::abs(routes.time_s - 2 * flat) < 1.6, qPrintable(QString::number(routes.time_s)));
	}
}


/// Accept (5)
void RouteSettingsTest::dialogTest()
{
	Map map;
	fillMap(map);
	auto data = map.courseData();
	data.route.base_pace = 300;
	data.route.use_elevation = false;
	data.route.symbols.push_back({ QStringLiteral("406.000"), Runnability{ Runnability::Vegetation, 50 } });
	data.route.symbols.push_back({ QStringLiteral("999.000"), Runnability{ Runnability::Path, 70 } });
	changeCourseData(map, data);
	
	RouteSettingsDialog dialog(&map, false);
	auto* pace = dialog.findChild<QSpinBox*>(QStringLiteral("route-pace"));
	auto* use_runnability = dialog.findChild<QCheckBox*>(QStringLiteral("route-use-runnability"));
	auto* use_elevation = dialog.findChild<QCheckBox*>(QStringLiteral("route-use-elevation"));
	auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("route-symbols"));
	auto* defaults = dialog.findChild<QPushButton*>(QStringLiteral("route-defaults"));
	QVERIFY(pace && use_runnability && use_elevation && table && defaults);
	QCOMPARE(pace->value(), 300);
	QCOMPARE(pace->minimum(), 30);
	QCOMPARE(pace->maximum(), 1200);
	QVERIFY(use_runnability->isChecked());
	// Without a model the switch is disabled and keeps its value.
	QVERIFY(!use_elevation->isEnabled());
	QVERIFY(!use_elevation->isChecked());
	// The three symbols which are lines or areas; the point symbols are not in the table.
	QCOMPARE(table->rowCount(), 3);
	QVERIFY(table->isEnabled());
	// What the dialog has is what the map has.
	QVERIFY(dialog.settings() == data.route);
	
	pace->setValue(180);
	use_runnability->setChecked(false);
	QVERIFY(!table->isEnabled());
	use_runnability->setChecked(true);
	QVERIFY(dialog.setRunnability(QStringLiteral("505.000"), Runnability{ Runnability::Path, 80 }));
	QVERIFY(!dialog.setRunnability(QStringLiteral("123.000"), Runnability{}));
	auto settings = dialog.settings();
	QCOMPARE(settings.base_pace, 180);
	QVERIFY(!settings.use_elevation);
	QCOMPARE(int(settings.symbols.size()), 3);
	QCOMPARE(settings.symbols[0].first, QStringLiteral("406.000"));
	QCOMPARE(settings.symbols[1].first, QStringLiteral("505.000"));
	QVERIFY(settings.symbols[1].second == (Runnability{ Runnability::Path, 80 }));
	QCOMPARE(settings.symbols[2].first, QStringLiteral("999.000"));
	
	// A row which is put back to its default is no setting.
	QVERIFY(dialog.setRunnability(QStringLiteral("406.000"), Runnability{ Runnability::Vegetation, 65 }));
	QCOMPARE(int(dialog.settings().symbols.size()), 2);
	// A symbol which does not count has no percentage to differ in.
	QVERIFY(dialog.setRunnability(QStringLiteral("401.000"), Runnability{ Runnability::None, 12 }));
	settings = dialog.settings();
	QCOMPARE(int(settings.symbols.size()), 3);
	QCOMPARE(settings.symbols[0].first, QStringLiteral("401.000"));
	QCOMPARE(settings.symbols[0].second.kind, Runnability::None);
	
	defaults->click();
	settings = dialog.settings();
	QCOMPARE(int(settings.symbols.size()), 1);
	QCOMPARE(settings.symbols[0].first, QStringLiteral("999.000"));
	
	// With a model the switch can be used.
	RouteSettingsDialog with_model(&map, true);
	auto* enabled = with_model.findChild<QCheckBox*>(QStringLiteral("route-use-elevation"));
	QVERIFY(enabled->isEnabled());
	enabled->setChecked(true);
	QVERIFY(with_model.settings().use_elevation);
}



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RouteSettingsTest)
#include "route_settings_t.moc"  // IWYU pragma: keep
