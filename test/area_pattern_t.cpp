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
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"

using namespace OpenOrienteering;


/**
 * Tests the irregular area pattern.
 *
 * See the design notes, "Area symbol properties".
 */
class AreaPatternTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void regularTest();
	void variationTest();
	void minimumDistanceTest();
	void shiftedRowsTest();
	void xmlRoundTripTest();
	void ocdRoundTripTest();
	
private:
	struct Fixture
	{
		Map map;
		MapColor* black = nullptr;
		AreaSymbol* area = nullptr;
		PathObject* object = nullptr;
	};
	static void setup(Fixture& f);
	static PathObject* square(Fixture& f, double x0, double y0, double size);
	/** The dot centres of the object's renderables, sorted. */
	static std::vector<QPointF> dots(PathObject& object);
	static double offNode(double value) { return std::abs(value - 5.0 * std::round(value / 5.0)); }
	
	static constexpr double tolerance = 0.01;
};


void AreaPatternTest::initTestCase()
{
	doStaticInitializations();
}

void AreaPatternTest::setup(Fixture& f)
{
	f.black = new MapColor(QStringLiteral("Black"), 0);
	f.black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	f.map.addColor(f.black, 0);
	f.area = new AreaSymbol();
	f.area->setColor(nullptr);
	f.area->setNumberComponent(0, 401);
	f.area->setNumFillPatterns(1);
	auto& pattern = f.area->getFillPattern(0);
	pattern.type = AreaSymbol::FillPattern::PointPattern;
	pattern.line_spacing = 5000;
	pattern.point_distance = 5000;
	pattern.angle = 0.0;
	auto* dot = new PointSymbol();
	dot->setInnerColor(f.black);
	dot->setInnerRadius(300);
	pattern.point = dot;
	f.map.addSymbol(f.area, 0);
	f.object = square(f, 0.0, 0.0, 50.0);
}

PathObject* AreaPatternTest::square(Fixture& f, double x0, double y0, double size)
{
	auto* object = new PathObject(f.area, { MapCoord(x0, y0), MapCoord(x0 + size, y0), MapCoord(x0 + size, y0 + size), MapCoord(x0, y0 + size) });
	object->parts().front().setClosed(true, true);
	f.map.addObject(object);
	return object;
}

std::vector<QPointF> AreaPatternTest::dots(PathObject& object)
{
	object.setOutputDirty();
	object.update();
	std::vector<QPointF> result;
	object.renderables().forEach([&result](const Renderable& renderable) {
		auto const& extent = renderable.getExtent();
		if (std::abs(extent.width() - 0.6) < 1e-6 && std::abs(extent.height() - 0.6) < 1e-6)
			result.push_back(extent.center());
	});
	std::sort(begin(result), end(result), [](const QPointF& a, const QPointF& b) {
		return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
	});
	return result;
}


void AreaPatternTest::regularTest()
{
	Fixture f;
	setup(f);
	auto const placed = dots(*f.object);
	QVERIFY(placed.size() >= 100);
	for (auto const& centre : placed)
		QVERIFY2(offNode(centre.x()) < tolerance && offNode(centre.y()) < tolerance, qPrintable(QStringLiteral("%1 %2").arg(centre.x()).arg(centre.y())));
}

