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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QObject>
#include <QPageSize>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gdal.h>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/print_job.h"
#include "core/print_output.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"

using namespace OpenOrienteering;


namespace {

MapColor* addColor(Map& map, const QString& name, const MapColorCmyk& cmyk, const QString& spot_name = {})
{
	auto* color = new MapColor(name, map.getNumColors());
	if (!spot_name.isEmpty())
		color->setSpotColorName(spot_name);
	color->setCmyk(cmyk);
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

AreaSymbol* addArea(Map& map, const QString& name, int number, const MapColor* color)
{
	auto* symbol = new AreaSymbol();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	symbol->setColor(color);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

Object* addSquare(Map& map, const Symbol* symbol, const QRectF& box, int part = -1)
{
	MapCoordVector coords { MapCoord(box.topLeft()), MapCoord(box.topRight()), MapCoord(box.bottomRight()), MapCoord(box.bottomLeft()), MapCoord(box.topLeft()) };
	coords.back().setClosePoint(true);
	auto* object = new PathObject(symbol, coords);
	map.addObject(object, part);
	return object;
}

/**
 * A 1:10,000 map with squares in the area from (0, 0) to (20, 10) mm:
 * full brown at x 1..5, 50 % brown at 6..10, black over brown at 11..15,
 * a half transparent colour at 16..19. All from y = 1 to 5.
 */
void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = addColor(map, QStringLiteral("Black"), { 0.0f, 0.0f, 0.0f, 1.0f }, QStringLiteral("BLACK"));
	black->setKnockout(true);
	auto* brown = addColor(map, QStringLiteral("Brown"), { 0.0f, 0.56f, 1.0f, 0.18f }, QStringLiteral("BROWN"));
	auto* half = new MapColor(QStringLiteral("Brown 50 %"), map.getNumColors());
	half->setSpotColorComposition({ SpotColorComponent(brown, 0.5f) });
	half->setCmykFromSpotColors();
	half->setRgbFromSpotColors();
	map.addColor(half, map.getNumColors());
	auto* pale = addColor(map, QStringLiteral("Pale"), { 0.4f, 0.0f, 0.0f, 0.2f });
	pale->setOpacity(0.5f);
	
	addSquare(map, addArea(map, QStringLiteral("Brown area"), 1, brown), { 1, 1, 4, 4 });
	addSquare(map, addArea(map, QStringLiteral("Half area"), 2, half), { 6, 1, 4, 4 });
	addSquare(map, map.getSymbol(0), { 11, 1, 4, 4 });
	addSquare(map, addArea(map, QStringLiteral("Black area"), 3, black), { 12, 2, 2, 2 });
	addSquare(map, addArea(map, QStringLiteral("Pale area"), 4, pale), { 16, 1, 3, 4 });
}

const QRectF area(0, 0, 20, 10);
constexpr auto dpi = 254.0;  // 10 pixels per mm

int ink(const RasterPlate& plate, int x, int y)
{
	return plate.ink.constScanLine(y)[x];
}

const RasterPlate* plateNamed(const std::vector<RasterPlate>& plates, const QString& name)
{
	for (auto const& plate : plates)
	{
		if (plate.name == name)
			return &plate;
	}
	return nullptr;
}

/// The share of black pixels of a 1-bit image.
double blackShare(const QImage& image)
{
	auto black = 0.0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
			black += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
	}
	return black / (double(image.width()) * image.height());
}

QImage uniformPlate(int value)
{
	QImage plate(600, 600, QImage::Format_Grayscale8);
	plate.fill(value);
	return plate;
}

/// The page sizes of a PDF written by Qt, in points, rounded.
std::vector<QSize> pdfPages(const QString& path)
{
	std::vector<QSize> pages;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return pages;
	auto const pdf = file.readAll();
	QRegularExpression box(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]"));
	auto matches = box.globalMatch(QString::fromLatin1(pdf.constData(), pdf.size()));
	while (matches.hasNext())
	{
		auto const match = matches.next();
		pages.emplace_back(qRound(match.captured(1).toDouble()), qRound(match.captured(2).toDouble()));
	}
	return pages;
}

QJsonObject step(const char* name, const QString& path)
{
	return QJsonObject{ { QStringLiteral("step"), QLatin1String(name) }, { QStringLiteral("path"), path } };
}

}  // namespace


