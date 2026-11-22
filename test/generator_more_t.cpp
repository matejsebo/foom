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
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QChar>
#include <QColor>  // foom
#include <QImage>  // foom
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QObject>
#include <QPainter>  // foom
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"  // foom
#include "core/symbols/area_symbol.h"  // foom
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/virtual_path.h"
#include "layout/generator.h"
#include "layout/layout_part.h"
#include "layout/qr_code.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const QString north_arrow = QStringLiteral("gen-north-arrow");
const QString grid = QStringLiteral("gen-grid");
const QString graticule = QStringLiteral("gen-graticule");
const QString name_index = QStringLiteral("gen-name-index");
const QString trim_marks = QStringLiteral("gen-trim-marks");
const QString qr_code = QStringLiteral("gen-qr-code");

MapColor* addBlack(Map& map)
{
	auto* color = new MapColor(QStringLiteral("Black"), map.getNumColors());
	color->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

template <class S>
S* addSymbol(Map& map, const QString& name, int number, const MapColor* color)
{
	auto* symbol = new S();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	symbol->setColor(color);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

/// A 1:10,000 map in UTM zone 13 with its reference point at the position.
void setUtm(Map& map, double latitude, double longitude, double declination)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setGeographicRefPoint(LatLon(latitude, longitude));
	georef.setDeclination(declination);
	map.setGeoreferencing(georef);
}

/// Adds a closed rectangle and selects it.
PathObject* addSelectedRectangle(Map& map, const Symbol* symbol, const QRectF& box)
{
	MapCoordVector coords { MapCoord(box.topLeft()), MapCoord(box.topRight()), MapCoord(box.bottomRight()), MapCoord(box.bottomLeft()), MapCoord(box.topLeft()) };
	coords.back().setClosePoint(true);
	auto* outline = new PathObject(symbol, coords);
	map.addObject(outline);
	map.clearObjectSelection(false);
	map.addObjectToSelection(outline, false);
	return outline;
}

TextObject* addText(Map& map, const Symbol* symbol, const QString& text, const QPointF& anchor, int part = -1)
{
	auto* object = new TextObject(symbol);
	object->setAnchorPosition(MapCoordF(anchor));
	object->setText(text);
	map.addObject(object, part);
	return object;
}

QPointF point(const Object* object, std::size_t index)
{
	return QPointF(MapCoordF(object->getRawCoordinateVector()[MapCoordVector::size_type(index)]));
}

bool near(const QPointF& a, const QPointF& b, double tolerance = 0.002)
{
	return qAbs(a.x() - b.x()) <= tolerance && qAbs(a.y() - b.y()) <= tolerance;
}

std::vector<const PathObject*> paths(const std::vector<Object*>& objects)
{
	std::vector<const PathObject*> result;
	for (auto const* object : objects)
	{
		if (object->getType() == Object::Path)
			result.push_back(object->asPath());
	}
	return result;
}

std::vector<const TextObject*> texts(const std::vector<Object*>& objects)
{
	std::vector<const TextObject*> result;
	for (auto const* object : objects)
	{
		if (object->getType() == Object::Text)
			result.push_back(object->asText());
	}
	return result;
}

QStringList strings(const std::vector<Object*>& objects)
{
	QStringList result;
	for (auto const* text : texts(objects))
		result.append(text->getText());
	return result;
}

/// The text object of the group which is anchored at the position.
const TextObject* textAt(const std::vector<Object*>& objects, const QPointF& anchor)
{
	for (auto const* text : texts(objects))
	{
		if (near(QPointF(text->getAnchorCoordF()), anchor))
			return text;
	}
	return nullptr;
}

/// Runs a generator and returns the objects of its group.
std::vector<Object*> run(Map& map, const QString& id, const QJsonObject& params, const QPointF& anchor = {}, int* part = nullptr, int* group_out = nullptr)
{
	QString error;
	auto const group = runGenerator(map, id, params, MapCoordF(anchor), &error);
	if (group_out)
		*group_out = group;
	if (group == 0)
	{
		qWarning("%s", qPrintable(error));
		return {};
	}
	return groupObjects(map, group, part);
}

QString angle(int degrees, int minutes, QChar hemisphere)
{
	QString result = QString::number(degrees);
	result += QChar(0x00B0);
	result += QString::number(minutes).rightJustified(2, QLatin1Char('0'));
	result += QChar(0x2032);
	result += hemisphere;
	return result;
}

QString angle(int degrees, int minutes, int seconds, QChar hemisphere)
{
	QString result = QString::number(degrees);
	result += QChar(0x00B0);
	result += QString::number(minutes).rightJustified(2, QLatin1Char('0'));
	result += QChar(0x2032);
	result += QString::number(seconds).rightJustified(2, QLatin1Char('0'));
	result += QChar(0x2033);
	result += hemisphere;
	return result;
}

}  // namespace