void AreaPatternTest::variationTest()
{
	Fixture f;
	setup(f);
	auto& pattern = f.area->getFillPattern(0);
	pattern.variation_x = 50;
	pattern.variation_y = 50;
	auto const placed = dots(*f.object);
	QCOMPARE(placed.size(), dots(*f.object).size());
	auto displaced = 0;
	for (auto const& centre : placed)
	{
		QVERIFY2(offNode(centre.x()) <= 1.25 + 1e-6 && offNode(centre.y()) <= 1.25 + 1e-6, qPrintable(QStringLiteral("%1 %2").arg(centre.x()).arg(centre.y())));
		displaced += offNode(centre.x()) > 0.1;
	}
	QVERIFY2(3 * displaced >= int(placed.size()), qPrintable(QString::number(displaced)));
	
	// Rendering again gives the same layout.
	auto const again = dots(*f.object);
	QCOMPARE(again.size(), placed.size());
	for (std::size_t i = 0; i < placed.size(); ++i)
		QVERIFY((again[i] - placed[i]).manhattanLength() < 1e-9);
	
	// A second object of the same symbol shares the layout in the shared cells.
	auto* other = square(f, 10.0, 10.0, 50.0);
	auto const other_dots = dots(*other);
	auto shared = 0;
	for (auto const& centre : other_dots)
	{
		if (centre.x() < 12.0 || centre.x() > 48.0 || centre.y() < 12.0 || centre.y() > 48.0)
			continue;
		++shared;
		auto const match = std::find_if(begin(placed), end(placed), [&centre](const QPointF& p) {
			return (p - centre).manhattanLength() < tolerance;
		});
		QVERIFY2(match != end(placed), qPrintable(QStringLiteral("%1 %2").arg(centre.x()).arg(centre.y())));
	}
	QVERIFY(shared >= 40);
}

void AreaPatternTest::minimumDistanceTest()
{
	Fixture f;
	setup(f);
	auto& pattern = f.area->getFillPattern(0);
	// the design notes
	auto const regular = dots(*f.object);
	auto const closest = [](const std::vector<QPointF>& placed) {
		auto result = 1e9;
		for (std::size_t i = 0; i < placed.size(); ++i)
		{
			for (std::size_t j = i + 1; j < placed.size(); ++j)
				result = qMin(result, std::hypot(placed[i].x() - placed[j].x(), placed[i].y() - placed[j].y()));
		}
		return result;
	};
	auto const farthest = [](const std::vector<QPointF>& placed) {
		auto result = 0.0;
		for (auto const& centre : placed)
			result = qMax(result, qMax(offNode(centre.x()), offNode(centre.y())));
		return result;
	};
	
	// The full variation: the dots of 0.6 mm in cells of 5 mm never touch, and none is left out.
	pattern.variation_x = 100;
	pattern.variation_y = 100;
	auto const unrestricted = dots(*f.object);
	QCOMPARE(unrestricted.size(), regular.size());
	QVERIFY(farthest(unrestricted) <= 2.2 + 1e-6);
	QVERIFY(farthest(unrestricted) > 2.0);
	QVERIFY(closest(unrestricted) >= 0.6 - 1e-6);
	
	// A minimum distance is the clear space between two elements: 4 mm between dots of 0.6 mm leave each
	// dot 0.2 mm of play about its node, and still none is left out.
	pattern.minimum_distance = 4000;
	auto const placed = dots(*f.object);
	QCOMPARE(placed.size(), regular.size());
	QVERIFY(farthest(placed) <= 0.2 + 1e-6);
	QVERIFY(farthest(placed) > 0.18);
	QVERIFY(closest(placed) >= 4.6 - 1e-6);
	auto displaced = 0;
	for (auto const& centre : placed)
		displaced += offNode(centre.x()) > 0.02;
	QVERIFY(3 * displaced >= int(placed.size()));
	
	// A distance which leaves no play gives the regular pattern.
	pattern.minimum_distance = 4400;
	QVERIFY(farthest(dots(*f.object)) < 1e-6);
	pattern.minimum_distance = 9000;
	QVERIFY(farthest(dots(*f.object)) < 1e-6);
	
	// One direction alone.
	pattern.minimum_distance = 0;
	pattern.variation_y = 0;
	for (auto const& centre : dots(*f.object))
		QVERIFY(offNode(centre.y()) < 1e-6);
}

