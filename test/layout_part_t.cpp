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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QDir>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <gdal.h>
#include <ogr_api.h>

#include "global.h"
#include "check/issue.h"
#include "check/legibility.h"
#include "check/map_check.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "layout/layout_part.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

MapColor* addColor(Map& map, const QString& name, const MapColorCmyk& cmyk)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setCmyk(cmyk);
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

AreaSymbol* addAreaSymbol(Map& map, const QString& name, const MapColorCmyk& cmyk, int number)
{
	auto* symbol = new AreaSymbol();
	symbol->setName(name);
	symbol->setColor(addColor(map, name, cmyk));
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

PointSymbol* addPointSymbol(Map& map, const QString& name, int number)
{
	auto* symbol = new PointSymbol();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

PathObject* square(const Symbol* symbol, double x, double y, double size)
{
	MapCoordVector coords {
	    MapCoord(x, y), MapCoord(x + size, y), MapCoord(x + size, y + size), MapCoord(x, y + size), MapCoord(x, y) };
	coords.back().setClosePoint(true);
	return new PathObject(symbol, coords);
}

PointObject* point(const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoord(x, y));
	return object;
}

void setupGeoreferencing(Map& map)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	map.setGeoreferencing(georef);
}

/// Selects the object, which must be in the current part, and moves it to the part.
void moveTo(Map& map, Object* object, int target)
{
	map.clearObjectSelection(false);
	map.addObjectToSelection(object, false);
	QCOMPARE(moveSelectionToPart(map, target), 1);
}

int countRule(const std::vector<Issue>& issues, const char* rule)
{
	int n = 0;
	for (auto const& issue : issues)
		n += issue.rule == rule;
	return n;
}

const MapCoordVector::size_type first = 0;

}  // namespace



/**
 * Tests the layout part commands.
 *
 * See the design notes, "Layout part commands".
 */
class LayoutPartTest : public QObject
{
Q_OBJECT
	
	QTemporaryDir dir;
	
	QString filePath(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/// A map with symbols A and B in the map part and C in the layout part.
	void makeLayoutMap(Map& map)
	{
		setupGeoreferencing(map);
		auto* a = addAreaSymbol(map, QStringLiteral("A"), { 0.0f, 1.0f, 1.0f, 0.0f }, 1);
		auto* b = addAreaSymbol(map, QStringLiteral("B"), { 1.0f, 1.0f, 0.0f, 0.0f }, 2);
		auto* c = addAreaSymbol(map, QStringLiteral("C"), { 1.0f, 0.0f, 1.0f, 0.0f }, 3);
		map.addObject(square(a, 0, 0, 10));
		map.addObject(square(b, 20, 0, 10));
		addLayoutPart(map);
		map.addObject(square(c, 100, 50, 5));
	}
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		GDALAllRegister();
		QVERIFY(dir.isValid());
	}
	
	/// Accept (1)
	void createTest()
	{
		Map map;
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(layoutPartIndex(map), -1);
		QCOMPARE(addLayoutPart(map), 1);
		QCOMPARE(map.getNumParts(), 2);
		auto const* part = map.getPart(1);
		QCOMPARE(part->getName(), QStringLiteral("Layout"));
		QVERIFY(part->role() == MapPart::Role::Layout);
		QVERIFY(part->isVisible());
		QCOMPARE(map.getCurrentPartIndex(), std::size_t(1));
		QCOMPARE(layoutPartIndex(map), 1);
		QCOMPARE(mapContentPartIndex(map), 0);
		
		QCOMPARE(addLayoutPart(map), 1);
		QCOMPARE(map.getNumParts(), 2);
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(layoutPartIndex(map), -1);
	}
	
	/// Hiding the current layout part makes the map part current (part of the design above Accept).
	void hideTest()
	{
		Map map;
		QVERIFY(!setLayoutVisible(map, false));
		addLayoutPart(map);
		QVERIFY(setLayoutVisible(map, false));
		QVERIFY(!map.getPart(1)->isVisible());
		QCOMPARE(map.getCurrentPartIndex(), std::size_t(0));
		QVERIFY(setLayoutVisible(map, true));
		QVERIFY(map.getPart(1)->isVisible());
	}
	
	/// Accept (2)
	void moveTest()
	{
		Map map;
		auto* symbol = addAreaSymbol(map, QStringLiteral("A"), { 0.0f, 1.0f, 1.0f, 0.0f }, 1);
		auto* object = square(symbol, 0, 0, 10);
		map.addObject(object);
		addLayoutPart(map);
		map.setCurrentPartIndex(0);
		auto const steps = map.undoManager().undoStepCount();
		
		moveTo(map, object, 1);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(map.getPart(0)->getNumObjects(), 0);
		QCOMPARE(map.getPart(1)->getNumObjects(), 1);
		QVERIFY(map.getPart(1)->contains(object));
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getPart(0)->getNumObjects(), 1);
		QCOMPARE(map.getPart(1)->getNumObjects(), 0);
		
