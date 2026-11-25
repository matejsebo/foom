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
#include <QtEndian>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPoint>
#include <QString>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/ocd_sketch.h"
#include "sketch/sketch.h"

using namespace OpenOrienteering;


/**
 * Tests the import of the sketch strokes of OCD files.
 *
 * See the design notes, "Sketch strokes in OCD files".
 */
class OcdSketchTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void readTest();
	void damagedTest();
	void importTest();
	void penTest();

private:
	struct Stroke
	{
		QColor color;
		int width;
		std::vector<QPoint> points;  // 1/100 mm, y up
	};
	/** An OCD 2018 file of a map with one line object, as FOOM writes it. */
	static QByteArray plainFile();
	/** Appends a sketch block of one layer, as the format has it, and sets its place in the file header. */
	static void appendSketch(QByteArray& file, const QString& layer, const std::vector<Stroke>& strokes);
	static void put32(QByteArray& bytes, int at, quint32 value) { qToLittleEndian(value, reinterpret_cast<uchar*>(bytes.data()) + at); }
};


void OcdSketchTest::initTestCase()
{
	doStaticInitializations();
}

QByteArray OcdSketchTest::plainFile()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setColor(black);
	line->setLineWidth(0.2);
	line->setNumberComponent(0, 101);
	map.addSymbol(line, 0);
	map.addObject(new PathObject(line, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
	auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(2018));
	Q_ASSERT(format);
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	exporter->doExport();
	return buffer.data();
}

void OcdSketchTest::appendSketch(QByteArray& file, const QString& layer, const std::vector<Stroke>& strokes)
{
	// The sizes and places are those of a file with two strokes.
	auto const header = file.size();
	file.append(QByteArray(22, '\0'));
	file[header] = 1;
	file[header + 1] = 1;
	auto const layer_at = file.size();
	put32(file, header + 18, quint32(layer_at));
	file.append(QByteArray(550, '\0'));
	put32(file, layer_at + 4, 1);
	auto const name = layer.leftJustified(256, QLatin1Char(' '));
	for (int i = 0; i < 256; ++i)
		qToLittleEndian(quint16(name.at(i).unicode()), reinterpret_cast<uchar*>(file.data()) + layer_at + 8 + 2 * i);
	auto const index_at = file.size();
	put32(file, layer_at + 546, quint32(index_at));
	file.append(QByteArray(12 + 8 * int(strokes.size()), '\0'));
	put32(file, index_at + 8, quint32(strokes.size()));
	for (std::size_t k = 0; k < strokes.size(); ++k)
	{
		auto const& stroke = strokes[k];
		auto const at = file.size();
		auto const size = 56 + 8 * int(stroke.points.size());
		put32(file, index_at + 12 + 8 * int(k), quint32(at));
		put32(file, index_at + 16 + 8 * int(k), quint32(size));
		file.append(QByteArray(size, '\0'));
		file[at] = char(stroke.color.red());
		file[at + 1] = char(stroke.color.green());
		file[at + 2] = char(stroke.color.blue());
		put32(file, at + 4, quint32(stroke.width));
		put32(file, at + 40, quint32(stroke.points.size()));
		put32(file, at + 44, 1);
		for (std::size_t i = 0; i < stroke.points.size(); ++i)
		{
			put32(file, at + 56 + 8 * int(i), quint32(stroke.points[i].x()) << 8);
			put32(file, at + 60 + 8 * int(i), quint32(stroke.points[i].y()) << 8);
		}
	}
	put32(file, ocd_sketch_header_field, quint32(header));
}


void OcdSketchTest::readTest()
{
	auto file = plainFile();
	QVERIFY(file.size() > 100);
	auto damaged = true;
	QVERIFY(readOcdSketch(file, &damaged).empty());
	QVERIFY(!damaged);
	
	// The two strokes: black of 2.13 mm with eight points, red of 5.96 mm with seven.
	appendSketch(file, QStringLiteral("Sketch-1"), {
	    { QColor(0, 0, 0), 213, { { -31203, -21262 }, { -27501, -22751 }, { -24736, -23857 }, { -20000, -23000 }, { -18000, -22000 }, { -16000, -21500 }, { -15000, -21300 }, { -14000, -21200 } } },
	    { QColor(255, 0, 0), 596, { { -31203, -28707 }, { -27076, -28707 }, { -24098, -28707 }, { -22000, -28707 }, { -21000, -28000 }, { -20500, -27500 }, { -19759, -28412 } } },
	});
	auto const strokes = readOcdSketch(file, &damaged);
	QVERIFY(!damaged);
	QCOMPARE(int(strokes.size()), 2);
	QCOMPARE(strokes[0].layer, QStringLiteral("Sketch-1"));
	QCOMPARE(strokes[0].color, QColor(0, 0, 0));
	QCOMPARE(strokes[0].width, 213);
	QCOMPARE(int(strokes[0].points.size()), 8);
	QCOMPARE(strokes[0].points[1].x() >> 8, -27501);
	QCOMPARE(strokes[0].points[1].y() >> 8, -22751);
	QCOMPARE(strokes[1].color, QColor(255, 0, 0));
	QCOMPARE(strokes[1].width, 596);
	QCOMPARE(int(strokes[1].points.size()), 7);
}