/**
 * @test Tests the north arrow, grid, graticule, name index, trim marks and
 *       QR code generators.
 *
 * the design notes, "Generators", second change.
 */
class GeneratorMoreTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	void idsTest()
	{
		auto const ids = generatorIds();
		for (auto const& id : { north_arrow, grid, graticule, name_index, trim_marks, qr_code })
		{
			QVERIFY2(ids.contains(id), qPrintable(id));
			Map map;
			QVERIFY(!generatorFields(id, &map).empty());
			QVERIFY(!defaultGeneratorParams(id, map).isEmpty());
			QVERIFY(generatorTitle(id) != id);
		}
	}
	
	/// Accept (1)
	void northArrowTest()
	{
		Map map;
		setUtm(map, 47.0, -107.0, 5.0);
		auto const& georef = map.getGeoreferencing();
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		QVERIFY(qAbs(georef.getDeclination() - georef.getGrivation()) > 0.5);
		
		auto params = defaultGeneratorParams(north_arrow, map);
		auto part = -1;
		auto objects = run(map, north_arrow, params, { 20, 30 }, &part);
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(part, layoutPartIndex(map));
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		auto const* arrow = objects.front();
		QCOMPARE(arrow->getType(), Object::Point);
		QVERIFY(near(point(arrow, 0), { 20, 30 }));
		QVERIFY(arrow->getSymbol()->isPrivate());
		QCOMPARE(arrow->getSymbol()->getType(), Symbol::Point);
		QVERIFY(arrow->getSymbol()->isRotatable());
		QCOMPARE(arrow->asPoint()->getRotation(), 0.0);
		arrow->update();
		QVERIFY2(qAbs(arrow->getExtent().height() - 15.0) < 0.3, qPrintable(QString::number(arrow->getExtent().height())));
		
		params.insert(QStringLiteral("reference"), QStringLiteral("grid"));
		objects = run(map, north_arrow, params, { 40, 30 });
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(objects.front()->asPoint()->getRotation(), qDegreesToRadians(georef.getGrivation()));
		
		params.insert(QStringLiteral("reference"), QStringLiteral("true"));
		auto group = 0;
		objects = run(map, north_arrow, params, { 60, 30 }, nullptr, &group);
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(objects.front()->asPoint()->getRotation(), qDegreesToRadians(5.0));
		
		// The arrow follows the georeferencing when it is rebuilt.
		Georeferencing changed(map.getGeoreferencing());
		changed.setDeclination(8.0);
		map.setGeoreferencing(changed);
		QString error;
		QCOMPARE(regenerateGroups(map, { group }, &error), 1);
		objects = groupObjects(map, group);
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(objects.front()->asPoint()->getRotation(), qDegreesToRadians(8.0));
		QVERIFY(near(point(objects.front(), 0), { 60, 30 }));
	}
	
	/// Accept (2)
	void northArrowDesignsTest()
	{
		for (auto const& design : { QStringLiteral("arrow"), QStringLiteral("needle"), QStringLiteral("barb") })
		{
			Map map;
			map.setScaleDenominator(10000);
			auto params = defaultGeneratorParams(north_arrow, map);
			params.insert(QStringLiteral("design"), design);
			auto objects = run(map, north_arrow, params);
			QCOMPARE(objects.size(), std::size_t(1));
			auto const with_letter = objects.front()->getSymbol()->asPoint()->getNumElements();
			QVERIFY2(with_letter >= 2, qPrintable(design));
			
			params.insert(QStringLiteral("letter"), false);
			objects = run(map, north_arrow, params);
			QCOMPARE(objects.size(), std::size_t(1));
			QCOMPARE(objects.front()->getSymbol()->asPoint()->getNumElements(), with_letter - 1);
		}
	}
	
	/// Accept (3)
	void gridSpacingTest()
	{
		QCOMPARE(gridAutoSpacing(10000), 500.0);
		QCOMPARE(gridAutoSpacing(15000), 1000.0);
		QCOMPARE(gridAutoSpacing(4000), 200.0);
	}
	
	/// Accept (4), (5)
	void gridTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Outline"), 1, addBlack(map));
		auto* outline = addSelectedRectangle(map, line, { 5, -65, 100, 60 });
		
		auto params = defaultGeneratorParams(grid, map);
		params.insert(QStringLiteral("spacing_e"), 300);
		params.insert(QStringLiteral("spacing_n"), 200);
		params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
		auto part = -1;
		auto group = 0;
		auto objects = run(map, grid, params, {}, &part, &group);
		QCOMPARE(part, 0);
		QCOMPARE(map.getNumParts(), 1);
		auto lines = paths(objects);
		QCOMPARE(lines.size(), std::size_t(6));
		for (std::size_t i = 0; i < 3; ++i)
		{
			auto const x = 30.0 * (i + 1);
			QVERIFY(near(point(lines[i], 0), { x, -5 }));
			QVERIFY(near(point(lines[i], 1), { x, -65 }));
			auto const y = -20.0 * (i + 1);
			QVERIFY(near(point(lines[i + 3], 0), { 5, y }));
			QVERIFY(near(point(lines[i + 3], 1), { 105, y }));
		}
		QCOMPARE(texts(objects).size(), std::size_t(12));
		auto const* below = textAt(objects, { 30, -4 });
		QVERIFY(below);
		QCOMPARE(below->getText(), QStringLiteral("300"));
		QCOMPARE(below->getVerticalAlignment(), TextObject::AlignTop);
		QCOMPARE(below->getHorizontalAlignment(), TextObject::AlignHCenter);
		auto const* left = textAt(objects, { 4, -40 });
		QVERIFY(left);
		QCOMPARE(left->getText(), QStringLiteral("400"));
		QCOMPARE(left->getHorizontalAlignment(), TextObject::AlignRight);
		QVERIFY(textAt(objects, { 30, -66 }));
		QVERIFY(textAt(objects, { 106, -40 }));
		
		// The outline is stored: a rebuild does not need the selection.
		map.clearObjectSelection(false);
		QString error;
		QCOMPARE(regenerateGroups(map, { group }, &error), 1);
		QCOMPARE(groupObjects(map, group).size(), std::size_t(18));
		
		map.addObjectToSelection(outline, false);
		
		params.insert(QStringLiteral("offset_e"), 100);
		lines = paths(run(map, grid, params));
		QCOMPARE(lines.size(), std::size_t(7));
		QVERIFY(near(point(lines[0], 0), { 10, -5 }));
		params.insert(QStringLiteral("offset_e"), 0);
		
		params.insert(QStringLiteral("horizontal"), false);
		params.insert(QStringLiteral("labels"), false);
		objects = run(map, grid, params);
		QCOMPARE(objects.size(), std::size_t(3));
		QVERIFY(texts(objects).empty());
		for (auto const* vertical : paths(objects))
			QCOMPARE(point(vertical, 0).x(), point(vertical, 1).x());
		
		params.insert(QStringLiteral("junction_vertices"), true);
		lines = paths(run(map, grid, params));
		QCOMPARE(lines.size(), std::size_t(3));
		QCOMPARE(lines[0]->getCoordinateCount(), MapCoordVector::size_type(5));
		QVERIFY(near(point(lines[0], 1), { 30, -20 }));
		QVERIFY(near(point(lines[0], 3), { 30, -60 }));
	}
	
	/// Accept (6)
	void gridGrivationTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		Georeferencing turned(map.getGeoreferencing());
		turned.setGrivation(10.0);
		map.setGeoreferencing(turned);
		auto const& georef = map.getGeoreferencing();
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Outline"), 1, addBlack(map));
		addSelectedRectangle(map, line, { 5, -65, 100, 60 });
		
		auto params = defaultGeneratorParams(grid, map);
		params.insert(QStringLiteral("spacing_e"), 300);
		params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
		params.insert(QStringLiteral("horizontal"), false);
		params.insert(QStringLiteral("labels"), false);
		auto const lines = paths(run(map, grid, params));
		QVERIFY(lines.size() >= 2);
		
		auto north = QPointF(georef.toMapCoordF(QPointF(0, 1000))) - QPointF(georef.toMapCoordF(QPointF(0, 0)));
		north /= std::hypot(north.x(), north.y());
		QVERIFY(qAbs(north.x()) > 0.1);
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			auto const direction = point(lines[i], 1) - point(lines[i], 0);
			QVERIFY(qAbs(direction.x() * north.y() - direction.y() * north.x()) / std::hypot(direction.x(), direction.y()) < 1e-3);
			if (i > 0)
			{
				auto const offset = point(lines[i], 0) - point(lines[i - 1], 0);
				QVERIFY(qAbs(qAbs(offset.x() * north.y() - offset.y() * north.x()) - 30.0) < 0.01);
			}
		}
	}
	
	/// Accept (7)
	void graticuleIntervalTest()
	{
		QCOMPARE(graticuleAutoInterval(10000), 15.0);
		QCOMPARE(graticuleAutoInterval(50000), 120.0);
	}
	
	/// Accept (8), (9)
	void graticuleTest()
	{
		Map map;
		setUtm(map, 47.0, -105.0, 0.0);
		auto const& georef = map.getGeoreferencing();
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Outline"), 1, addBlack(map));
		addSelectedRectangle(map, line, { -100, -100, 200, 200 });
		
		auto params = defaultGeneratorParams(graticule, map);
		params.insert(QStringLiteral("interval_min"), 0.5);
		params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
		auto part = -1;
		auto objects = run(map, graticule, params, {}, &part);
		QCOMPARE(part, 0);
		auto lines = paths(objects);
		QCOMPARE(lines.size(), std::size_t(6));
		
		// The meridians come first, from the west; then the parallels, from the south.
		auto const last = [](const PathObject* path) {
			return point(path, path->getCoordinateCount() - 1);
		};
		QVERIFY(near(point(lines[1], 0), { 0, 100 }, 0.01));
		QVERIFY(near(last(lines[1]), { 0, -100 }, 0.01));
		auto const y = georef.toMapCoordF(LatLon(47.0 + 30.0 / 3600.0, -105.0)).y();
		QVERIFY(y < -80 && y > -100);
		QVERIFY(near(point(lines[5], 0), { -100, y }, 0.05));
		QVERIFY(near(last(lines[5]), { 100, y }, 0.05));
		
		auto labels = strings(objects);
		QCOMPARE(labels.size(), 12);
		QCOMPARE(labels.count(angle(47, 0, 30, QLatin1Char('N'))), 2);
		QCOMPARE(labels.count(angle(46, 59, 30, QLatin1Char('N'))), 2);
		QCOMPARE(labels.count(angle(105, 0, 30, QLatin1Char('W'))), 2);
		QCOMPARE(labels.count(angle(105, 0, 0, QLatin1Char('W'))), 2);
		// 1 mm outside the frame, below the lower end of the middle meridian.
		auto const* below = textAt(objects, { 0, 101 });
		QVERIFY(below);
		QCOMPARE(below->getText(), angle(105, 0, 0, QLatin1Char('W')));
		QCOMPARE(below->getVerticalAlignment(), TextObject::AlignTop);
		
		params.insert(QStringLiteral("interval_min"), 1);
		objects = run(map, graticule, params);
		QCOMPARE(paths(objects).size(), std::size_t(2));
		labels = strings(objects);
		QCOMPARE(labels.size(), 4);
		QCOMPARE(labels.count(angle(47, 0, QLatin1Char('N'))), 2);
		QCOMPARE(labels.count(angle(105, 0, QLatin1Char('W'))), 2);
		
		// Typed bounds inside the frame: the lines end there and have no labels.
		params.insert(QStringLiteral("interval_min"), 0.5);
		params.insert(QStringLiteral("lat_min"), 46.999);
		params.insert(QStringLiteral("lat_max"), 47.001);
		params.insert(QStringLiteral("lon_min"), -105.01);
		params.insert(QStringLiteral("lon_max"), -104.99);
		objects = run(map, graticule, params);
		lines = paths(objects);
		QCOMPARE(lines.size(), std::size_t(4));
		QVERIFY(texts(objects).empty());
		auto const south = QPointF(georef.toMapCoordF(LatLon(46.999, -105.0)));
		QVERIFY(near(point(lines[1], 0), south, 0.01));
		
		Map local;
		local.setScaleDenominator(10000);
		addSelectedRectangle(local, addSymbol<LineSymbol>(local, QStringLiteral("Outline"), 1, addBlack(local)), { -100, -100, 200, 200 });
		QString error;
		QCOMPARE(runGenerator(local, graticule, defaultGeneratorParams(graticule, local), {}, &error), 0);
		QVERIFY(!error.isEmpty());
	}
	
	/// Accept (10), (11)
	void nameIndexTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto const* black = addBlack(map);
		auto* names = addSymbol<TextSymbol>(map, QStringLiteral("Names"), 1, black);
		auto* other = addSymbol<TextSymbol>(map, QStringLiteral("Other"), 2, black);
		addText(map, names, QStringLiteral("Birch"), { 60, 60 });
		addText(map, names, QStringLiteral("Birch"), { 70, 80 });
		addText(map, names, QStringLiteral("Birch"), { 120, 170 });
		addText(map, names, QStringLiteral("Alder\nbank"), { 10, 10 });
		addText(map, names, QStringLiteral("Yard 10"), { 10, 60 });
		addText(map, names, QStringLiteral("Yard 9"), { 60, 10 });
		addText(map, names, QStringLiteral("Thorn"), { 1310, 10 });
		addText(map, names, QStringLiteral("Left"), { -10, 10 });
		addText(map, other, QStringLiteral("Skip"), { 10, 10 });
		auto const layout = addLayoutPart(map);
		addText(map, names, QStringLiteral("Title"), { 10, 10 }, layout);
		map.undoManager().clear();
		
		auto params = defaultGeneratorParams(name_index, map);
		params.insert(QStringLiteral("origin_x"), 0);
		params.insert(QStringLiteral("origin_y"), 0);
		params.insert(QStringLiteral("symbols"), QStringLiteral("1"));
		auto part = -1;
		auto objects = run(map, name_index, params, { 200, 0 }, &part);
		QCOMPARE(part, layout);
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(objects.front()->getType(), Object::Text);
		QVERIFY(near(QPointF(objects.front()->asText()->getAnchorCoordF()), { 200, 0 }));
		QCOMPARE(objects.front()->asText()->getText(),
		         QStringLiteral("Alder bank\tA1\nBirch\tB2, C4\nThorn\tAA1\nYard 9\tB1\nYard 10\tA2"));
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		
		// Every text symbol.
		params.insert(QStringLiteral("symbols"), QString{});
		objects = run(map, name_index, params, { 200, 0 });
		QCOMPARE(objects.size(), std::size_t(1));
		QVERIFY(objects.front()->asText()->getText().contains(QLatin1String("Skip\tA1")));
		QVERIFY(!objects.front()->asText()->getText().contains(QLatin1String("Title")));
		QVERIFY(!objects.front()->asText()->getText().contains(QLatin1String("Left")));
		
		// Columns.
		params.insert(QStringLiteral("symbols"), QStringLiteral("1"));
		params.insert(QStringLiteral("rows_per_column"), 2);
		objects = run(map, name_index, params, { 200, 0 });
		QCOMPARE(objects.size(), std::size_t(3));
		for (std::size_t i = 0; i < 3; ++i)
			QVERIFY(near(QPointF(objects[i]->asText()->getAnchorCoordF()), { 200.0 + 60.0 * i, 0 }));
		QCOMPARE(objects[2]->asText()->getText(), QStringLiteral("Yard 10\tA2"));
		auto const* automatic = objects.front()->getSymbol()->asText();
		QVERIFY(automatic->isPrivate());
		QCOMPARE(automatic->getNumCustomTabs(), 1);
		QCOMPARE(automatic->getCustomTab(0), 40000);
		
		// A text symbol of the map is used as it is.
		params.insert(QStringLiteral("text_symbol"), QStringLiteral("2"));
		objects = run(map, name_index, params, { 200, 0 });
		QCOMPARE(objects.front()->getSymbol(), static_cast<const Symbol*>(other));
	}
	
	/// Accept (12)
	void nameIndexGraticuleTest()
	{
		Map map;
		setUtm(map, 47.0, -105.0, 0.0);
		auto* names = addSymbol<TextSymbol>(map, QStringLiteral("Names"), 1, addBlack(map));
		auto const& georef = map.getGeoreferencing();
		addText(map, names, QStringLiteral("Farm"), QPointF(georef.toMapCoordF(LatLon(47.0 + 0.5 / 60, -105.0 + 0.5 / 60))));
		
		auto params = defaultGeneratorParams(name_index, map);
		params.insert(QStringLiteral("cells"), QStringLiteral("graticule"));
		auto const objects = run(map, name_index, params);
		QCOMPARE(objects.size(), std::size_t(1));
		QString expected = QStringLiteral("Farm\t");
		expected += angle(47, 0, QLatin1Char('N'));
		expected += QLatin1Char(' ');
		expected += angle(105, 0, QLatin1Char('W'));
		QCOMPARE(objects.front()->asText()->getText(), expected);
		
		Map local;
		local.setScaleDenominator(10000);
		addText(local, addSymbol<TextSymbol>(local, QStringLiteral("Names"), 1, addBlack(local)), QStringLiteral("Farm"), { 10, 10 });
		QString error;
		QCOMPARE(runGenerator(local, name_index, params, {}, &error), 0);
		QVERIFY(!error.isEmpty());
	}
	
	/// Accept (13)
	void trimMarksTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		Extent sheet;
		sheet.name = QStringLiteral("sheet");
		sheet.rect_mm = QRectF(10, 20, 148, 105);
		map.setExtents({ sheet });
		
		auto params = defaultGeneratorParams(trim_marks, map);
		params.insert(QStringLiteral("extent"), QStringLiteral("sheet"));
		auto part = -1;
		auto objects = run(map, trim_marks, params, {}, &part);
		QCOMPARE(part, layoutPartIndex(map));
		QCOMPARE(objects.size(), std::size_t(9));
		auto lines = paths(objects);
		QCOMPARE(lines.size(), std::size_t(9));
		QVERIFY(near(point(lines[0], 0), { 2, 20 }));
		QVERIFY(near(point(lines[0], 1), { 7, 20 }));
		QVERIFY(near(point(lines[1], 0), { 10, 12 }));
		QVERIFY(near(point(lines[1], 1), { 10, 17 }));
		// The bottom right corner.
		QVERIFY(near(point(lines[4], 0), { 166, 125 }));
		QVERIFY(near(point(lines[4], 1), { 161, 125 }));
		lines[8]->update();
		QVERIFY(lines[8]->parts().front().isClosed());
		QVERIFY(near(point(lines[8], 0), { 7, 17 }));
		QVERIFY(near(point(lines[8], 2), { 161, 128 }));
		
		params.insert(QStringLiteral("bleed"), 0);
		lines = paths(run(map, trim_marks, params));
		QCOMPARE(lines.size(), std::size_t(8));
		QVERIFY(near(point(lines[0], 0), { 3, 20 }));
		QVERIFY(near(point(lines[0], 1), { 8, 20 }));
		
		QString error;
		params.insert(QStringLiteral("extent"), QStringLiteral("nothing"));
		QCOMPARE(runGenerator(map, trim_marks, params, {}, &error), 0);
		QVERIFY(!error.isEmpty());
		
		error.clear();
		params.insert(QStringLiteral("extent"), QString{});
		QVERIFY(!map.hasPrinterConfig());
		QCOMPARE(runGenerator(map, trim_marks, params, {}, &error), 0);
		QVERIFY(!error.isEmpty());
		
		MapPrinterConfig config(map);
		config.print_area = QRectF(0, 0, 100, 50);
		map.setPrinterConfig(config);
		lines = paths(run(map, trim_marks, params));
		QCOMPARE(lines.size(), std::size_t(8));
		QVERIFY(near(point(lines[0], 0), { -7, 0 }));
		QVERIFY(near(point(lines[0], 1), { -2, 0 }));
		
		// The choice of the dialog: the print area and the extents.
		for (auto const& field : generatorFields(trim_marks, &map))
		{
			if (field.key == QLatin1String("extent"))
				QCOMPARE(field.choices, (QStringList{ QString{}, QStringLiteral("sheet") }));
		}
	}
	
	/// Accept (18)
	void qrCodeTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto const text = QStringLiteral("https://example.org/map?id=12345");
		auto params = defaultGeneratorParams(qr_code, map);
		QCOMPARE(params.value(QLatin1String("quiet_zone")).toInt(), 4);
		QString error;
		QCOMPARE(runGenerator(map, qr_code, params, {}, &error), 0);
		QVERIFY(!error.isEmpty());
		
		params.insert(QStringLiteral("text"), text);
		params.insert(QStringLiteral("module"), 0.5);
		params.insert(QStringLiteral("background"), false);  // foom: third change
		auto part = -1;
		auto const objects = run(map, qr_code, params, { 10, 10 }, &part);
		QCOMPARE(part, layoutPartIndex(map));
		QCOMPARE(objects.size(), std::size_t(1));
		QCOMPARE(objects.front()->getType(), Object::Path);
		QCOMPARE(objects.front()->getSymbol()->getType(), Symbol::Area);
		
		auto const code = encodeQr(text.toUtf8(), QrEcLevel::M);
		QVERIFY(code.isValid());
		auto dark = 0;
		for (auto module : code.modules)
			dark += module;
		
		auto const* path = objects.front()->asPath();
		path->update();
		auto const extent = path->getExtent();
		auto const side = code.size * 0.5;
		QVERIFY(near(extent.topLeft(), { 12, 12 }, 0.01));
		QVERIFY(near(extent.bottomRight(), { 12 + side, 12 + side }, 0.01));
		QVERIFY(int(path->parts().size()) < dark);
		QVERIFY(path->parts().size() > 10);
		auto area = 0.0;
		for (auto const& part_of_path : path->parts())
		{
			QVERIFY(part_of_path.isClosed());
			auto twice = 0.0;
			for (auto i = part_of_path.first_index; i < part_of_path.last_index; ++i)
			{
				auto const a = point(path, i);
				auto const b = point(path, i + 1);
				twice += a.x() * b.y() - a.y() * b.x();
			}
			area += qAbs(twice) / 2;
		}
		QVERIFY2(qAbs(area - dark * 0.25) < 1e-6, qPrintable(QString::number(area)));
	}
	
	/// Third change, Accept (6)
	void qrBackgroundTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* black = addBlack(map);
		auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
		yellow->setCmyk({ 0.0f, 0.0f, 1.0f, 0.0f });
		yellow->setRgbFromCmyk();
		map.addColor(yellow, 1);
		
		auto params = defaultGeneratorParams(qr_code, map);
		QCOMPARE(params.value(QLatin1String("background")).toBool(), true);
		params.insert(QStringLiteral("text"), QStringLiteral("FOOM"));
		auto const objects = run(map, qr_code, params, { 10, 10 });
		QCOMPARE(objects.size(), std::size_t(2));
		QCOMPARE(map.getNumColors(), 3);
		QCOMPARE(map.getColor(0), black);
		QCOMPARE(map.getColor(2), yellow);
		auto const* white = map.getColor(1);
		QCOMPARE(static_cast<const QColor&>(*white).rgb(), QColor(Qt::white).rgb());
		
		auto const* background = objects.front()->asPath();
		QVERIFY(background->getSymbol()->isPrivate());
		QCOMPARE(background->getSymbol()->getType(), Symbol::Area);
		QVERIFY(background->getSymbol()->containsColor(white));
		QVERIFY(!background->getSymbol()->containsColor(black));
		QCOMPARE(background->parts().size(), std::size_t(1));
		QVERIFY(background->parts().front().isClosed());
		background->update();
		auto const side = (21 + 8) * 0.5;
		QVERIFY(near(background->getExtent().topLeft(), { 10, 10 }, 0.01));
		QVERIFY(near(background->getExtent().bottomRight(), { 10 + side, 10 + side }, 0.01));
		QVERIFY(objects.back()->getSymbol()->containsColor(black));
		
		// Once more: no colour is inserted.
		QCOMPARE(run(map, qr_code, params, { 50, 10 }).size(), std::size_t(2));
		QCOMPARE(map.getNumColors(), 3);
		
		// A white colour below black already.
		Map other;
		other.setScaleDenominator(10000);
		addBlack(other);
		auto* paper = new MapColor(QStringLiteral("Paper"), 1);
		paper->setCmyk({ 0.0f, 0.0f, 0.0f, 0.0f });
		paper->setRgbFromCmyk();
		other.addColor(paper, 1);
		QCOMPARE(run(other, qr_code, params, { 10, 10 }).size(), std::size_t(2));
		QCOMPARE(other.getNumColors(), 2);
	}
	
	/// Third change, Accept (7)
	void qrBackgroundDrawnTest()
	{
		auto const draw = [](bool with_background) {
			Map map;
			map.setScaleDenominator(10000);
			addBlack(map);
			auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
			yellow->setCmyk({ 0.0f, 0.0f, 1.0f, 0.0f });
			yellow->setRgbFromCmyk();
			map.addColor(yellow, 1);
			auto* land = addSymbol<AreaSymbol>(map, QStringLiteral("Land"), 1, yellow);
			MapCoordVector coords { MapCoord(-10, -10), MapCoord(40, -10), MapCoord(40, 40), MapCoord(-10, 40), MapCoord(-10, -10) };
			coords.back().setClosePoint(true);
			map.addObject(new PathObject(land, coords), 0);
			auto params = defaultGeneratorParams(qr_code, map);
			params.insert(QStringLiteral("text"), QStringLiteral("FOOM"));
			params.insert(QStringLiteral("background"), with_background);
			run(map, qr_code, params, { 0, 0 });
			
			map.updateObjects();
			QRectF const extent(0, 0, 20, 20);
			constexpr double pixel_per_mm = 10;
			QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
			image.fill(QColor(Qt::white));
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing, false);
			painter.scale(pixel_per_mm, pixel_per_mm);
			map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
			painter.end();
			return image;
		};
		// The quiet zone is 2 mm wide; the finder pattern's corner module is at (2, 2) to (2.5, 2.5) mm.
		auto const with = draw(true);
		QCOMPARE(QColor(with.pixel(10, 10)), QColor(Qt::white));
		QCOMPARE(QColor(with.pixel(22, 22)).lightness() < 60, true);
		auto const without = draw(false);
		auto const quiet = QColor(without.pixel(10, 10));
		QVERIFY2(quiet.blue() < 60 && quiet.red() > 200, qPrintable(quiet.name()));
		QCOMPARE(QColor(without.pixel(22, 22)).lightness() < 60, true);
	}
	
	void qrBackgroundRedrawTest()
	{
		// the design notes, "Generators", third change, Accept (7):
		// what was drawn before the white colour came in keeps its colours.
		Map map;
		map.setScaleDenominator(10000);
		addBlack(map);
		auto* yellow = new MapColor(QStringLiteral("Yellow"), 1);
		yellow->setCmyk({ 0.0f, 0.0f, 1.0f, 0.0f });
		yellow->setRgbFromCmyk();
		map.addColor(yellow, 1);
		auto* land = addSymbol<AreaSymbol>(map, QStringLiteral("Land"), 1, yellow);
		MapCoordVector coords { MapCoord(-10, -10), MapCoord(40, -10), MapCoord(40, 40), MapCoord(-10, 40), MapCoord(-10, -10) };
		coords.back().setClosePoint(true);
		map.addObject(new PathObject(land, coords), 0);
		// The area is drawn before the generator runs, as it is in an open map.
		map.updateObjects();
		
		auto params = defaultGeneratorParams(qr_code, map);
		params.insert(QStringLiteral("text"), QStringLiteral("FOOM"));
		run(map, qr_code, params, { 0, 0 });
		QCOMPARE(map.getColor(1)->getName(), QStringLiteral("White"));
		QCOMPARE(map.getColor(2), yellow);
		
		map.updateObjects();
		QRectF const extent(0, 0, 40, 40);
		constexpr double pixel_per_mm = 10;
		QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(Qt::darkBlue));
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.scale(pixel_per_mm, pixel_per_mm);
		map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
		painter.end();
		// 30 mm is beyond the code's 14.5 mm: the area there is still yellow.
		auto const outside = QColor(image.pixel(300, 300));
		QVERIFY2(outside.blue() < 60 && outside.red() > 200 && outside.green() > 200, qPrintable(outside.name()));
		QCOMPARE(QColor(image.pixel(10, 10)), QColor(Qt::white));
	}
};


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(GeneratorMoreTest)
#include "generator_more_t.moc"  // IWYU pragma: keep
