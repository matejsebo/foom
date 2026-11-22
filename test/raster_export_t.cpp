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


#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRect>
#include <QRectF>
#include <QRgb>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTransform>

#include <gdal.h>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/print_output.h"
#include "core/raster_export.h"
#include "core/symbols/area_symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"

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

void addSquare(Map& map, const Symbol* symbol, const QRectF& box)
{
	MapCoordVector coords { MapCoord(box.topLeft()), MapCoord(box.topRight()), MapCoord(box.bottomRight()), MapCoord(box.bottomLeft()), MapCoord(box.topLeft()) };
	coords.back().setClosePoint(true);
	map.addObject(new PathObject(symbol, coords));
}

/**
 * The map of print_output_t: in the area (0, 0) to (20, 10) mm, full brown
 * at x 1..5, 50 % brown at 6..10, black (knockout) over brown at 11..15,
 * a half transparent process colour at 16..19, all from y = 1 to 5.
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

/// A UTM 13 georeferencing: map (0, 0) is at 500000 E, 5200000 N, no grivation.
void setUtm32(Map& map)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000, 5200000), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	map.setGeoreferencing(georef);
}

/// The RGB rendering of the area, as the image export draws it.
QImage renderRgb(Map& map)
{
	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setPrintArea(area);
	printer.setResolution(dpi);
	QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	QPainter painter(&image);
	printer.drawPage(&painter, area, &image);
	painter.end();
	return image;
}

QRgb rgbOf(const MapColor* color)
{
	return static_cast<const QColor&>(*color).rgb() & 0xffffff;
}

bool rgbNear(QRgb pixel, QRgb expected, int tolerance)
{
	return qAbs(qRed(pixel) - qRed(expected)) <= tolerance
	       && qAbs(qGreen(pixel) - qGreen(expected)) <= tolerance
	       && qAbs(qBlue(pixel) - qBlue(expected)) <= tolerance;
}

QString rgbText(QRgb pixel)
{
	return QStringLiteral("%1,%2,%3").arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel));
}

QStringList worldFileLines(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return {};
	return QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

bool worldFileIs(const QString& path, const std::vector<double>& expected)
{
	auto const lines = worldFileLines(path);
	if (lines.size() != int(expected.size()))
		return false;
	for (int i = 0; i < lines.size(); ++i)
	{
		if (qAbs(lines[i].toDouble() - expected[std::size_t(i)]) > 1e-6)
			return false;
	}
	return true;
}

}  // namespace


/**
 * @test 256 colours, greyscale, GIF, tiles, and the spot colour view.
 *
 * the design notes, "Print and raster output", fourth change;
 * the design notes, "Spot colour view".
 */
class RasterExportTest : public QObject
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

	int runJob(const QJsonObject& job, const QString& job_path, QByteArray* output = nullptr) const
	{
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			job_file.write(QJsonDocument(job).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return -1;
		if (output)
			*output = process.readAllStandardOutput() + process.readAllStandardError();
		return process.exitCode();
	}

	QJsonObject jobOf(const QString& map_path, QJsonObject export_step) const
	{
		export_step.insert(QStringLiteral("step"), QStringLiteral("export"));
		export_step.insert(QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 });
		export_step.insert(QStringLiteral("dpi"), 254);
		QJsonObject open { { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } };
		return QJsonObject{ { QStringLiteral("steps"), QJsonArray{ open, export_step } } };
	}