void OcdSketchTest::damagedTest()
{
	// A block which points out of the file gives what can be read and says so; nothing crashes.
	auto file = plainFile();
	appendSketch(file, QStringLiteral("Sketch-1"), { { QColor(0, 0, 255), 100, { { 0, 0 }, { 1000, 0 } } }, { QColor(0, 0, 255), 100, { { 0, 100 }, { 1000, 100 } } } });
	auto damaged = false;
	auto cut = file.left(file.size() - 40);  // the second stroke is cut off
	QCOMPARE(int(readOcdSketch(cut, &damaged).size()), 1);
	QVERIFY(damaged);
	
	auto lost = file;
	put32(lost, ocd_sketch_header_field, quint32(file.size() + 1000));
	QVERIFY(readOcdSketch(lost, &damaged).empty());
	QVERIFY(damaged);
	
	// A layer which names itself as the next one ends the reading.
	auto loop = file;
	auto const header = int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(loop.constData()) + ocd_sketch_header_field));
	auto const layer = int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(loop.constData()) + header + 18));
	put32(loop, layer, quint32(layer));
	readOcdSketch(loop, &damaged);
	QVERIFY(damaged);
}

void OcdSketchTest::importTest()
{
	auto file = plainFile();
	appendSketch(file, QStringLiteral("Sketch-1"), {
	    { QColor(0, 0, 0), 213, { { 1000, -2000 }, { 2000, -2500 }, { 3000, -2000 } } },
	    { QColor(255, 0, 0), 596, { { 1000, -4000 }, { 3000, -4000 } } },
	    { QColor(255, 0, 0), 596, { { 1000, -5000 }, { 3000, -5000 } } },
	    { QColor(18, 52, 86), 50, { { 1000, -6000 }, { 3000, -6000 } } },
	    { QColor(0, 0, 0), 213, { { 500, 500 } } },  // one point: no stroke
	});
	auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(2018));
	QVERIFY(format);
	QBuffer buffer(&file);
	QVERIFY(buffer.open(QIODevice::ReadOnly));
	Map map;
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	for (auto const& warning : importer->warnings())
		qInfo("import warning: %s", qPrintable(warning));
	
	// The map part keeps its object, and the strokes stand in a part of the role sketch.
	auto const sketch_index = sketchPartIndex(map);
	QVERIFY(sketch_index > 0);
	QCOMPARE(map.getPart(0)->getNumObjects(), 1);
	auto const* part = map.getPart(std::size_t(sketch_index));
	QCOMPARE(part->getNumObjects(), 4);
	
	auto const* black = part->getObject(0);
	QCOMPARE(black->getType(), Object::Path);
	QCOMPARE(int(black->getRawCoordinateVector().size()), 3);
	// 1/100 mm with y up to mm with y down
	QVERIFY(black->getRawCoordinateVector()[1].isPositionEqualTo(MapCoord(20.0, 25.0)));
	auto const* black_symbol = black->getSymbol()->asLine();
	QVERIFY(black_symbol && black_symbol->isPrivate() && black_symbol->isHelperSymbol());
	QCOMPARE(black_symbol->getLineWidth(), 2130);
	QCOMPARE(black_symbol->getColor()->getName(), sketchColorName(7));
	
	// Two strokes of one pen share its symbol; its colour is the sketch red, its width the file's.
	auto const* red_symbol = part->getObject(1)->getSymbol()->asLine();
	QCOMPARE(part->getObject(2)->getSymbol(), static_cast<const Symbol*>(red_symbol));
	QCOMPARE(red_symbol->getLineWidth(), 5960);
	QCOMPARE(red_symbol->getColor()->getName(), sketchColorName(0));
	
	// A colour which is no pen's gets a colour of its own, named after its values.
	auto const* other_symbol = part->getObject(3)->getSymbol()->asLine();
	QCOMPARE(other_symbol->getLineWidth(), 500);
	QVERIFY(other_symbol->getColor()->getName().contains(QLatin1String("R18 G52 B86")));
	QCOMPARE(QColor(QRgb(*other_symbol->getColor())).rgb(), QColor(18, 52, 86).rgb());
	
	// The list of the sketch has the four strokes.
	QCOMPARE(int(sketchItems(map).size()), 4);
}

void OcdSketchTest::penTest()
{
	// A pen of FOOM's own colour and width is FOOM's pen: the stroke takes the symbol which the sketch tool makes.
	Map map;
	auto const* pen = sketchSymbol(map, 2, 1);
	auto const* same = sketchSymbol(map, sketchColorRgb(2), sketchWidth(1));
	QCOMPARE(same, pen);
	auto const* wider = sketchSymbol(map, sketchColorRgb(2), 3.3);
	QVERIFY(wider != pen);
	QCOMPARE(wider->asLine()->getColor(), pen->asLine()->getColor());
	QCOMPARE(sketchSymbol(map, sketchColorRgb(2), 3.3), wider);
}


QTEST_MAIN(OcdSketchTest)
#include "ocd_sketch_t.moc"  // IWYU pragma: keep
