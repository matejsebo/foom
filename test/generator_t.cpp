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


#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/private_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "layout/generator.h"
#include "layout/layout_part.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const QString scale_bar = QStringLiteral("gen-scale-bar");
const QString north_lines = QStringLiteral("gen-north-lines");
const QString legend = QStringLiteral("gen-legend");

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

PointSymbol* addPointSymbol(Map& map, const QString& name, int number, const MapColor* color)
{
	auto* symbol = new PointSymbol();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	symbol->setInnerRadius(300);
	symbol->setInnerColor(color);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

MapCoord coord(const Object* object, std::size_t index)
{
	return object->getRawCoordinateVector()[MapCoordVector::size_type(index)];
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

QStringList texts(const std::vector<Object*>& objects, int v_align = -1)
{
	QStringList result;
	for (auto const* object : objects)
	{
		if (object->getType() == Object::Text && (v_align < 0 || int(object->asText()->getVerticalAlignment()) == v_align))
			result.append(object->asText()->getText());
	}
	return result;
}

/// The objects of the group which have the symbol.
std::vector<const Object*> withSymbol(const std::vector<Object*>& objects, const Symbol* symbol)
{
	std::vector<const Object*> result;
	for (auto const* object : objects)
	{
		if (object->getSymbol() == symbol)
			result.push_back(object);
	}
	return result;
}

/// The labels of a legend: the texts in a private symbol.
QStringList labels(const std::vector<Object*>& objects)
{
	QStringList result;
	for (auto const* object : objects)
	{
		if (object->getType() == Object::Text && object->getSymbol()->isPrivate())
			result.append(object->asText()->getText());
	}
	return result;
}

}  // namespace



/**
 * Tests the generators.
 *
 * See the design notes, "Generators".
 */
class GeneratorTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	/// Accept (1)
	void autoLengthTest()
	{
		QCOMPARE(scaleBarAutoLength(10000), 500.0);
		QCOMPARE(scaleBarAutoLength(15000), 500.0);
		QCOMPARE(scaleBarAutoLength(4000), 200.0);
		QCOMPARE(scaleBarAutoLength(50000), 2000.0);
	}
	
	/// Accept (2)
	void scaleBarTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		QString error;
		auto const group = runGenerator(map, scale_bar, defaultGeneratorParams(scale_bar, map), MapCoordF(20, 30), &error);
		QVERIFY2(group != 0, qPrintable(error));
		QCOMPARE(map.getNumParts(), 2);
		QCOMPARE(layoutPartIndex(map), 1);
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		QCOMPARE(map.objectGroupName(group), QStringLiteral("Scale bar"));
		
		auto part = -1;
		auto const objects = groupObjects(map, group, &part);
		QCOMPARE(part, 1);
		QCOMPARE(int(objects.size()), map.getPart(1)->getNumObjects());
		// The frame, three blocks, six labels, the ratio.
		QCOMPARE(objects.size(), std::size_t(11));
		for (auto const* object : objects)
		{
			QCOMPARE(object->getTag(QStringLiteral("generator")), scale_bar);
			QVERIFY(!object->getTag(QStringLiteral("params")).isEmpty());
			QCOMPARE(object->group(), group);
		}
		QVERIFY(coord(objects.front(), 0) == MapCoord(20.0, 30.0));
		QVERIFY(coord(objects.front(), 1) == MapCoord(70.0, 30.0));
		auto const bar_labels = texts(objects, TextObject::AlignBottom);
		QCOMPARE(bar_labels.size(), 6);
		QCOMPARE(bar_labels.first(), QStringLiteral("0"));
		QCOMPARE(bar_labels.last(), QStringLiteral("500 m"));
		QVERIFY(texts(objects).contains(QStringLiteral("1:10 000")));
		QVERIFY(generatedGroups(map, false) == std::vector<int>{ group });
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumParts(), 1);
		QVERIFY(generatedGroups(map, false).empty());
	}
	
	/// Accept (3)
	void regenerateTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		QString error;
		auto const group = runGenerator(map, scale_bar, defaultGeneratorParams(scale_bar, map), MapCoordF(20, 30), &error);
		QVERIFY2(group != 0, qPrintable(error));
		for (auto* object : groupObjects(map, group))
		{
			object->move(MapCoord(7.0, 3.0));
			object->update();
		}
		map.setScaleDenominator(15000);
		QVERIFY(scaleDependentGroups(map) == std::vector<int>{ group });
		
		auto const steps = map.undoManager().undoStepCount();
		QCOMPARE(regenerateGroups(map, { group }, &error), 1);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		auto part = -1;
		auto objects = groupObjects(map, group, &part);
		QCOMPARE(part, 1);
		QCOMPARE(objects.size(), std::size_t(11));
		QVERIFY(coord(objects.front(), 0) == MapCoord(27.0, 33.0));
		QCOMPARE(coord(objects.front(), 1).nativeX() - coord(objects.front(), 0).nativeX(), 33333);
		QCOMPARE(texts(objects, TextObject::AlignBottom).last(), QStringLiteral("500 m"));
		QVERIFY(texts(objects).contains(QStringLiteral("1:15 000")));
		QVERIFY(generatedGroups(map, false) == std::vector<int>{ group });
		
		QVERIFY(map.undoManager().undo());
		objects = groupObjects(map, group);
		QCOMPARE(objects.size(), std::size_t(11));
		QCOMPARE(coord(objects.front(), 1).nativeX() - coord(objects.front(), 0).nativeX(), 50000);
	}
	
	/// Accept (4)
	void ticksTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto params = defaultGeneratorParams(scale_bar, map);
		params.insert(QStringLiteral("style"), QStringLiteral("ticks"));
		params.insert(QStringLiteral("length_m"), 300);
		params.insert(QStringLiteral("divisions"), 3);
		params.insert(QStringLiteral("contour_interval_m"), 5);
		QString error;
		auto const group = runGenerator(map, scale_bar, params, MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		auto const objects = groupObjects(map, group);
		auto const lines = paths(objects);
		QCOMPARE(lines.size(), std::size_t(5));
		QVERIFY(coord(lines.front(), 0) == MapCoord(0.0, 1.2));
		QVERIFY(coord(lines.front(), 1) == MapCoord(30.0, 1.2));
		auto const bar_labels = texts(objects, TextObject::AlignBottom);
		QCOMPARE(bar_labels.size(), 4);
		QCOMPARE(bar_labels.last(), QStringLiteral("300 m"));
		QVERIFY(texts(objects).contains(QStringLiteral("1:10 000")));
		auto contour_text = false;
		for (auto const& text : texts(objects, TextObject::AlignTop))
			contour_text = contour_text || text.contains(QLatin1String("5 m"));
		QVERIFY(contour_text);
		
		// The rebuilt bar is where the first one was.
		QCOMPARE(regenerateGroups(map, { group }, &error), 1);
		QVERIFY(coord(groupObjects(map, group).front(), 0) == MapCoord(0.0, 1.2));
	}
	
	/// Accept (5)
	void northLinesTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Outline"), 1, addBlack(map));
		MapCoordVector coords { MapCoord(5, 10), MapCoord(105, 10), MapCoord(105, 70), MapCoord(5, 70), MapCoord(5, 10) };
		coords.back().setClosePoint(true);
		auto* outline = new PathObject(line, coords);
		map.addObject(outline);
		
		auto params = defaultGeneratorParams(north_lines, map);
		params.insert(QStringLiteral("spacing_m"), 300);
		params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
		QString error;
		QCOMPARE(runGenerator(map, north_lines, params, MapCoordF(0, 0), &error), 0);
		QVERIFY(!error.isEmpty());
		
		map.addObjectToSelection(outline, false);
		auto const group = runGenerator(map, north_lines, params, MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(map.objectGroupName(group), QStringLiteral("North lines"));
		auto part = -1;
		auto const objects = groupObjects(map, group, &part);
		QCOMPARE(part, 0);
		QCOMPARE(objects.size(), std::size_t(3));
		for (std::size_t i = 0; i < 3; ++i)
		{
			auto const x = 30.0 * (i + 1);
			QVERIFY(coord(objects[i], 0) == MapCoord(x, 10.0));
			QVERIFY(coord(objects[i], 1) == MapCoord(x, 70.0));
			QVERIFY(objects[i]->getTag(QStringLiteral("params")).contains(QLatin1String("outline")));
		}
		
		// The outline is stored, so a rebuild does not need the selection.
		map.clearObjectSelection(false);
		QCOMPARE(regenerateGroups(map, { group }, &error), 1);
		QCOMPARE(groupObjects(map, group).size(), std::size_t(3));
		QVERIFY(scaleDependentGroups(map).empty());
	}
	
	/// Accept (6), (7)
	void northLinesAngleTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto params = defaultGeneratorParams(north_lines, map);
		params.insert(QStringLiteral("spacing_m"), 300);
		params.insert(QStringLiteral("angle"), 10);
		params.insert(QStringLiteral("outline"), QJsonArray{ QJsonArray{ 0, 0 }, QJsonArray{ 100, 0 }, QJsonArray{ 100, 60 }, QJsonArray{ 0, 60 } });
		QString error;
		auto group = runGenerator(map, north_lines, params, MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		auto objects = groupObjects(map, group);
		QVERIFY(objects.size() >= 3);
		auto const angle = qDegreesToRadians(10.0);
		auto previous = 0.0;
		for (std::size_t i = 0; i < objects.size(); ++i)
		{
			auto const top = QPointF(MapCoordF(coord(objects[i], 0)));
			auto const bottom = QPointF(MapCoordF(coord(objects[i], 1)));
			auto const up = top - bottom;
			QVERIFY(qAbs(std::atan2(up.x(), -up.y()) - angle) < 0.001);
			auto const across = top.x() * std::cos(angle) + top.y() * std::sin(angle);
			if (i > 0)
				QVERIFY(qAbs(across - previous - 30.0) < 0.01);
			previous = across;
		}
		
		// A notch from the right: every line through it is cut in two.
		params.insert(QStringLiteral("angle"), 0);
		params.insert(QStringLiteral("outline"), QJsonArray{
		    QJsonArray{ 0, 0 }, QJsonArray{ 100, 0 }, QJsonArray{ 100, 20 }, QJsonArray{ 20, 20 },
		    QJsonArray{ 20, 40 }, QJsonArray{ 100, 40 }, QJsonArray{ 100, 60 }, QJsonArray{ 0, 60 } });
		group = runGenerator(map, north_lines, params, MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		objects = groupObjects(map, group);
		QCOMPARE(objects.size(), std::size_t(6));
		auto at_30 = 0;
		for (auto const* object : objects)
		{
			if (coord(object, 0).nativeX() == 30000)
			{
				++at_30;
				QCOMPARE(coord(object, 1).nativeY() - coord(object, 0).nativeY(), 20000);
			}
		}
		QCOMPARE(at_30, 2);
	}
	
	/// Accept (8)
	void northLinesDefaultTest()
	{
		Map map;
		map.setScaleDenominator(15000);
		map.setSymbolSetId(QStringLiteral("ISOM 2017-2"));
		QCOMPARE(northLinesDefaultSpacing(map), 300.0);
		map.setScaleDenominator(4000);
		map.setSymbolSetId(QStringLiteral("ISSprOM 2019"));
		QCOMPARE(northLinesDefaultSpacing(map), 120.0);
	}
	
	/// Accept (9)
	void legendTest()
	{
		Map map;
		auto* black = addBlack(map);
		auto* point = addPointSymbol(map, QStringLiteral("Point"), 1, black);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Line"), 2, black);
		line->setLineWidth(0.3);
		auto* area = addSymbol<AreaSymbol>(map, QStringLiteral("Area"), 3, black);
		auto* text = addSymbol<TextSymbol>(map, QStringLiteral("Text"), 4, black);
		auto* helper = addSymbol<LineSymbol>(map, QStringLiteral("Helper"), 5, black);
		helper->setIsHelperSymbol(true);
		auto* hidden = addSymbol<LineSymbol>(map, QStringLiteral("Hidden"), 6, black);
		hidden->setHidden(true);
		map.addSymbol(makePrivateCopy(*line).release(), map.getNumSymbols());
		
		QString error;
		auto params = defaultGeneratorParams(legend, map);
		auto group = runGenerator(map, legend, params, MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		auto objects = groupObjects(map, group);
		QCOMPARE(labels(objects), (QStringList{ QStringLiteral("Point"), QStringLiteral("Line"), QStringLiteral("Area"), QStringLiteral("Text"), QStringLiteral("Hidden") }));
		QCOMPARE(objects.size(), std::size_t(10));
		QCOMPARE(withSymbol(objects, point).size(), std::size_t(1));
		QCOMPARE(withSymbol(objects, line).size(), std::size_t(1));
		QCOMPARE(withSymbol(objects, area).size(), std::size_t(1));
		QCOMPARE(withSymbol(objects, text).size(), std::size_t(1));
		QCOMPARE(withSymbol(objects, hidden).size(), std::size_t(1));
		QCOMPARE(withSymbol(objects, helper).size(), std::size_t(0));
		QCOMPARE(map.objectGroupName(group), QStringLiteral("Legend"));
		
		params.insert(QStringLiteral("include_hidden"), false);
		group = runGenerator(map, legend, params, MapCoordF(100, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		QCOMPARE(labels(groupObjects(map, group)).size(), 4);
		
		// The legends above do not make their symbols used.
		params.insert(QStringLiteral("only_used"), true);
		QCOMPARE(runGenerator(map, legend, params, MapCoordF(200, 0), &error), 0);
		map.addObject(new PathObject(line, MapCoordVector{ MapCoord(0, 500), MapCoord(10, 500) }), 0);
		params.insert(QStringLiteral("show_number"), true);
		group = runGenerator(map, legend, params, MapCoordF(200, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		QCOMPARE(labels(groupObjects(map, group)), QStringList{ QStringLiteral("2 Line") });
	}
	
	/// Accept (10)
	void legendSampleTest()
	{
		Map map;
		auto* black = addBlack(map);
		auto* dashed = addSymbol<LineSymbol>(map, QStringLiteral("Dashed"), 1, black);
		dashed->setLineWidth(0.3);
		dashed->setDashed(true);
		dashed->setDashLength(10000);
		dashed->setBreakLength(4000);
		auto* plain = addSymbol<LineSymbol>(map, QStringLiteral("Plain"), 2, black);
		plain->setLineWidth(0.3);
		auto* area = addSymbol<AreaSymbol>(map, QStringLiteral("Area"), 3, black);
		
		QString error;
		auto const group = runGenerator(map, legend, defaultGeneratorParams(legend, map), MapCoordF(0, 0), &error);
		QVERIFY2(group != 0, qPrintable(error));
		auto const objects = groupObjects(map, group);
		auto const* dashed_sample = withSymbol(objects, dashed).front();
		QCOMPARE(coord(dashed_sample, 1).nativeX() - coord(dashed_sample, 0).nativeX(), 14000);
		auto const* plain_sample = withSymbol(objects, plain).front();
		QCOMPARE(coord(plain_sample, 1).nativeX() - coord(plain_sample, 0).nativeX(), 10000);
		auto const* area_sample = withSymbol(objects, area).front()->asPath();
		QVERIFY(area_sample->parts().front().isClosed());
		QCOMPARE(coord(area_sample, 2).nativeX() - coord(area_sample, 0).nativeX(), 10000);
		QCOMPARE(coord(area_sample, 2).nativeY() - coord(area_sample, 0).nativeY(), 5000);
	}
	
	/// Accept (11)
	void legendColumnsTest()
	{
		Map map;
		auto* black = addBlack(map);
		for (int i = 1; i <= 30; ++i)
			addSymbol<LineSymbol>(map, QStringLiteral("Line %1").arg(i), i, black)->setLineWidth(0.3);
		auto params = defaultGeneratorParams(legend, map);
		params.insert(QStringLiteral("icon_height"), 7);
		params.insert(QStringLiteral("line_spacing"), 1);
		params.insert(QStringLiteral("max_column_height"), 100);
		QString error;
		auto const group = runGenerator(map, legend, params, MapCoordF(10, 20), &error);
		QVERIFY2(group != 0, qPrintable(error));
		int columns[3] = { 0, 0, 0 };
		for (auto const* sample : paths(groupObjects(map, group)))
		{
			auto const column = (coord(sample, 0).nativeX() - 10000) / 60000;
			QVERIFY(column >= 0 && column < 3);
			QCOMPARE((coord(sample, 0).nativeX() - 10000) % 60000, 0);
			++columns[column];
		}
		QCOMPARE(columns[0], 12);
		QCOMPARE(columns[1], 12);
		QCOMPARE(columns[2], 6);
	}
};



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(GeneratorTest)
#include "generator_t.moc"  // IWYU pragma: keep