private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
		GDALAllRegister();
	}

	/// Accept (14)
	void paletteTest()
	{
		Map map;
		setupMap(map);
		auto const palette = mapPalette(map);
		QCOMPARE(palette.size(), 5);
		QCOMPARE(palette[0] & 0xffffff, QRgb(0xffffff));
		for (int i = 0; i < 4; ++i)
			QCOMPARE(palette[i + 1] & 0xffffff, rgbOf(map.getColor(i)));

		Map twins;
		addColor(twins, QStringLiteral("One"), { 0.0f, 0.0f, 0.0f, 1.0f });
		addColor(twins, QStringLiteral("Two"), { 0.0f, 0.0f, 0.0f, 1.0f });
		QCOMPARE(mapPalette(twins).size(), 2);

		Map many;
		for (int i = 0; i < 300; ++i)
			addColor(many, QString::number(i), { 0.0f, float(i % 20) / 20, float(i / 20) / 15, 0.0f });
		QCOMPARE(mapPalette(many).size(), 256);
	}

	/// Accept (15)
	void convertTest()
	{
		Map map;
		setupMap(map);
		auto const rgb = renderRgb(map);
		auto const palette = mapPalette(map);

		auto const indexed = convertRasterColours(rgb, RasterColours::Palette, map);
		QCOMPARE(indexed.format(), QImage::Format_Indexed8);
		QVERIFY(indexed.colorCount() <= 256);
		auto const brown = rgbOf(map.getColor(1));
		QCOMPARE(indexed.pixel(30, 30) & 0xffffff, brown);
		QCOMPARE(indexed.pixel(130, 30) & 0xffffff, QRgb(0));
		QCOMPARE(indexed.pixel(30, 80) & 0xffffff, QRgb(0xffffff));
		for (int y = 0; y < indexed.height(); ++y)
		{
			for (int x = 0; x < indexed.width(); ++x)
			{
				auto const pixel = indexed.pixel(x, y) | 0xff000000;
				bool found = false;
				for (auto entry : palette)
					found = found || (entry | 0xff000000) == pixel;
				if (!found)
					QFAIL(qPrintable(QStringLiteral("pixel %1,%2 is %3").arg(x).arg(y).arg(rgbText(pixel))));
			}
		}

		auto const grey = convertRasterColours(rgb, RasterColours::Grey, map);
		QCOMPARE(grey.format(), QImage::Format_Grayscale8);
		QVERIFY2(qAbs(qGray(grey.pixel(30, 30)) - qGray(brown)) <= 1, qPrintable(QString::number(qGray(grey.pixel(30, 30)))));
		QCOMPARE(qGray(grey.pixel(130, 30)), 0);
		QCOMPARE(qGray(grey.pixel(30, 80)), 255);
	}

	/// Accept (16)
	void tilesTest()
	{
		auto const tiles = rasterTiles(QSize(200, 100), { 64, 64 }, QStringLiteral("dir/map.png"));
		QCOMPARE(int(tiles.size()), 8);
		QCOMPARE(tiles.front().rect, QRect(0, 0, 64, 64));
		QCOMPARE(tiles.front().path, QStringLiteral("dir/map_1_1.png"));
		QCOMPARE(tiles.back().rect, QRect(192, 64, 8, 36));
		QCOMPARE(tiles.back().path, QStringLiteral("dir/map_2_4.png"));
		QVERIFY(rasterTiles(QSize(200, 100), {}, QStringLiteral("dir/map.png")).empty());
	}

	/// Accept (17)
	void saveTilesTest()
	{
		Map map;
		setupMap(map);
		setUtm32(map);
		auto const rgb = renderRgb(map);
		auto const world = rasterWorldTransform(map, area, dpi);
		QVERIFY(qAbs(world.m11() - 1) < 1e-9);
		QVERIFY(qAbs(world.m22() + 1) < 1e-9);
		QVERIFY(qAbs(world.dx() - 500000) < 1e-6);
		QVERIFY(qAbs(world.dy() - 5200000) < 1e-6);

		RasterSaveOptions options;
		options.tiles = { 128, 64 };
		options.world_file = true;
		options.world = world;
		QStringList files;
		QString error;
		auto const path = file("tiles/map.png");
		QVERIFY(QDir().mkpath(file("tiles")));
		QVERIFY2(saveRasterImage(rgb, map, path, options, &files, &error), qPrintable(error));
		QCOMPARE(files.size(), 4);
		QVERIFY(!QFileInfo::exists(path));
		const QSize sizes[4] = { { 128, 64 }, { 72, 64 }, { 128, 36 }, { 72, 36 } };
		const QPoint offsets[4] = { { 0, 0 }, { 128, 0 }, { 0, 64 }, { 128, 64 } };
		const char* names[4] = { "tiles/map_1_1", "tiles/map_1_2", "tiles/map_2_1", "tiles/map_2_2" };
		for (int i = 0; i < 4; ++i)
		{
			auto const name = file(names[i]);
			QVERIFY2(files.contains(name + QLatin1String(".png")), qPrintable(files.join(QLatin1Char(' '))));
			const QImage tile(name + QLatin1String(".png"));
			QCOMPARE(tile.size(), sizes[i]);
			QVERIFY(QFileInfo::exists(name + QLatin1String(".pgw")));
			auto const whole = rgb.copy(QRect(offsets[i], sizes[i])).convertToFormat(QImage::Format_RGB32);
			QVERIFY(tile.convertToFormat(QImage::Format_RGB32) == whole);
		}
		QVERIFY(worldFileIs(file("tiles/map_1_2.pgw"), { 1, 0, 0, -1, 500128.5, 5199999.5 }));
	}

	/// Accept (18)
	void runnerGifTest()
	{
		QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
		Map map;
		setupMap(map);
		auto const brown = rgbOf(map.getColor(1));
		auto const map_path = file("gif.foom");
		QVERIFY(saveMap(map, map_path));

		QJsonObject export_step { { QStringLiteral("path"), file("map.gif") } };
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("gif.json"), &output) == 0, output.constData());
		auto* dataset = GDALOpen(file("map.gif").toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(QByteArray(GDALGetDriverShortName(GDALGetDatasetDriver(dataset))), QByteArray("GIF"));
		GDALClose(dataset);
		const QImage gif(file("map.gif"));
		QCOMPARE(gif.size(), QSize(200, 100));
		QVERIFY2((gif.pixel(30, 30) & 0xffffff) == brown, qPrintable(rgbText(gif.pixel(30, 30))));
		QCOMPARE(gif.pixel(30, 80) & 0xffffff, QRgb(0xffffff));

		export_step.insert(QStringLiteral("path"), file("grey.gif"));
		export_step.insert(QStringLiteral("colours"), QStringLiteral("grey"));
		QVERIFY2(runJob(jobOf(map_path, export_step), file("grey-gif.json"), &output) == 0, output.constData());
		const QImage grey_gif(file("grey.gif"));
		QCOMPARE(qGray(grey_gif.pixel(130, 30)), 0);
		QCOMPARE(qGray(grey_gif.pixel(30, 80)), 255);

		// Rejected when the job is read: nothing is written.
		struct Bad { const char* path; const char* key; QJsonValue value; };
		const Bad bad_steps[] = {
		    { "bad1.gif", "colours", QStringLiteral("rgb") },
		    { "bad2.jpg", "colours", QStringLiteral("256") },
		    { "bad3.png", "colours", QStringLiteral("16") },
		    { "bad4.png", "tiles", 0 },
		    { "bad5.png", "tiles", QJsonArray{ 64 } },
		};
		for (auto const& bad : bad_steps)
		{
			QJsonObject step { { QStringLiteral("path"), file(bad.path) }, { QLatin1String(bad.key), bad.value } };
			QVERIFY2(runJob(jobOf(map_path, step), file("bad.json"), &output) != 0, bad.path);
			QVERIFY(!QFileInfo::exists(file(bad.path)));
		}
		QJsonObject plates { { QStringLiteral("path"), file("bad6.png") }, { QStringLiteral("tiles"), 64 }, { QStringLiteral("plates"), QStringLiteral("spot") } };
		QVERIFY(runJob(jobOf(map_path, plates), file("bad.json"), &output) != 0);
		QVERIFY(!QFileInfo::exists(file("bad6-BROWN.png")));
	}

	/// Accept (19)
	void runnerPngTest()
	{
		Map map;
		setupMap(map);
		setUtm32(map);
		auto const map_path = file("png.foom");
		QVERIFY(saveMap(map, map_path));

		QJsonObject export_step { { QStringLiteral("path"), file("grey.png") }, { QStringLiteral("colours"), QStringLiteral("grey") } };
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("grey.json"), &output) == 0, output.constData());
		const QImage grey(file("grey.png"));
		QCOMPARE(grey.size(), QSize(200, 100));
		QVERIFY(grey.format() == QImage::Format_Grayscale8 || (grey.format() == QImage::Format_Indexed8 && grey.isGrayscale()));

		export_step.insert(QStringLiteral("path"), file("indexed.png"));
		export_step.insert(QStringLiteral("colours"), QStringLiteral("256"));
		QVERIFY2(runJob(jobOf(map_path, export_step), file("indexed.json"), &output) == 0, output.constData());
		const QImage indexed(file("indexed.png"));
		QCOMPARE(indexed.format(), QImage::Format_Indexed8);
		QVERIFY(indexed.colorCount() <= 256);

		QVERIFY(QDir().mkpath(file("runner-tiles")));
		QJsonObject tiled {
			{ QStringLiteral("path"), file("runner-tiles/map.png") },
			{ QStringLiteral("tiles"), QJsonArray{ 128, 64 } },
			{ QStringLiteral("world_file"), true },
		};
		QVERIFY2(runJob(jobOf(map_path, tiled), file("tiled.json"), &output) == 0, output.constData());
		for (auto name : { "map_1_1", "map_1_2", "map_2_1", "map_2_2" })
		{
			QString const base = file("runner-tiles/") + QLatin1String(name);
			QVERIFY2(output.contains(QFileInfo(base + QLatin1String(".png")).fileName().toUtf8()), output.constData());
			QVERIFY(QFileInfo::exists(base + QLatin1String(".png")));
			QVERIFY(QFileInfo::exists(base + QLatin1String(".pgw")));
		}
		QVERIFY(worldFileIs(file("runner-tiles/map_1_2.pgw"), { 1, 0, 0, -1, 500128.5, 5199999.5 }));

		Map local;
		setupMap(local);
		auto const local_path = file("local.foom");
		QVERIFY(saveMap(local, local_path));
		QJsonObject world { { QStringLiteral("path"), file("local.png") }, { QStringLiteral("world_file"), true } };
		QVERIFY2(runJob(jobOf(local_path, world), file("local.json"), &output) == 0, output.constData());
		QVERIFY2(output.contains("world file"), output.constData());
		QVERIFY(QFileInfo::exists(file("local.png")));
		QVERIFY(!QFileInfo::exists(file("local.pgw")));
	}

	/// the design notes, "Spot colour view", Accept (1) to (3)
	void spotColourViewTest()
	{
		Map map;
		setupMap(map);
		auto const* black = map.getColor(0);
		auto const* brown = map.getColor(1);
		auto const brown_rgb = rgbOf(brown);

		auto const plate = renderSpotColourView(map, area, dpi, brown);
		QCOMPARE(plate.size(), QSize(200, 100));
		QVERIFY2(rgbNear(plate.pixel(30, 30), brown_rgb, 2), qPrintable(rgbText(plate.pixel(30, 30))));
		// Half ink: the RGB of the tint as the colour table makes it from half the CMYK.
		auto const half = rgbOf(map.getColor(2));
		QVERIFY2(rgbNear(plate.pixel(80, 30), half, 3), qPrintable(rgbText(plate.pixel(80, 30))));
		QVERIFY2(rgbNear(plate.pixel(115, 30), brown_rgb, 2), qPrintable(rgbText(plate.pixel(115, 30))));
		QVERIFY2(rgbNear(plate.pixel(130, 30), qRgb(255, 255, 255), 0), qPrintable(rgbText(plate.pixel(130, 30))));
		QVERIFY2(rgbNear(plate.pixel(175, 30), qRgb(255, 255, 255), 0), qPrintable(rgbText(plate.pixel(175, 30))));
		QVERIFY(rgbNear(plate.pixel(30, 80), qRgb(255, 255, 255), 0));

		auto const black_plate = renderSpotColourView(map, area, dpi, black);
		QVERIFY2(rgbNear(black_plate.pixel(130, 30), qRgb(0, 0, 0), 2), qPrintable(rgbText(black_plate.pixel(130, 30))));
		for (auto x : { 30, 80, 115, 175 })
			QVERIFY2(rgbNear(black_plate.pixel(x, 30), qRgb(255, 255, 255), 0), qPrintable(rgbText(black_plate.pixel(x, 30))));

		MapView view(&map);
		QVERIFY(!view.spotColourView());
		view.setOverprintingSimulationEnabled(true);
		view.setSpotColourView(brown);
		QCOMPARE(view.spotColourView(), brown);
		QVERIFY(!view.isOverprintingSimulationEnabled());
		view.setOverprintingSimulationEnabled(true);
		QVERIFY(!view.spotColourView());
	}

	/// the design notes, "Spot colour view", Accept (4)
	void spotColourViewEditor()
	{
		auto* map = new Map();
		setupMap(*map);
		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(editor);
		auto* view = editor->getMainWidget() ? editor->getMainWidget()->getMapView() : nullptr;
		QVERIFY(view);

		auto* menu = window->findChild<QMenu*>(QStringLiteral("spot-colour-view"));
		QVERIFY(menu);
		emit menu->aboutToShow();
		QStringList entries;
		for (auto* action : menu->actions())
		{
			if (!action->isSeparator())
				entries.append(action->text().remove(QLatin1Char('&')));
		}
		QCOMPARE(entries, QStringList({ QStringLiteral("Off"), QStringLiteral("Black"), QStringLiteral("Brown") }));

		auto* overprinting = editor->getAction("overprintsimulation");
		QVERIFY(overprinting);
		overprinting->trigger();
		QVERIFY(overprinting->isChecked());
		QVERIFY(view->isOverprintingSimulationEnabled());
		editor->showSpotColourView(1);
		QCOMPARE(view->spotColourView(), map->getColor(1));
		QVERIFY(!overprinting->isChecked());
		QVERIFY(!view->isOverprintingSimulationEnabled());
		overprinting->trigger();
		QVERIFY(overprinting->isChecked());
		QVERIFY(!view->spotColourView());

		editor->showSpotColourView(1);
		QCOMPARE(view->spotColourView(), map->getColor(1));
		map->deleteColor(1);
		QVERIFY(!view->spotColourView());

		map->setHasUnsavedChanges(false);  // no question on closing
		window->close();
		window->deleteLater();
	}
};


namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(RasterExportTest)
#include "raster_export_t.moc"  // IWYU pragma: keep
