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
#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"

using namespace OpenOrienteering;


/**
 * Tests the taper and the secondary mid symbol of line symbols.
 *
 * See the design notes, "Line symbol properties".
 */
class LineTaperTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void placementTest();
	void taperTest();
	void spacingTest();
	void secondaryTest();
	void widthTest();
	void xmlRoundTripTest();
	void ocdRoundTripTest();
	
private:
	struct Fixture
	{
		Map map;
		MapColor* black = nullptr;
		LineSymbol* line = nullptr;
		PointSymbol* dot = nullptr;
		PathObject* object = nullptr;
	};
	static void setup(Fixture& f, bool with_dot = true);
	/** The dots of the object's renderables as (centre x, diameter), sorted by x. */
	static std::vector<std::pair<double, double>> dots(PathObject& object);
	static QImage render(Map& map);
	static int inkColumn(const QImage& image, double x_mm);
	
	static constexpr double tolerance = 0.01;
};


void LineTaperTest::initTestCase()
{
	doStaticInitializations();
}

void LineTaperTest::setup(Fixture& f, bool with_dot)
{
	f.black = new MapColor(QStringLiteral("Black"), 0);
	f.black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	f.map.addColor(f.black, 0);
	f.line = new LineSymbol();
	f.line->setColor(f.black);
	f.line->setLineWidth(0.5);
	f.line->setNumberComponent(0, 501);
	if (with_dot)
	{
		f.dot = new PointSymbol();
		f.dot->setInnerColor(f.black);
		f.dot->setInnerRadius(1000);
		f.line->setMidSymbol(f.dot);
		f.line->setMidSymbolsPerSpot(1);
		f.line->setSegmentLength(10000);
		f.line->setEndLength(0);
	}
	f.map.addSymbol(f.line, 0);
	f.object = new PathObject(f.line, { MapCoord(0.0, 0.0), MapCoord(60.0, 0.0) });
	f.map.addObject(f.object);
}

std::vector<std::pair<double, double>> LineTaperTest::dots(PathObject& object)
{
	object.setOutputDirty();  // the map updated the object when it was added, before the symbol was changed
	object.update();
	std::vector<std::pair<double, double>> result;
	object.renderables().forEach([&result](const Renderable& renderable) {
		auto const& extent = renderable.getExtent();
		if (extent.width() < 5.0 && std::abs(extent.width() - extent.height()) < 1e-6)
			result.emplace_back(extent.center().x(), extent.width());
	});
	std::sort(begin(result), end(result));
	return result;
}

QImage LineTaperTest::render(Map& map)
{
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setResolution(300);
	printer.setPrintArea(QRectF(0.0, -5.0, 60.0, 10.0));
	auto const pixel_per_mm = 300 / 25.4;
	QImage image(qRound(60.0 * pixel_per_mm), qRound(10.0 * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image, RenderConfig::NoOptions);
	}
	return image;
}

int LineTaperTest::inkColumn(const QImage& image, double x_mm)
{
	auto const x = qRound(x_mm * 300 / 25.4);
	auto count = 0;
	for (int y = 0; y < image.height(); ++y)
		count += qGray(image.pixel(x, y)) < 128;
	return count;
}


void LineTaperTest::placementTest()
{
	Fixture f;
	setup(f);
	auto const placed = dots(*f.object);
	QCOMPARE(placed.size(), std::size_t(7));
	for (std::size_t k = 0; k < placed.size(); ++k)
	{
		QVERIFY2(std::abs(placed[k].first - 10.0 * k) < tolerance, qPrintable(QString::number(placed[k].first)));
		QVERIFY(std::abs(placed[k].second - 2.0) < tolerance);
	}
}

void LineTaperTest::taperTest()
{
	Fixture f;
	setup(f);
	f.line->setTaperEndPercent(20);
	
	f.line->setTaperMode(LineSymbol::TaperTowardEnd);
	auto placed = dots(*f.object);
	QCOMPARE(placed.size(), std::size_t(7));
	for (std::size_t k = 0; k < 7; ++k)
		QVERIFY2(std::abs(placed[k].second - 2.0 * (1.0 - 0.8 * k / 6.0)) < tolerance, qPrintable(QString::number(placed[k].second)));
	
	f.line->setTaperMode(LineSymbol::TaperTowardStart);
	placed = dots(*f.object);
	for (std::size_t k = 0; k < 7; ++k)
		QVERIFY(std::abs(placed[k].second - 2.0 * (1.0 - 0.8 * (6 - k) / 6.0)) < tolerance);
	
	f.line->setTaperMode(LineSymbol::TaperBothEnds);
	placed = dots(*f.object);
	for (std::size_t k = 0; k < 7; ++k)
		QVERIFY2(std::abs(placed[k].second - 2.0 * (0.2 + 0.8 * std::min(k, 6 - k) / 3.0)) < tolerance, qPrintable(QString::number(placed[k].second)));
}

