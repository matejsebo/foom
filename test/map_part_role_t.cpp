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


#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QIODevice>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "fileformats/xml_file_format.h"
#include "fileformats/file_import_export.h"
#include "undo/map_part_undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const MapColorCmyk red_cmyk { 0.0f, 1.0f, 1.0f, 0.0f };
const MapColorCmyk blue_cmyk { 1.0f, 1.0f, 0.0f, 0.0f };

AreaSymbol* addAreaSymbol(Map& map, const QString& name, const MapColorCmyk& cmyk, int number)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setCmyk(cmyk);
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	auto* symbol = new AreaSymbol();
	symbol->setName(name);
	symbol->setColor(color);
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

/// The number of pixels whose colour is mostly blue, or mostly red.
int countPixels(Map& map, bool blue)
{
	map.updateObjects();
	QRectF const extent(-20, -20, 40, 40);
	constexpr double pixel_per_mm = 5;
	QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.scale(pixel_per_mm, pixel_per_mm);
	painter.translate(-extent.topLeft());
	map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
	painter.end();
	auto count = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			auto const c = QColor(image.pixel(x, y));
			if (blue ? (c.blue() > 200 && c.red() < 60) : (c.red() > 200 && c.blue() < 60))
				++count;
		}
	}
	return count;
}

struct Fixture
{
	Map map;
	AreaSymbol* red = nullptr;
	AreaSymbol* blue = nullptr;
	MapPart* layout = nullptr;
	
	Fixture()
	{
		red = addAreaSymbol(map, QStringLiteral("Red"), red_cmyk, 401);
		blue = addAreaSymbol(map, QStringLiteral("Blue"), blue_cmyk, 402);
		map.getPart(0)->addObject(square(red, -10, -10, 5));
		layout = new MapPart(QStringLiteral("Layout"), &map);
		map.addPart(layout, 1);
		layout->setRole(MapPart::Role::Layout);
		layout->addObject(square(blue, 5, 5, 5));
	}
};

QByteArray save(const Map& map, bool foom)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setOption(QStringLiteral("foom"), foom);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool load(const QByteArray& data, Map& map)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format.makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

}  // namespace



/**
 * Tests the roles and the visibility of map parts.
 *
 * See the design notes, "Layout part", Map part roles.
 */
class MapPartRoleTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	/// Accept (1)
	void defaultsTest()
	{
		Map map;
		QCOMPARE(map.getPart(0)->role(), MapPart::Role::Map);
		QVERIFY(map.getPart(0)->isVisible());
		QCOMPARE(map.findPartIndexWithRole(MapPart::Role::Map), 0);
		QCOMPARE(map.findPartIndexWithRole(MapPart::Role::Layout), -1);
		
		Fixture f;
		QCOMPARE(f.map.findPartIndexWithRole(MapPart::Role::Layout), 1);
		QCOMPARE(MapPart::roleName(MapPart::Role::Issues), QStringLiteral("issues"));
		bool ok = false;
		QCOMPARE(MapPart::roleFromName(QStringLiteral("sketch"), &ok), MapPart::Role::Sketch);
		QVERIFY(ok);
		MapPart::roleFromName(QStringLiteral("nonsense"), &ok);
		QVERIFY(!ok);
	}
	
	/// Accept (2)
	void fileTest()
	{
		Fixture f;
		f.layout->setVisible(false);
		auto const data = save(f.map, true);
		QVERIFY(data.contains("role=\"layout\""));
		QVERIFY(data.contains("visible=\"false\""));
		
		Map reloaded;
		QVERIFY(load(data, reloaded));
		QCOMPARE(reloaded.getNumParts(), 2);
		QCOMPARE(reloaded.getPart(0)->role(), MapPart::Role::Map);
		QVERIFY(reloaded.getPart(0)->isVisible());
		QCOMPARE(reloaded.getPart(1)->role(), MapPart::Role::Layout);
		QVERIFY(!reloaded.getPart(1)->isVisible());
		QCOMPARE(reloaded.getPart(1)->getNumObjects(), 1);
		QCOMPARE(countPixels(reloaded, true), 0);
		QVERIFY(countPixels(reloaded, false) > 0);
		
		auto const plain = save(f.map, false);
		QVERIFY(!plain.isEmpty());
		QVERIFY(!plain.contains("role="));
		QVERIFY(!plain.contains("visible=\"false\""));
	}
	
	/// Accept (3)
	void hideShowTest()
	{
		Fixture f;
		auto const blue_before = countPixels(f.map, true);
		QVERIFY(blue_before > 0);
		f.layout->setVisible(false);
		QCOMPARE(countPixels(f.map, true), 0);
		QVERIFY(countPixels(f.map, false) > 0);
		f.layout->setVisible(true);
		QCOMPARE(countPixels(f.map, true), blue_before);
	}
	
	/// Accept (4)
	void addAndMoveTest()
	{
		Fixture f;
		f.layout->setVisible(false);
		auto* object = square(f.blue, -15, 5, 4);
		f.layout->addObject(object);
		QCOMPARE(countPixels(f.map, true), 0);
		
		auto* released = f.layout->releaseObject(object);
		QCOMPARE(released, static_cast<Object*>(object));
		f.map.getPart(0)->addObject(released);
		QVERIFY(countPixels(f.map, true) > 0);
	}
	
	/// Accept (5)
	void symbolUpdateTest()
	{
		Fixture f;
		f.layout->setVisible(false);
		f.map.updateAllObjectsWithSymbol(f.blue);
		QCOMPARE(countPixels(f.map, true), 0);
	}
	
	/// Accept (6)
	void extentTest()
	{
		Fixture f;
		auto const full = f.map.calculateExtent();
		QVERIFY(full.contains(QPointF(7, 7)));
		f.layout->setVisible(false);
		auto const reduced = f.map.calculateExtent();
		QVERIFY(!reduced.contains(QPointF(7, 7)));
		QVERIFY(reduced.contains(QPointF(-8, -8)));
	}
	
	/// Accept (7)
	void undoTest()
	{
		Fixture f;
		f.layout->deleteObject(0);
		f.layout->setVisible(false);
		auto* step = new MapPartUndoStep(&f.map, MapPartUndoStep::AddMapPart, 1);
		f.map.removePart(1);
		QCOMPARE(f.map.getNumParts(), 1);
		f.map.push(step);
		QVERIFY(f.map.undoManager().undo());
		QCOMPARE(f.map.getNumParts(), 2);
		QCOMPARE(f.map.getPart(1)->role(), MapPart::Role::Layout);
		QVERIFY(!f.map.getPart(1)->isVisible());
	}
};


QTEST_MAIN(MapPartRoleTest)
#include "map_part_role_t.moc"  // IWYU pragma: keep