		QVERIFY(map.undoManager().redo());
		QCOMPARE(map.getPart(1)->getNumObjects(), 1);
		map.setCurrentPartIndex(1);
		moveTo(map, map.getPart(1)->getObject(0), mapContentPartIndex(map));
		QCOMPARE(map.getPart(0)->getNumObjects(), 1);
		QCOMPARE(map.getPart(1)->getNumObjects(), 0);
		
		// Nothing selected, or the target is the current part.
		map.clearObjectSelection(false);
		QCOMPARE(moveSelectionToPart(map, 1), 0);
		map.setCurrentPartIndex(0);
		map.addObjectToSelection(map.getPart(0)->getObject(0), false);
		QCOMPARE(moveSelectionToPart(map, 0), 0);
	}
	
	/// Accept (3)
	void rotateTest()
	{
		Map map;
		auto* symbol = addPointSymbol(map, QStringLiteral("P"), 1);
		auto* in_map = point(symbol, 10, 0);
		map.addObject(in_map);
		addLayoutPart(map);
		auto* in_layout = point(symbol, 10, 0);
		map.addObject(in_layout);
		QVERIFY(map.getPart(1)->contains(in_layout));
		
		map.rotateMap(M_PI / 2, MapCoord(0, 0), false, false, false);
		QCOMPARE(in_map->getCoord().nativeX(), 0);
		QCOMPARE(in_map->getCoord().nativeY(), -10000);
		QCOMPARE(in_layout->getCoord().nativeX(), 10000);
		QCOMPARE(in_layout->getCoord().nativeY(), 0);
		
		map.rotateMap(M_PI / 2, MapCoord(0, 0), false, false, false, true);
		QCOMPARE(in_map->getCoord().nativeX(), -10000);
		QCOMPARE(in_map->getCoord().nativeY(), 0);
		QCOMPARE(in_layout->getCoord().nativeX(), 0);
		QCOMPARE(in_layout->getCoord().nativeY(), -10000);
	}
	
	/// Accept (4)
	void scaleTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* symbol = addPointSymbol(map, QStringLiteral("P"), 1);
		addLayoutPart(map);
		auto* in_layout = point(symbol, 30, 0);
		map.addObject(in_layout);
		map.changeScale(15000, 1.0, MapCoord(0, 0), false, true, false, false);
		QCOMPARE(in_layout->getCoord().nativeX(), 20000);
		QCOMPARE(in_layout->getCoord().nativeY(), 0);
	}
	
	/// Accept (5)
	void checksTest()
	{
		Map map;
		auto* text_symbol = new TextSymbol();
		map.addSymbol(text_symbol, 0);
		auto* text = new TextObject(text_symbol);
		text->setAnchorPosition(MapCoord(0, 0));
		map.addObject(text);
		
		auto* line_symbol = new LineSymbol();
		line_symbol->setLineWidth(0.35);
		line_symbol->setMinimumLength(5000);
		map.addSymbol(line_symbol, 1);
		auto* line = new PathObject(line_symbol, MapCoordVector{ MapCoord(0, 50), MapCoord(1, 50) });
		map.addObject(line);
		
		QCOMPARE(countRule(checkMap(map), rule::text_empty), 1);
		QCOMPARE(countRule(checkLegibility(map, LegibilityParams{}), rule::line_short), 1);
		
		addLayoutPart(map);
		map.setCurrentPartIndex(0);
		moveTo(map, text, 1);
		moveTo(map, line, 1);
		QCOMPARE(countRule(checkMap(map), rule::text_empty), 0);
		QCOMPARE(countRule(checkLegibility(map, LegibilityParams{}), rule::line_short), 0);
	}
	
	/// Accept (6)
	void vectorExportTest()
	{
		auto const* format = FileFormats.findFormat("OGR-export-GPKG");
		if (!format)
			QSKIP("GDAL has no GeoPackage driver");
		
		Map map;
		setupGeoreferencing(map);
		auto* a = addPointSymbol(map, QStringLiteral("A"), 1);
		auto* b = addPointSymbol(map, QStringLiteral("B"), 2);
		map.addObject(point(a, 0, 0));
		addLayoutPart(map);
		map.addObject(point(b, 5, 5));
		
		auto layers = [](const QString& path) {
			QStringList result;
			auto* dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
			if (!dataset)
				return result;
			for (int i = 0; i < GDALDatasetGetLayerCount(dataset); ++i)
			{
				auto* layer = GDALDatasetGetLayer(dataset, i);
				result.append(QString::fromUtf8(OGR_L_GetName(layer)) + QLatin1Char('=') + QString::number(OGR_L_GetFeatureCount(layer, 1)));
			}
			GDALClose(dataset);
			return result;
		};
		
		auto const by_type = filePath("by_type.gpkg");
		{
			auto exporter = format->makeExporter(by_type, &map, nullptr);
			QVERIFY(exporter);
			QVERIFY(exporter->doExport());
		}
		QVERIFY(layers(by_type).contains(QStringLiteral("by_type_points=1")));
		
		auto const by_symbol = filePath("by_symbol.gpkg");
		{
			auto exporter = format->makeExporter(by_symbol, &map, nullptr);
			QVERIFY(exporter);
			exporter->setOption(QStringLiteral("Per Symbol Layers"), true);
			QVERIFY(exporter->doExport());
		}
		auto const names = layers(by_symbol);
		QVERIFY2(names.contains(QStringLiteral("by_symbol_A=1")), qPrintable(names.join(QLatin1Char(' '))));
		QVERIFY2(names.filter(QStringLiteral("by_symbol_B")).isEmpty(), qPrintable(names.join(QLatin1Char(' '))));
	}
	
	/// Accept (7)
	void saveTest()
	{
		Map map;
		makeLayoutMap(map);
		QString error;
		QVERIFY(!saveLayout(map, filePath("layout.ocd"), &error));
		QVERIFY(!error.isEmpty());
		
		auto const path = filePath("layout.foom");
		QVERIFY2(saveLayout(map, path, &error), qPrintable(error));
		
		Map loaded;
		QVERIFY(loaded.loadFrom(path));
		QCOMPARE(loaded.getNumParts(), 1);
		QVERIFY(loaded.getPart(0)->role() == MapPart::Role::Layout);
		QCOMPARE(loaded.getPart(0)->getNumObjects(), 1);
		auto const* object = loaded.getPart(0)->getObject(0)->asPath();
		QCOMPARE(object->getCoordinate(first).nativeX(), 100000);
		QCOMPARE(object->getCoordinate(first).nativeY(), 50000);
		QCOMPARE(loaded.getNumSymbols(), 1);
		QCOMPARE(loaded.getSymbol(0)->getName(), QStringLiteral("C"));
		QCOMPARE(loaded.getNumColors(), 1);
		QCOMPARE(loaded.getColor(0)->getName(), QStringLiteral("C"));
		QCOMPARE(loaded.getScaleDenominator(), 10000u);
		QCOMPARE(loaded.getGeoreferencing().getProjectedCRSSpec(), map.getGeoreferencing().getProjectedCRSSpec());
		QCOMPARE(loaded.getGeoreferencing().getProjectedRefPoint(), map.getGeoreferencing().getProjectedRefPoint());
	}
	
	/// Accept (8)
	void importTest()
	{
		Map source;
		makeLayoutMap(source);
		auto const path = filePath("import.foom");
		QString error;
		QVERIFY2(saveLayout(source, path, &error), qPrintable(error));
		
		Map map;
		setupGeoreferencing(map);
		map.setScaleDenominator(15000);
		auto* c = addAreaSymbol(map, QStringLiteral("C"), { 1.0f, 0.0f, 1.0f, 0.0f }, 3);
		map.addObject(square(c, 0, 0, 10));
		auto const steps = map.undoManager().undoStepCount();
		
		QCOMPARE(importLayoutFile(map, path, &error), 1);
		QCOMPARE(map.getNumParts(), 2);
		QCOMPARE(layoutPartIndex(map), 1);
		QCOMPARE(map.getCurrentPartIndex(), std::size_t(0));
		auto const* part = map.getPart(1);
		QCOMPARE(part->getNumObjects(), 1);
		auto const* object = part->getObject(0)->asPath();
		QCOMPARE(object->getCoordinate(first).nativeX(), 100000);
		QCOMPARE(object->getCoordinate(first).nativeY(), 50000);
		QCOMPARE(map.getNumSymbols(), 1);
		QCOMPARE(object->getSymbol(), static_cast<const Symbol*>(c));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(map.getPart(0)->getNumObjects(), 1);
		QCOMPARE(layoutPartIndex(map), -1);
	}
	
	/// Accept (9)
	void importRefusedTest()
	{
		Map source;
		auto* a = addAreaSymbol(source, QStringLiteral("A"), { 0.0f, 1.0f, 1.0f, 0.0f }, 1);
		source.addObject(square(a, 0, 0, 10));
		
		Map map;
		auto* b = addAreaSymbol(map, QStringLiteral("B"), { 1.0f, 1.0f, 0.0f, 0.0f }, 2);
		map.addObject(square(b, 0, 0, 10));
		auto const steps = map.undoManager().undoStepCount();
		QString error;
		QCOMPARE(importLayout(map, source, &error), -1);
		QVERIFY(!error.isEmpty());
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(map.getNumObjects(), 1);
		QCOMPARE(map.getNumSymbols(), 1);
		QCOMPARE(map.undoManager().undoStepCount(), steps);
	}
};



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(LayoutPartTest)
#include "layout_part_t.moc"  // IWYU pragma: keep