void LineTaperTest::spacingTest()
{
	Fixture f;
	setup(f);
	f.line->setTaperMode(LineSymbol::TaperTowardEnd);
	f.line->setTaperEndPercent(20);
	f.line->setTaperSpacing(true);
	auto const placed = dots(*f.object);
	QCOMPARE(placed.size(), std::size_t(7));
	double s[7];
	for (int k = 0; k < 7; ++k)
		s[k] = 1.0 - 0.8 * k / 6.0;
	auto total = 0.0;
	for (int k = 0; k < 6; ++k)
		total += s[k] + s[k + 1];
	auto accumulated = 0.0;
	for (int k = 0; k < 7; ++k)
	{
		auto const expected = 60.0 * accumulated / total;
		QVERIFY2(std::abs(placed[std::size_t(k)].first - expected) < tolerance,
		         qPrintable(QStringLiteral("%1 vs %2").arg(placed[std::size_t(k)].first).arg(expected)));
		if (k < 6)
			accumulated += s[k] + s[k + 1];
	}
	QVERIFY(std::abs(placed.back().first - 60.0) < tolerance);
}

void LineTaperTest::secondaryTest()
{
	Fixture f;
	setup(f);
	auto* small = new PointSymbol();
	small->setInnerColor(f.black);
	small->setInnerRadius(500);
	f.line->setMidSymbol2(small);
	auto const placed = dots(*f.object);
	QCOMPARE(placed.size(), std::size_t(7));
	for (std::size_t k = 0; k < 7; ++k)
		QVERIFY2(std::abs(placed[k].second - (k % 2 ? 1.0 : 2.0)) < tolerance, qPrintable(QString::number(placed[k].second)));
}

void LineTaperTest::widthTest()
{
	Fixture f;
	setup(f, false);
	f.line->setLineWidth(2.0);
	f.line->setTaperMode(LineSymbol::TaperTowardEnd);
	f.line->setTaperEndPercent(20);
	f.object->setOutputDirty();
	
	auto image = render(f.map);
	auto start = inkColumn(image, 3.0);
	auto finish = inkColumn(image, 57.0);
	qInfo("plain stroke: %d px at 3 mm, %d px at 57 mm", start, finish);
	QVERIFY(start >= 20 && start <= 26);
	QVERIFY(finish >= 20 && finish <= 26);
	
	f.line->setTaperWidth(true);
	f.object->setOutputDirty();
	image = render(f.map);
	start = inkColumn(image, 3.0);
	finish = inkColumn(image, 57.0);
	qInfo("tapered stroke: %d px at 3 mm, %d px at 57 mm", start, finish);
	QVERIFY(start >= 20 && start <= 26);
	QVERIFY(finish >= 3 && finish <= 9);
}

void LineTaperTest::xmlRoundTripTest()
{
	Fixture f;
	setup(f);
	f.line->setTaperMode(LineSymbol::TaperBothEnds);
	f.line->setTaperEndPercent(30);
	f.line->setTaperSpacing(true);
	f.line->setTaperWidth(true);
	auto* small = new PointSymbol();
	small->setInnerColor(f.black);
	small->setInnerRadius(500);
	f.line->setMidSymbol2(small);
	f.line->setPreferredTool(QStringLiteral("drawrectangle"));
	
	auto const* format = FileFormats.findFormat("XML");
	QVERIFY(format);
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &f.map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	QCOMPARE(reloaded.getNumSymbols(), 1);
	auto const* loaded = reloaded.getSymbol(0)->asLine();
	QCOMPARE(loaded->getTaperMode(), int(LineSymbol::TaperBothEnds));
	QCOMPARE(loaded->getTaperEndPercent(), 30);
	QVERIFY(loaded->tapersSpacing() && loaded->tapersWidth());
	QVERIFY(loaded->getMidSymbol2());
	QCOMPARE(loaded->getMidSymbol2()->getInnerRadius(), 500);
	QCOMPARE(loaded->getPreferredTool(), QStringLiteral("drawrectangle"));
	QVERIFY(loaded->equals(f.line));
	
	Fixture plain;
	setup(plain);
	QBuffer plain_buffer;
	QVERIFY(plain_buffer.open(QIODevice::ReadWrite));
	auto plain_exporter = format->makeExporter({}, &plain.map, nullptr);
	plain_exporter->setDevice(&plain_buffer);
	QVERIFY(plain_exporter->doExport());
	auto const xml = QString::fromUtf8(plain_buffer.data());
	for (auto const* attribute : { "taper_mode", "taper_end_percent", "preferred_tool", "mid_symbol_2" })
		QVERIFY2(!xml.contains(QLatin1String(attribute)), attribute);
}

