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


#include <cstddef>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "sketch/sketch.h"
#include "templates/world_file.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// A straight stroke along y = 10 mm.
Object* strokeAlong(Map& map, double from, double to, int color = 0, int width = 1)
{
	return addSketchStroke(map, { MapCoordF(from, 10), MapCoordF((from + to) / 2, 10), MapCoordF(to, 10) }, color, width);
}

QPointF first(const Object* object)
{
	return QPointF(MapCoordF(object->getRawCoordinateVector().front()));
}

QPointF last(const Object* object)
{
	return QPointF(MapCoordF(object->getRawCoordinateVector().back()));
}

bool near(const QPointF& a, const QPointF& b)
{
	return qAbs(a.x() - b.x()) < 0.002 && qAbs(a.y() - b.y()) < 0.002;
}

/// The area from (0, 0) to (40, 20) mm at 5 pixels per mm.
QImage render(Map& map, RenderConfig::Options options)
{
	QImage image(200, 100, QImage::Format_RGB32);
	image.fill(QColor(Qt::white));
	QPainter painter(&image);
	painter.scale(5, 5);
	RenderConfig config = { map, QRectF(0, 0, 40, 20), 5, options, 1.0 };
	map.draw(&painter, config);
	return image;
}

template <class S>
S* addSymbol(Map& map, const QString& name, int number)
{
	auto* symbol = new S();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

}  // namespace


/**
 * @test Tests the sketch part: strokes, notes, the eraser, the list of
 *       items, the conversion to map objects and the flattening.
 *
 * the design notes, "Sketch layer", first change.
 */
class SketchTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	/// Accept (1)
	void strokeTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		QCOMPARE(sketchPartIndex(map), -1);
		QVERIFY(!addSketchStroke(map, { MapCoordF(1, 1) }, 0, 1));
		QCOMPARE(map.undoManager().undoStepCount(), 0);
		
		auto const* stroke = strokeAlong(map, 5, 35);
		QVERIFY(stroke);
		QCOMPARE(map.getNumParts(), 2);
		auto const index = sketchPartIndex(map);
		QCOMPARE(index, 1);
		auto const* part = map.getPart(std::size_t(index));
		QCOMPARE(part->role(), MapPart::Role::Sketch);
		QCOMPARE(part->getName(), sketchPartName());
		QCOMPARE(part->getNumObjects(), 1);
		QCOMPARE(map.getPart(0)->getNumObjects(), 0);
		QCOMPARE(map.getCurrentPartIndex(), std::size_t(0));
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		
		QCOMPARE(stroke->getType(), Object::Path);
		QCOMPARE(stroke->getRawCoordinateVector().size(), MapCoordVector::size_type(3));
		QVERIFY(near(first(stroke), { 5, 10 }));
		QVERIFY(near(last(stroke), { 35, 10 }));
		auto const* symbol = stroke->getSymbol();
		QCOMPARE(symbol->getType(), Symbol::Line);
		QVERIFY(symbol->isPrivate());
		QVERIFY(symbol->isHelperSymbol());
		QCOMPARE(symbol->asLine()->getLineWidth(), 500);
		QCOMPARE(sketchWidth(1), 0.5);
		// The colour is in the colour table, at the top.
		QCOMPARE(map.getNumColors(), 1);
		QCOMPARE(map.getColor(0)->getName(), sketchColorName(0));
		QCOMPARE(symbol->asLine()->getColor(), map.getColor(0));
		QCOMPARE(static_cast<const QColor&>(*map.getColor(0)).rgb(), QColor(255, 0, 0).rgb());
		
		// The same pen again; another width; another colour.
		QCOMPARE(strokeAlong(map, 5, 35)->getSymbol(), symbol);
		QCOMPARE(map.getNumSymbols(), 1);
		auto const* wide = strokeAlong(map, 5, 35, 0, 2)->getSymbol();
		QVERIFY(wide != symbol);
		QCOMPARE(wide->asLine()->getLineWidth(), 1000);
		QCOMPARE(map.getNumColors(), 1);
		QVERIFY(strokeAlong(map, 5, 35, 5, 1)->getSymbol() != symbol);
		QCOMPARE(map.getNumColors(), 2);
		QCOMPARE(map.getNumParts(), 2);
		QCOMPARE(part->getNumObjects(), 4);
		
		// Undoing everything removes the strokes and, last, the part.
		for (int i = 0; i < 4; ++i)
			QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumParts(), 1);
		QCOMPARE(sketchPartIndex(map), -1);
	}
	
	/// Accept (2)
	void notPrintedTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		strokeAlong(map, 5, 35, 0, 2);
		auto const on_screen = render(map, RenderConfig::HelperSymbols);
		auto const printed = render(map, RenderConfig::NoOptions);
		QCOMPARE(on_screen.pixelColor(100, 50), QColor(255, 0, 0));
		QCOMPARE(on_screen.pixelColor(100, 20), QColor(Qt::white));
		QCOMPARE(printed.pixelColor(100, 50), QColor(Qt::white));
	}
	
	/// A sketch colour goes to the top of the colour table. The objects which
	/// are there already must keep their colours.
	void keepsColoursTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* green = new MapColor(QStringLiteral("Green"), 0);
		green->setCmyk(MapColorCmyk(1.0f, 0.0f, 1.0f, 0.0f));
		green->setRgbFromCmyk();
		map.addColor(green, 0);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("line"), 1);
		line->setLineWidth(2.0);
		line->setColor(green);
		map.addObject(new PathObject(line, { MapCoord(5, 5), MapCoord(35, 5) }));
		
		auto const before = render(map, RenderConfig::HelperSymbols);
		auto const line_color = before.pixelColor(100, 25);
		QVERIFY(line_color.green() > 200 && line_color.red() < 60);
		
		strokeAlong(map, 5, 35, 0, 2);
		QCOMPARE(map.getColor(0)->getName(), sketchColorName(0));
		auto const after = render(map, RenderConfig::HelperSymbols);
		QCOMPARE(after.pixelColor(100, 25), line_color);
		QCOMPARE(after.pixelColor(100, 50), QColor(255, 0, 0));
	}
	
	/// Undoing a step of the sketch leaves the part one draws into as it was.
	void undoKeepsPartTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		strokeAlong(map, 0, 40);
		QCOMPARE(eraseSketch(map, MapCoordF(10, 10), 2), 1);
		auto const sketch_part = std::size_t(sketchPartIndex(map));
		QVERIFY(map.getCurrentPartIndex() != sketch_part);
		auto const current = map.getCurrentPartIndex();
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getPart(sketch_part)->getNumObjects(), 1);
		QCOMPARE(map.getCurrentPartIndex(), current);
		QCOMPARE(map.getNumSelectedObjects(), 0);
		
		QVERIFY(map.undoManager().redo());
		QCOMPARE(map.getPart(sketch_part)->getNumObjects(), 2);
		QCOMPARE(map.getCurrentPartIndex(), current);
		QCOMPARE(map.getNumSelectedObjects(), 0);
		
		// With the sketch as the current part, the pieces are selected as upstream does it.
		map.setCurrentPartIndex(sketch_part);
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getCurrentPartIndex(), sketch_part);
		QCOMPARE(map.getNumSelectedObjects(), 1);
	}
	
	/// Accept (3)
	void eraserTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto const* stroke = strokeAlong(map, 0, 40);
		auto const uuid = stroke->uuid();
		auto const* part = map.getPart(std::size_t(sketchPartIndex(map)));
		auto const steps = map.undoManager().undoStepCount();
		
		// Nowhere near.
		QCOMPARE(eraseSketch(map, MapCoordF(20, 30), 2), 0);
		QCOMPARE(map.undoManager().undoStepCount(), steps);
		
		// Through the stroke at x = 10: 0 to 8 and 12 to 40 are left.
		QCOMPARE(eraseSketch(map, MapCoordF(10, 10), 2), 1);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(part->getNumObjects(), 2);
		auto const* longer = map.findObject(uuid);
		QVERIFY(longer);
		QVERIFY(near(first(longer), { 12, 10 }));
		QVERIFY(near(last(longer), { 40, 10 }));
		const Object* shorter = nullptr;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			if (part->getObject(i) != longer)
				shorter = part->getObject(i);
		}
		QVERIFY(shorter);
		QVERIFY(near(first(shorter), { 0, 10 }));
		QVERIFY(near(last(shorter), { 8, 10 }));
		QCOMPARE(shorter->derivedFrom(), uuid);
		QVERIFY(shorter->uuid() != uuid);
		QCOMPARE(shorter->getSymbol(), longer->getSymbol());
		
		// One undo gives the stroke back.
		QVERIFY(map.undoManager().undo());
		QCOMPARE(part->getNumObjects(), 1);
		QVERIFY(near(first(map.findObject(uuid)), { 0, 10 }));
		QVERIFY(near(last(map.findObject(uuid)), { 40, 10 }));
		
		// At an end the stroke gets shorter.
		QCOMPARE(eraseSketch(map, MapCoordF(40, 10), 3), 1);
		QCOMPARE(part->getNumObjects(), 1);
		QVERIFY(near(last(map.findObject(uuid)), { 37, 10 }));
		
		// A circle over all of it removes it.
		QCOMPARE(eraseSketch(map, MapCoordF(18, 10), 30), 1);
		QCOMPARE(part->getNumObjects(), 0);
		
		// A path in the map part is no sketch.
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Line"), 1);
		auto* object = new PathObject(line, MapCoordVector{ MapCoord(0.0, 10.0), MapCoord(40.0, 10.0) });
		map.addObject(object, 0);
		QCOMPARE(eraseSketch(map, MapCoordF(10, 10), 2), 0);
		QCOMPARE(map.getPart(0)->getNumObjects(), 1);
	}
	
	/// Accept (4), (5)
	void noteAndItemsTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		strokeAlong(map, 0, 40);
		auto const group = addSketchNote(map, MapCoordF(10, 30), QStringLiteral("Fence gone"), 3);
		QVERIFY(group != 0);
		auto const* part = map.getPart(std::size_t(sketchPartIndex(map)));
		QCOMPARE(part->getNumObjects(), 3);
		QCOMPARE(map.undoManager().undoStepCount(), 2);
		
		auto items = sketchItems(map);
		QCOMPARE(items.size(), std::size_t(2));
		QCOMPARE(items[0].kind, SketchItem::Stroke);
		QCOMPARE(items[0].objects.size(), std::size_t(1));
		QCOMPARE(items[0].label, QStringLiteral("Stroke, 40.0 mm"));
		QVERIFY(items[0].extent.contains(QPointF(20, 10)));
		QCOMPARE(items[1].kind, SketchItem::Note);
		QCOMPARE(items[1].label, QStringLiteral("Fence gone"));
		QCOMPARE(items[1].objects.size(), std::size_t(2));
		auto const* text = items[1].objects[0];
		auto const* marker = items[1].objects[1];
		QCOMPARE(text->getType(), Object::Text);
		QCOMPARE(marker->getType(), Object::Point);
		QCOMPARE(text->group(), group);
		QCOMPARE(marker->group(), group);
		QVERIFY(near(first(marker), { 10, 30 }));
		QVERIFY(near(QPointF(text->asText()->getAnchorCoordF()), { 11.5, 30 }));
		for (auto const* object : { text, marker })
		{
			QVERIFY(object->getSymbol()->isPrivate());
			QVERIFY(object->getSymbol()->isHelperSymbol());
		}
		QVERIFY(items[1].extent.contains(QPointF(10, 30)));
		QCOMPARE(map.getColor(0)->getName(), sketchColorName(3));
		
		// The eraser on the marker takes the whole note.
		QCOMPARE(eraseSketch(map, MapCoordF(10, 30), 1), 2);
		QCOMPARE(part->getNumObjects(), 1);
		QCOMPARE(sketchItems(map).size(), std::size_t(1));
		QVERIFY(map.undoManager().undo());
		QCOMPARE(part->getNumObjects(), 3);
		
		Map empty;
		QVERIFY(sketchItems(empty).empty());
	}
	
	/// Accept (6)
	void convertTest()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* line = addSymbol<LineSymbol>(map, QStringLiteral("Path"), 1);
		auto* area = addSymbol<AreaSymbol>(map, QStringLiteral("Field"), 2);
		auto* label = addSymbol<TextSymbol>(map, QStringLiteral("Label"), 3);
		auto* point = addSymbol<PointSymbol>(map, QStringLiteral("Boulder"), 4);
		strokeAlong(map, 0, 40);
		addSketchStroke(map, { MapCoordF(0, 40), MapCoordF(10, 40), MapCoordF(10, 50) }, 1, 1);
		addSketchNote(map, MapCoordF(10, 30), QStringLiteral("Fence gone"), 3);
		addSketchNote(map, MapCoordF(30, 30), QStringLiteral("Boulder, 2 m"), 3);
		auto const* sketch = map.getPart(std::size_t(sketchPartIndex(map)));
		auto const* content = map.getPart(0);
		QCOMPARE(sketch->getNumObjects(), 6);
		
		// A stroke does not become a text.
		auto items = sketchItems(map);
		QCOMPARE(items.size(), std::size_t(4));
		QString error;
		QVERIFY(!convertSketchItem(map, items[0], label, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(sketch->getNumObjects(), 6);
		
		// Stroke to line: the same object, by its uuid, in the map part.
		auto const stroke_uuid = items[0].objects[0]->uuid();
		auto const steps = map.undoManager().undoStepCount();
		auto const* as_line = convertSketchItem(map, items[0], line, &error);
		QVERIFY2(as_line, qPrintable(error));
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(as_line->getSymbol(), static_cast<const Symbol*>(line));
		QCOMPARE(as_line->uuid(), stroke_uuid);
		QVERIFY(content->findObjectIndex(as_line) >= 0);
		QCOMPARE(sketch->getNumObjects(), 5);
		QVERIFY(near(last(as_line), { 40, 10 }));
		
		// Stroke to area: closed.
		items = sketchItems(map);
		auto const* as_area = convertSketchItem(map, items[0], area, &error);
		QVERIFY2(as_area, qPrintable(error));
		as_area->update();
		QVERIFY(as_area->asPath()->parts().front().isClosed());
		QCOMPARE(as_area->getSymbol(), static_cast<const Symbol*>(area));
		
		// Note to text: the text goes over, the marker goes away.
		items = sketchItems(map);
		QCOMPARE(items.size(), std::size_t(2));
		auto const* as_text = convertSketchItem(map, items[0], label, &error);
		QVERIFY2(as_text, qPrintable(error));
		QCOMPARE(as_text->getType(), Object::Text);
		QCOMPARE(as_text->asText()->getText(), QStringLiteral("Fence gone"));
		QCOMPARE(as_text->getSymbol(), static_cast<const Symbol*>(label));
		QCOMPARE(as_text->group(), 0);
		QCOMPARE(sketch->getNumObjects(), 2);
		
		// Note to point: at the marker, with the text as a tag.
		items = sketchItems(map);
		auto const* as_point = convertSketchItem(map, items[0], point, &error);
		QVERIFY2(as_point, qPrintable(error));
		QCOMPARE(as_point->getType(), Object::Point);
		QVERIFY(near(first(as_point), { 30, 30 }));
		QCOMPARE(as_point->getTag(QStringLiteral("note")), QStringLiteral("Boulder, 2 m"));
		QCOMPARE(sketch->getNumObjects(), 0);
		QCOMPARE(content->getNumObjects(), 4);
		
		// Each conversion is one undo step.
		QVERIFY(map.undoManager().undo());
		QCOMPARE(sketch->getNumObjects(), 2);
		QCOMPARE(content->getNumObjects(), 3);
	}
	
	/// Accept (7)
	void flattenTest()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("sketch.png"));
		Map map;
		map.setScaleDenominator(10000);
		QString error;
		QVERIFY(!flattenSketch(map, path, 254, &error));
		QVERIFY(!error.isEmpty());
		
		strokeAlong(map, 5, 35, 0, 2);
		QVERIFY2(flattenSketch(map, path, 254, &error), qPrintable(error));
		const QImage image(path);
		QVERIFY(!image.isNull());
		QVERIFY(image.hasAlphaChannel());
		// The stroke of 30 by 1 mm, a margin of 1 mm, 10 pixels per mm.
		QVERIFY2(qAbs(image.width() - 330) <= 12, qPrintable(QString::number(image.width())));
		QVERIFY2(qAbs(image.height() - 30) <= 2, qPrintable(QString::number(image.height())));
		QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(255, 0, 0));
		QCOMPARE(image.pixelColor(2, 2).alpha(), 0);
		
		// The world file: 1 m per pixel, y down, the top left corner in metres.
		QFile world(WorldFile::pathForImage(path));
		QVERIFY(world.open(QIODevice::ReadOnly));
		auto const lines = QString::fromLatin1(world.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
		QCOMPARE(lines.size(), 6);
		QCOMPARE(lines[0].trimmed().toDouble(), 1.0);
		QCOMPARE(lines[1].trimmed().toDouble(), 0.0);
		QCOMPARE(lines[2].trimmed().toDouble(), 0.0);
		QCOMPARE(lines[3].trimmed().toDouble(), -1.0);
		QVERIFY2(qAbs(lines[4].trimmed().toDouble() - 35.0) < 6.5, qPrintable(lines[4]));
		QVERIFY2(qAbs(lines[5].trimmed().toDouble() - -85.0) < 6.5, qPrintable(lines[5]));
	}
};


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(SketchTest)
#include "sketch_t.moc"  // IWYU pragma: keep