/**
 * @test Tests the hidden content list, the raster plates, the CMYK raster
 *       and the print job.
 *
 * the design notes, "Print and raster output".
 */
class PrintOutputTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	bool saveMap(const Map& map, const QString& path) const
	{
		XMLFileFormat format;
		auto exporter = format.makeExporter(path, &map, nullptr);
		return exporter && exporter->doExport();
	}
	
	/// Runs the steps with the runner. Returns its exit code, or -1.
	int runJob(const QJsonObject& job, const QString& job_path, QByteArray* output = nullptr) const
	{
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			job_file.write(QJsonDocument(job).toJson());
		}
		return runJobFile(job_path, output);
	}
	
	int runJobFile(const QString& job_path, QByteArray* output = nullptr) const
	{
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return -1;
		if (output)
		{
			*output = process.readAllStandardOutput();
			*output += process.readAllStandardError();
		}
		return process.exitCode();
	}
	
	QJsonObject jobOf(const QString& map_path, QJsonObject export_step) const
	{
		export_step.insert(QStringLiteral("step"), QStringLiteral("export"));
		return QJsonObject{ { QStringLiteral("steps"), QJsonArray{ step("open", map_path), export_step } } };
	}
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	}
	
	/// Accept (1)
	void hiddenContentTest()
	{
		Map map;
		setupMap(map);
		QVERIFY(hiddenContent(map).isEmpty());
		
		auto const* brown = map.getColor(1);
		auto* used = addArea(map, QStringLiteral("Alpha"), 11, brown);
		used->setHidden(true);
		addSquare(map, used, { 1, 6, 1, 1 });
		addSquare(map, used, { 3, 6, 1, 1 });
		auto* unused = addArea(map, QStringLiteral("Beta"), 12, brown);
		unused->setHidden(true);
		auto* helper = addArea(map, QStringLiteral("Gamma"), 13, brown);
		helper->setHidden(true);
		helper->setIsHelperSymbol(true);
		addSquare(map, helper, { 5, 6, 1, 1 });
		
		auto* sketch = new MapPart(QStringLiteral("Sketch"), &map);
		map.addPart(sketch, std::size_t(map.getNumParts()));
		auto const sketch_index = map.findPartIndex(sketch);
		for (int i = 0; i < 3; ++i)
			addSquare(map, map.getSymbol(0), { 7.0 + 2 * i, 6, 1, 1 }, sketch_index);
		sketch->setVisible(false);
		
		auto const content = hiddenContent(map);
		QVERIFY(!content.isEmpty());
		QCOMPARE(content.symbols.size(), std::size_t(1));
		QCOMPARE(content.symbols[0].name, QStringLiteral("11 Alpha"));
		QCOMPARE(content.symbols[0].objects, 2);
		QCOMPARE(content.parts.size(), std::size_t(1));
		QCOMPARE(content.parts[0].name, QStringLiteral("Sketch"));
		QCOMPARE(content.parts[0].objects, 3);
		auto const lines = content.lines();
		QCOMPARE(lines.size(), 2);
		QVERIFY(lines[0].contains(QLatin1String("11 Alpha")) && lines[0].contains(QLatin1Char('2')));
		QVERIFY(lines[1].contains(QLatin1String("Sketch")) && lines[1].contains(QLatin1Char('3')));
	}
	
	/// Accept (2)
	void spotPlatesTest()
	{
		Map map;
		setupMap(map);
		auto const plates = renderSpotPlates(map, area, dpi);
		QCOMPARE(plates.size(), std::size_t(2));
		auto const* brown = plateNamed(plates, QStringLiteral("BROWN"));
		auto const* black = plateNamed(plates, QStringLiteral("BLACK"));
		QVERIFY(brown && black);
		for (auto const& plate : plates)
		{
			QCOMPARE(plate.ink.size(), QSize(200, 100));
			QCOMPARE(plate.ink.format(), QImage::Format_Grayscale8);
		}
		
		QCOMPARE(ink(*brown, 30, 30), 255);
		QVERIFY(ink(*brown, 80, 30) == 127 || ink(*brown, 80, 30) == 128);
		QCOMPARE(ink(*brown, 30, 80), 0);
		QCOMPARE(ink(*black, 30, 30), 0);
		QCOMPARE(ink(*black, 30, 80), 0);
		// Black with knockout over brown.
		QCOMPARE(ink(*brown, 115, 30), 255);
		QCOMPARE(ink(*brown, 130, 30), 0);
		QCOMPARE(ink(*black, 130, 30), 255);
		// The colour without a spot colour is on neither plate.
		QCOMPARE(ink(*brown, 175, 30), 0);
		QCOMPARE(ink(*black, 175, 30), 0);
	}
	
	/// Accept (3)
	void processPlatesTest()
	{
		Map map;
		setupMap(map);
		auto const plates = renderProcessPlates(map, area, dpi);
		QCOMPARE(plates.size(), std::size_t(4));
		QCOMPARE(plates[0].name, QStringLiteral("Cyan"));
		QCOMPARE(plates[3].name, QStringLiteral("Black"));
		const int brown[4] = { 0, 143, 255, 46 };
		const int pale[4] = { 51, 0, 0, 26 };
		for (std::size_t i = 0; i < 4; ++i)
		{
			QCOMPARE(plates[i].ink.size(), QSize(200, 100));
			QCOMPARE(ink(plates[i], 30, 30), brown[i]);
			QCOMPARE(ink(plates[i], 30, 80), 0);
			// Half of (0.4, 0, 0, 0.2).
			QVERIFY2(qAbs(ink(plates[i], 175, 30) - pale[i]) <= 1, qPrintable(QString::number(ink(plates[i], 175, 30))));
		}
		// Black over brown.
		QCOMPARE(ink(plates[3], 130, 30), 255);
		QCOMPARE(ink(plates[2], 130, 30), 0);
		
		// An image for a plate keeps its alpha.
		QImage image(2, 1, QImage::Format_ARGB32);
		image.setPixel(0, 0, qRgba(255, 0, 0, 255));
		image.setPixel(1, 0, qRgba(0, 0, 0, 100));
		QCOMPARE(qRed(processPlateOfImage(image, 0).pixel(0, 0)), 255);  // no cyan in red
		QCOMPARE(qRed(processPlateOfImage(image, 1).pixel(0, 0)), 0);    // full magenta
		QCOMPARE(qRed(processPlateOfImage(image, 3).pixel(1, 0)), 0);    // full black
		QCOMPARE(qAlpha(processPlateOfImage(image, 3).pixel(1, 0)), 100);
	}
	
	/// Accept (4)
	void halftoneTest()
	{
		QString error;
		auto const none = plateAsHalftone(uniformPlate(0), 1200, 60, 45, &error);
		QCOMPARE(none.format(), QImage::Format_Mono);
		QCOMPARE(none.size(), QSize(600, 600));
		QCOMPARE(blackShare(none), 0.0);
		QCOMPARE(blackShare(plateAsHalftone(uniformPlate(255), 1200, 60, 45, &error)), 1.0);
		auto const half = blackShare(plateAsHalftone(uniformPlate(128), 1200, 60, 45, &error));
		QVERIFY2(half > 0.47 && half < 0.53, qPrintable(QString::number(half)));
		auto const quarter = blackShare(plateAsHalftone(uniformPlate(64), 1200, 60, 45, &error));
		QVERIFY2(quarter > 0.20 && quarter < 0.30, qPrintable(QString::number(quarter)));
		QVERIFY(error.isEmpty());
		
		// A colour without a frequency: 150 lines per inch at 1200 dpi.
		auto const automatic = blackShare(plateAsHalftone(uniformPlate(128), 1200, -1, 0, &error));
		QVERIFY2(automatic > 0.45 && automatic < 0.55, qPrintable(QString::number(automatic)));
		
		QVERIFY(plateAsHalftone(uniformPlate(128), 600, 100, 45, &error).isNull());
		QVERIFY(!error.isEmpty());
		
		QCOMPARE(blackShare(plateAsLineArt(uniformPlate(127))), 0.0);
		QCOMPARE(blackShare(plateAsLineArt(uniformPlate(128))), 1.0);
		QCOMPARE(plateAsGrey(uniformPlate(255)).pixelColor(0, 0), QColor(Qt::black));
		
		QCOMPARE(plateFileName(QStringLiteral("C:/out/map.tif"), QStringLiteral("PANTONE 471")), QStringLiteral("C:/out/map-PANTONE 471.tif"));
		QCOMPARE(plateFileName(QStringLiteral("out/map.png"), QStringLiteral("a/b:c")), QStringLiteral("out/map-a_b_c.png"));
	}
	
	/// Accept (4): a screen which is too fine fails before any plate is written.
	void halftonePlatesTest()
	{
		Map map;
		setupMap(map);
		auto const plates = renderSpotPlates(map, area, dpi);
		QCOMPARE(plates.size(), std::size_t(2));
		
		// Without screens, the halftone plates are written.
		QStringList files;
		QString error;
		QVERIFY2(saveSpotPlates(map, area, dpi, 1.0, file("fine.png"), PlateForm::Halftone, &files, &error), qPrintable(error));
		QCOMPARE(files.size(), 2);
		
		// The colour of the last plate gets a screen which 254 dpi cannot hold.
		for (int i = 0; i < map.getNumColors(); ++i)
		{
			auto* color = map.getMapColor(i);
			if (color->getSpotColorMethod() == MapColor::SpotColor && color->getSpotColorName() == plates.back().name)
			{
				color->setScreenAngle(45);
				color->setScreenFrequency(150);
			}
		}
		files.clear();
		error.clear();
		QVERIFY(!saveSpotPlates(map, area, dpi, 1.0, file("coarse.png"), PlateForm::Halftone, &files, &error));
		QVERIFY2(error.contains(QStringLiteral("150")) && error.contains(QStringLiteral("1200")), qPrintable(error));
		QVERIFY(files.isEmpty());
		QVERIFY(QDir(dir.path()).entryList({ QStringLiteral("coarse*") }).isEmpty());
		
		// The other forms do not mind the screen.
		QVERIFY2(saveSpotPlates(map, area, dpi, 1.0, file("coarse.png"), PlateForm::LineArt, &files, &error), qPrintable(error));
		QCOMPARE(files.size(), 2);
	}
	
	/// Accept (5), (7)
	void runnerSpotPlatesTest()
	{
		Map map;
		setupMap(map);
		auto* hidden = addArea(map, QStringLiteral("Hidden thing"), 21, map.getColor(1));
		hidden->setHidden(true);
		addSquare(map, hidden, { 1, 6, 2, 2 });
		auto const map_path = file("plates.foom");
		QVERIFY(saveMap(map, map_path));
		
		QJsonObject export_step {
			{ QStringLiteral("path"), file("plates.tif") },
			{ QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 } },
			{ QStringLiteral("dpi"), 254 },
			{ QStringLiteral("plates"), QStringLiteral("spot") },
		};
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("spot.json"), &output) == 0, output.constData());
		QVERIFY2(output.contains("plates-BROWN.tif") && output.contains("plates-BLACK.tif"), output.constData());
		QVERIFY2(output.contains("Hidden thing"), output.constData());
		QVERIFY(!QFileInfo::exists(file("plates.tif")));
		const QImage brown(file("plates-BROWN.tif"));
		QCOMPARE(brown.size(), QSize(200, 100));
		QCOMPARE(brown.depth(), 8);
		QVERIFY(brown.isGrayscale());
		QCOMPARE(qGray(brown.pixel(30, 30)), 0);
		QCOMPARE(qGray(brown.pixel(30, 80)), 255);
		// The hidden symbol's square is not drawn.
		QCOMPARE(qGray(brown.pixel(20, 70)), 255);
		QVERIFY(QFileInfo::exists(file("plates-BLACK.tif")));
		
		export_step.insert(QStringLiteral("path"), file("line.png"));
		export_step.insert(QStringLiteral("depth"), 1);
		QVERIFY2(runJob(jobOf(map_path, export_step), file("line.json"), &output) == 0, output.constData());
		const QImage line(file("line-BROWN.png"));
		QCOMPARE(line.size(), QSize(200, 100));
		QCOMPARE(line.depth(), 1);
		QCOMPARE(qGray(line.pixel(30, 30)), 0);
		// 50 % is black at the threshold, or not, but nothing between.
		QCOMPARE(qGray(line.pixel(30, 80)), 255);
		
		// A screen of 20 lines per inch by default would need 160 dpi; the map's colours have none, so 254 / 8.
		export_step.insert(QStringLiteral("path"), file("screen.png"));
		export_step.insert(QStringLiteral("halftone"), true);
		QVERIFY2(runJob(jobOf(map_path, export_step), file("screen.json"), &output) == 0, output.constData());
		const QImage screen(file("screen-BROWN.png"));
		QCOMPARE(screen.depth(), 1);
		auto const tint = blackShare(screen.copy(62, 12, 36, 36));
		QVERIFY2(tint > 0.35 && tint < 0.65, qPrintable(QString::number(tint)));
		QCOMPARE(blackShare(screen.copy(12, 12, 36, 36)), 1.0);
		
		// Rejected when the job is read: nothing is written.
		export_step.insert(QStringLiteral("path"), file("bad.png"));
		export_step.insert(QStringLiteral("depth"), 8);
		QVERIFY(runJob(jobOf(map_path, export_step), file("bad1.json"), &output) != 0);
		export_step.remove(QStringLiteral("halftone"));
		export_step.remove(QStringLiteral("depth"));
		export_step.insert(QStringLiteral("plates"), QStringLiteral("cmyk"));
		QVERIFY(runJob(jobOf(map_path, export_step), file("bad2.json"), &output) != 0);
		QVERIFY(!QFileInfo::exists(file("bad.png")));
		QVERIFY(!QFileInfo::exists(file("bad-BROWN.png")));
	}
	
	/// Accept (6)
	void runnerCmykTest()
	{
		Map map;
		setupMap(map);
		auto const map_path = file("cmyk.foom");
		QVERIFY(saveMap(map, map_path));
		
		auto const tiff_path = file("map.tif");
		QJsonObject export_step {
			{ QStringLiteral("path"), tiff_path },
			{ QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 } },
			{ QStringLiteral("dpi"), 254 },
			{ QStringLiteral("plates"), QStringLiteral("cmyk") },
		};
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("cmyk.json"), &output) == 0, output.constData());
		QVERIFY(QFileInfo::exists(tiff_path));
		
		GDALAllRegister();
		// Without the prefix, GDAL would hand out the file converted to RGB.
		QByteArray name = "GTIFF_RAW:";
		name += tiff_path.toUtf8();
		auto const dataset = GDALOpen(name.constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterCount(dataset), 4);
		QCOMPARE(GDALGetRasterXSize(dataset), 200);
		QCOMPARE(GDALGetRasterYSize(dataset), 100);
		const GDALColorInterp interpretations[4] = { GCI_CyanBand, GCI_MagentaBand, GCI_YellowBand, GCI_BlackBand };
		const int brown[4] = { 0, 143, 255, 46 };
		for (int band = 0; band < 4; ++band)
		{
			auto const handle = GDALGetRasterBand(dataset, band + 1);
			QCOMPARE(GDALGetRasterColorInterpretation(handle), interpretations[band]);
			unsigned char value = 77;
			QCOMPARE(GDALRasterIO(handle, GF_Read, 30, 30, 1, 1, &value, 1, 1, GDT_Byte, 0, 0), CE_None);
			QCOMPARE(int(value), brown[band]);
			QCOMPARE(GDALRasterIO(handle, GF_Read, 30, 80, 1, 1, &value, 1, 1, GDT_Byte, 0, 0), CE_None);
			QCOMPARE(int(value), 0);
		}
		GDALClose(dataset);
	}
	
	/// Accept (8), (9)
	void printJobTest()
	{
		Map map;
		setupMap(map);
		auto const map_path = file("job.foom");
		QVERIFY(saveMap(map, map_path));
		
		MapPrinter printer(map, nullptr);
		printer.setTarget(MapPrinter::pdfTarget());
		printer.setPageSize(QPageSize::A5);
		printer.setPageOrientation(MapPrinterPageFormat::Landscape);
		printer.setPrintArea(QRectF(10, 20, 190, 130));
		printer.setResolution(600);
		printer.setMode(MapPrinterOptions::Vector);
		
		auto job = printJob(printer, map_path, QStringLiteral("name"));
		auto steps = job.value(QLatin1String("steps")).toArray();
		QCOMPARE(steps.size(), 2);
		QCOMPARE(steps[0].toObject().value(QLatin1String("step")).toString(), QStringLiteral("open"));
		QCOMPARE(steps[0].toObject().value(QLatin1String("path")).toString(), map_path);
		auto output = steps[1].toObject();
		QCOMPARE(output.value(QLatin1String("step")).toString(), QStringLiteral("export"));
		QCOMPARE(output.value(QLatin1String("path")).toString(), QStringLiteral("name.pdf"));
		QCOMPARE(output.value(QLatin1String("extent")).toArray(), (QJsonArray{ 10, 20, 190, 130 }));
		QCOMPARE(output.value(QLatin1String("dpi")).toInt(), 600);
		QCOMPARE(output.value(QLatin1String("scale")).toInt(), 10000);
		QCOMPARE(output.value(QLatin1String("mode")).toString(), QStringLiteral("vector"));
		QCOMPARE(output.value(QLatin1String("paper")).toString(), QStringLiteral("A5"));
		QCOMPARE(output.value(QLatin1String("orientation")).toString(), QStringLiteral("landscape"));
		QVERIFY(output.value(QLatin1String("templates")).isBool());
		QVERIFY(output.value(QLatin1String("grid")).isBool());
		QVERIFY(output.value(QLatin1String("overprinting")).isBool());
		QCOMPARE(output.value(QLatin1String("overlap")).toArray().size(), 2);
		
		// Run as it is written.
		auto const job_path = file("name.json");
		QString error;
		QVERIFY2(savePrintJob(printer, map_path, job_path, &error), qPrintable(error));
		QByteArray log;
		QVERIFY2(runJobFile(job_path, &log) == 0, log.constData());
		auto pages = pdfPages(file("name.pdf"));
		QCOMPARE(pages.size(), std::size_t(1));
		QCOMPARE(pages[0], QSize(595, 420));
		
		// The same area on smaller paper.
		output.insert(QStringLiteral("paper"), QStringLiteral("A6"));
		output.insert(QStringLiteral("path"), QStringLiteral("small.pdf"));
		steps[1] = output;
		job.insert(QStringLiteral("steps"), steps);
		QVERIFY2(runJob(job, file("small.json"), &log) == 0, log.constData());
		pages = pdfPages(file("small.pdf"));
		QCOMPARE(pages.size(), std::size_t(4));
		for (auto const& page : pages)
			QCOMPARE(page, QSize(420, 297));
		
		// A custom paper size, and the image target.
		printer.setCustomPageSize({ 200, 140 });
		output = printJob(printer, map_path, QStringLiteral("name")).value(QLatin1String("steps")).toArray()[1].toObject();
		QCOMPARE(output.value(QLatin1String("paper")).toArray(), (QJsonArray{ 200, 140 }));
		QVERIFY(!output.contains(QLatin1String("orientation")));
		
		printer.setTarget(MapPrinter::imageTarget());
		output = printJob(printer, map_path, QStringLiteral("name")).value(QLatin1String("steps")).toArray()[1].toObject();
		QCOMPARE(output.value(QLatin1String("path")).toString(), QStringLiteral("name.png"));
		QVERIFY(!output.contains(QLatin1String("paper")));
		QVERIFY(!output.contains(QLatin1String("mode")));
	}
	
	/// Accept (10)
	void runnerGridTest()
	{
		Map map;
		setupMap(map);
		auto const map_path = file("grid.foom");
		QVERIFY(saveMap(map, map_path));
		
		QJsonObject export_step {
			{ QStringLiteral("path"), file("plain.png") },
			{ QStringLiteral("extent"), QJsonArray{ 0, 0, 120, 120 } },
			{ QStringLiteral("dpi"), 100 },
		};
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("plain.json"), &output) == 0, output.constData());
		export_step.insert(QStringLiteral("path"), file("grid.png"));
		export_step.insert(QStringLiteral("grid"), true);
		QVERIFY2(runJob(jobOf(map_path, export_step), file("grid.json"), &output) == 0, output.constData());
		const QImage plain(file("plain.png"));
		const QImage grid(file("grid.png"));
		QVERIFY(!plain.isNull());
		QCOMPARE(plain.size(), grid.size());
		QVERIFY(plain != grid);
		
		// Paper is for PDF only, and has to be known.
		export_step.insert(QStringLiteral("paper"), QStringLiteral("A4"));
		QVERIFY(runJob(jobOf(map_path, export_step), file("bad3.json"), &output) != 0);
		export_step.insert(QStringLiteral("path"), file("bad.pdf"));
		export_step.insert(QStringLiteral("paper"), QStringLiteral("No such paper"));
		QVERIFY(runJob(jobOf(map_path, export_step), file("bad4.json"), &output) != 0);
	}
};


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(PrintOutputTest)
#include "print_output_t.moc"  // IWYU pragma: keep