void LineTaperTest::ocdRoundTripTest()
{
	auto const round_trip = [](Fixture& f, Map& reloaded, std::vector<QString>& export_warnings, std::vector<QString>& import_warnings) {
		auto const* format = FileFormats.findFormat(OcdFileFormat::idForVersion(12));
		if (!format)
			return false;
		QBuffer buffer;
		if (!buffer.open(QIODevice::ReadWrite))
			return false;
		auto exporter = format->makeExporter({}, &f.map, nullptr);
		exporter->setDevice(&buffer);
		if (!exporter->doExport())
			return false;
		export_warnings = exporter->warnings();
		buffer.seek(0);
		auto importer = format->makeImporter({}, &reloaded, nullptr);
		importer->setDevice(&buffer);
		if (!importer->doImport())
			return false;
		import_warnings = importer->warnings();
		return reloaded.getNumSymbols() >= 1 && reloaded.getSymbol(0)->getType() == Symbol::Line;
	};
	auto const has = [](const std::vector<QString>& warnings, const char* phrase) {
		for (auto const& warning : warnings)
		{
			qInfo("warning: %s", qPrintable(warning));
			if (warning.contains(QLatin1String(phrase)))
				return true;
		}
		return false;
	};
	std::vector<QString> export_warnings;
	std::vector<QString> import_warnings;
	
	Fixture f;
	setup(f);
	f.line->setTaperMode(LineSymbol::TaperTowardEnd);
	f.line->setTaperEndPercent(20);
	auto* small = new PointSymbol();
	small->setInnerColor(f.black);
	small->setInnerRadius(500);
	f.line->setMidSymbol2(small);
	Map reloaded;
	QVERIFY(round_trip(f, reloaded, export_warnings, import_warnings));
	QVERIFY(!has(import_warnings, "secondary"));
	auto const* loaded = reloaded.getSymbol(0)->asLine();
	QCOMPARE(loaded->getTaperMode(), int(LineSymbol::TaperTowardEnd));
	QCOMPARE(loaded->getTaperEndPercent(), 20);
	QVERIFY(loaded->getMidSymbol() && !loaded->getMidSymbol()->isEmpty());
	QVERIFY(loaded->getMidSymbol2() && !loaded->getMidSymbol2()->isEmpty());
	QCOMPARE(loaded->getMidSymbol2()->getInnerRadius(), 500);
	
	Fixture both;
	setup(both);
	both.line->setTaperMode(LineSymbol::TaperBothEnds);
	both.line->setTaperEndPercent(40);
	Map reloaded_both;
	QVERIFY(round_trip(both, reloaded_both, export_warnings, import_warnings));
	QCOMPARE(reloaded_both.getSymbol(0)->asLine()->getTaperMode(), int(LineSymbol::TaperBothEnds));
	QCOMPARE(reloaded_both.getSymbol(0)->asLine()->getTaperEndPercent(), 40);
	
	Fixture start;
	setup(start);
	start.line->setTaperMode(LineSymbol::TaperTowardStart);
	Map reloaded_start;
	QVERIFY(round_trip(start, reloaded_start, export_warnings, import_warnings));
	QVERIFY(has(export_warnings, "toward the start"));
	QCOMPARE(reloaded_start.getSymbol(0)->asLine()->getTaperMode(), int(LineSymbol::TaperTowardEnd));
}


QTEST_MAIN(LineTaperTest)
#include "line_taper_t.moc"  // IWYU pragma: keep