void AreaPatternTest::shiftedRowsTest()
{
	// Shifted rows are two patterns of twice the row spacing: an element keeps its distance from those of
	// the other pattern too.
	Fixture f;
	setup(f);
	f.area->setNumFillPatterns(2);
	auto& first = f.area->getFillPattern(0);
	first.line_spacing = 10000;
	first.variation_x = 100;
	first.variation_y = 100;
	first.minimum_distance = 1000;
	auto& second = f.area->getFillPattern(1);
	second = first;
	second.point = duplicate(*first.point).release();
	second.line_offset = 5000;
	second.offset_along_line = 2500;
	auto const placed = dots(*f.object);
	QVERIFY(placed.size() >= 100);
	auto closest = 1e9;
	auto off_row = 0.0;
	for (std::size_t i = 0; i < placed.size(); ++i)
	{
		off_row = qMax(off_row, offNode(placed[i].y()));
		for (std::size_t j = i + 1; j < placed.size(); ++j)
			closest = qMin(closest, std::hypot(placed[i].x() - placed[j].x(), placed[i].y() - placed[j].y()));
	}
	// The rows are 5 mm apart: 1.6 mm are taken by a dot and the distance, half the rest is the play.
	QVERIFY2(closest >= 1.6 - 1e-6, qPrintable(QString::number(closest)));
	QVERIFY2(off_row <= 1.7 + 1e-6 && off_row > 1.5, qPrintable(QString::number(off_row)));
}

void AreaPatternTest::xmlRoundTripTest()
{
	Fixture f;
	setup(f);
	auto& pattern = f.area->getFillPattern(0);
	pattern.variation_x = 30;
	pattern.variation_y = 60;
	pattern.minimum_distance = 2000;
	
	auto const* format = FileFormats.findFormat("XML");
	QVERIFY(format);
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &f.map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	QVERIFY(QString::fromUtf8(buffer.data()).contains(QLatin1String("variation_x")));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	auto const& loaded = reloaded.getSymbol(0)->asArea()->getFillPattern(0);
	QCOMPARE(loaded.variation_x, 30);
	QCOMPARE(loaded.variation_y, 60);
	QCOMPARE(loaded.minimum_distance, 2000);
	QVERIFY(reloaded.getSymbol(0)->equals(f.area));
	
	Fixture plain;
	setup(plain);
	QBuffer plain_buffer;
	QVERIFY(plain_buffer.open(QIODevice::ReadWrite));
	auto plain_exporter = format->makeExporter({}, &plain.map, nullptr);
	plain_exporter->setDevice(&plain_buffer);
	QVERIFY(plain_exporter->doExport());
	QVERIFY(!QString::fromUtf8(plain_buffer.data()).contains(QLatin1String("variation")));
}

void AreaPatternTest::ocdRoundTripTest()
{
	Fixture f;
	setup(f);
	auto& pattern = f.area->getFillPattern(0);
	pattern.variation_x = 30;
	pattern.variation_y = 60;
	pattern.minimum_distance = 2000;
	
	auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(12));
	QVERIFY(format);
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &f.map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	for (auto const& warning : exporter->warnings())
		qInfo("export warning: %s", qPrintable(warning));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	for (auto const& warning : importer->warnings())
		qInfo("import warning: %s", qPrintable(warning));
	const AreaSymbol* loaded = nullptr;
	for (int i = 0; i < reloaded.getNumSymbols() && !loaded; ++i)
	{
		if (reloaded.getSymbol(i)->getType() == Symbol::Area)
			loaded = reloaded.getSymbol(i)->asArea();
	}
	QVERIFY(loaded);
	QVERIFY(loaded->getNumFillPatterns() >= 1);
	auto const& loaded_pattern = loaded->getFillPattern(0);
	QCOMPARE(loaded_pattern.type, AreaSymbol::FillPattern::PointPattern);
	QCOMPARE(loaded_pattern.variation_x, 30);
	QCOMPARE(loaded_pattern.variation_y, 60);
	QCOMPARE(loaded_pattern.minimum_distance, 2000);
}


QTEST_MAIN(AreaPatternTest)
#include "area_pattern_t.moc"  // IWYU pragma: keep
