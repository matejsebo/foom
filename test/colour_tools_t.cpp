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

// foom: the design notes, "Colours"

#include <cmath>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/colour_load.h"
#include "core/colour_swatch.h"
#include "core/colour_usage.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
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
	map.addColor(color, map.getNumColors());
	return color;
}

bool near(const MapColorCmyk& cmyk, float c, float m, float y, float k)
{
	return std::abs(cmyk.c - c) < 1e-4f && std::abs(cmyk.m - m) < 1e-4f
	       && std::abs(cmyk.y - y) < 1e-4f && std::abs(cmyk.k - k) < 1e-4f;
}

}  // namespace


class ColourToolsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase() { doStaticInitializations(); }  // the file format registry, for the OCD round trip
	void swatchSemicolonForm();
	void swatchTabForm();
	void swatchApply();
	void blendModeFile();
	void blendModeCopy();
	void loadReplaceAndAppend();
	void loadSpotComposition();
	void crtRead();
	void colourNumbers();
	void crtLoad();
	void usageReport();
};


// Swatch import, Accept (1)
void ColourToolsTest::swatchSemicolonForm()
{
	QString error;
	auto const entries = readColourSwatch("NR;NAME;CYAN;MAGENTA;YELLOW;BLACK\r\n1;White;0;0;0;0\r\n2;Black;0;0;0;100\r\n3;Brown;0;56;100;18\r\n", &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(int(entries.size()), 3);
	QCOMPARE(entries[0].number, 1);
	QCOMPARE(entries[2].number, 3);
	QCOMPARE(entries[2].name, QStringLiteral("Brown"));
	QVERIFY(near(entries[2].cmyk, 0, 0.56f, 1, 0.18f));
	QCOMPARE(entries[2].overprint, -1);
	QVERIFY(entries[2].opacity < 0);

	auto const csv = readColourSwatch("Name,C,M,Y,K\nBlue,100,0,0,0\n", &error);
	QCOMPARE(int(csv.size()), 1);
	QCOMPARE(csv[0].number, -1);
	QVERIFY(near(csv[0].cmyk, 1, 0, 0, 0));
}

// Swatch import, Accept (2)
void ColourToolsTest::swatchTabForm()
{
	QString error;
	auto const entries = readColourSwatch(QByteArray("\xef\xbb\xbf" "Colors\r\n\r\n\t\tCMYK (process) colors\t\t\r\n"
	                                                 "\tNo.\tName\tCyan\tMagenta\tYellow\tBlack\tOv.\tOpacity\r\n"
	                                                 "\t2\tBlack\t0\t0\t0\t100\t1\t100\r\n"
	                                                 "\t5\tBrown\t25\t75\t100\t0\t0\t50\t\r\n"
	                                                 "\r\n"), &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(int(entries.size()), 2);
	QCOMPARE(entries[0].number, 2);
	QCOMPARE(entries[0].overprint, 1);
	QCOMPARE(entries[0].opacity, 1.0);
	QCOMPARE(entries[1].name, QStringLiteral("Brown"));
	QCOMPARE(entries[1].overprint, 0);
	QCOMPARE(entries[1].opacity, 0.5);
	QVERIFY(near(entries[1].cmyk, 0.25f, 0.75f, 1, 0));

	auto const latin1 = readColourSwatch("Nr.\tName\tC\tM\tY\tK\n1\tGr\xfcn\t80\t0\t100\t0\n", &error);
	QCOMPARE(int(latin1.size()), 1);
	QCOMPARE(latin1[0].name, QString::fromUtf8("Grün"));

	error.clear();
	auto const none = readColourSwatch("1;White;0;0;0;0\n", &error);
	QVERIFY(none.empty());
	QVERIFY(!error.isEmpty());
}

// Swatch import, Accept (3)
void ColourToolsTest::swatchApply()
{
	auto const swatch = std::vector<SwatchEntry>{
	    { 2, QStringLiteral("Black"), MapColorCmyk(0, 0, 0, 1), 1, 1.0 },
	    { 5, QStringLiteral("Brown "), MapColorCmyk(0.25f, 0.75f, 1, 0), 0, 0.5 },
	    { 6, QStringLiteral("Brown"), MapColorCmyk(1, 1, 1, 1), 0, 0.5 },
	    { 7, QStringLiteral("Yellow"), MapColorCmyk(0, 0.27f, 0.79f, 0), 0, 1.0 },
	};
	auto const make_map = [](Map& map) {
		auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0, 0, 0, 0.9f), QStringLiteral("Black"));
		black->setKnockout(true);
		addColor(map, QStringLiteral("brown"), MapColorCmyk(0, 0.5f, 1, 0.2f));
		addColor(map, QStringLiteral("Blue"), MapColorCmyk(1, 0, 0, 0));
	};

	Map map;
	make_map(map);
	auto const report = applyColourSwatch(map, swatch, {});
	QCOMPARE(report.updated, (QStringList{ QStringLiteral("Black"), QStringLiteral("brown") }));
	QCOMPARE(report.not_found, QStringList{ QStringLiteral("Yellow") });
	QVERIFY(report.added.isEmpty());
	QVERIFY(near(map.getColor(0)->getCmyk(), 0, 0, 0, 1));
	QVERIFY(map.getColor(0)->getKnockout());
	QVERIFY(near(map.getColor(1)->getCmyk(), 0.25f, 0.75f, 1, 0));
	QCOMPARE(map.getColor(1)->getOpacity(), 1.0f);
	QCOMPARE(map.getColor(1)->getName(), QStringLiteral("brown"));
	QVERIFY(near(map.getColor(2)->getCmyk(), 1, 0, 0, 0));
	QCOMPARE(map.getNumColors(), 3);

	auto const again = applyColourSwatch(map, swatch, {});
	QCOMPARE(again.unchanged, (QStringList{ QStringLiteral("Black"), QStringLiteral("brown") }));
	QVERIFY(again.updated.isEmpty());

	Map all;
	make_map(all);
	SwatchOptions options;
	options.overprint = true;
	options.opacity = true;
	options.add_missing = true;
	auto const all_report = applyColourSwatch(all, swatch, options);
	QVERIFY(!all.getColor(0)->getKnockout());
	QCOMPARE(all.getColor(1)->getOpacity(), 0.5f);
	QCOMPARE(all_report.added, QStringList{ QStringLiteral("Yellow") });
	QCOMPARE(all.getNumColors(), 4);
	QCOMPARE(all.getColor(3)->getName(), QStringLiteral("Yellow"));
	QVERIFY(near(all.getColor(3)->getCmyk(), 0, 0.27f, 0.79f, 0));
	QVERIFY(!all_report.text().isEmpty());
}

namespace {

QByteArray saveXml(Map& map, bool foom)
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

bool loadXml(Map& map, QByteArray data)
{
	XMLFileFormat format;
	QBuffer buffer(&data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format.makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

}  // namespace

// Blend mode, Accept (1)
void ColourToolsTest::blendModeFile()
{
	Map map;
	addColor(map, QStringLiteral("Multiply"), MapColorCmyk(1, 0, 0, 0))->setBlendMode(MapColor::BlendMultiply);
	addColor(map, QStringLiteral("Darken"), MapColorCmyk(0, 1, 0, 0))->setBlendMode(MapColor::BlendDarken);
	addColor(map, QStringLiteral("Normal"), MapColorCmyk(0, 0, 1, 0));
	
	auto const data = saveXml(map, true);
	QCOMPARE(data.count(" blend="), 2);
	QVERIFY(data.contains(" blend=\"multiply\""));
	QVERIFY(data.contains(" blend=\"darken\""));
	Map loaded;
	QVERIFY(loadXml(loaded, data));
	QCOMPARE(loaded.getNumColors(), 3);
	QCOMPARE(loaded.getColor(0)->getBlendMode(), MapColor::BlendMultiply);
	QCOMPARE(loaded.getColor(1)->getBlendMode(), MapColor::BlendDarken);
	QCOMPARE(loaded.getColor(2)->getBlendMode(), MapColor::BlendNormal);
	
	auto const plain = saveXml(map, false);
	QVERIFY(!plain.isEmpty());
	QVERIFY(!plain.contains(" blend="));
	Map plain_loaded;
	QVERIFY(loadXml(plain_loaded, plain));
	QCOMPARE(plain_loaded.getColor(0)->getBlendMode(), MapColor::BlendNormal);
	
	auto unknown = data;
	unknown.replace(" blend=\"darken\"", " blend=\"screen\"");
	Map unknown_loaded;
	QVERIFY(loadXml(unknown_loaded, unknown));
	QCOMPARE(unknown_loaded.getColor(1)->getBlendMode(), MapColor::BlendNormal);
}

// Blend mode, Accept (2)
void ColourToolsTest::blendModeCopy()
{
	MapColor color(QStringLiteral("Blue"), 0);
	color.setBlendMode(MapColor::BlendMultiply);
	MapColor copy = color;
	QCOMPARE(copy.getBlendMode(), MapColor::BlendMultiply);
	auto* duplicate = color.duplicate();
	QCOMPARE(duplicate->getBlendMode(), MapColor::BlendMultiply);
	QVERIFY(duplicate->equals(color));
	delete duplicate;
	copy.setBlendMode(MapColor::BlendDarken);
	QVERIFY(!copy.equals(color));
}

namespace {

LineSymbol* addLineSymbol(Map& map, int number, const QString& name, const MapColor* color)
{
	auto* symbol = new LineSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setName(name);
	symbol->setColor(color);
	symbol->setLineWidth(0.25);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

QStringList colourNames(const Map& map)
{
	QStringList names;
	for (int i = 0; i < map.getNumColors(); ++i)
		names.append(map.getColor(i)->getName());
	return names;
}

void makeLoadMaps(Map& map, Map& source)
{
	addColor(map, QStringLiteral("Black"), MapColorCmyk(0, 0, 0, 1));
	auto* brown = addColor(map, QStringLiteral("Brown"), MapColorCmyk(0, 0.5f, 1, 0.2f));
	addColor(map, QStringLiteral("Red"), MapColorCmyk(0, 1, 1, 0));
	auto* contour = addLineSymbol(map, 101, QStringLiteral("Contour"), brown);
	map.addObject(new PathObject(contour, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));

	addColor(source, QStringLiteral("brown"), MapColorCmyk(0, 0.56f, 1, 0.18f));
	addColor(source, QStringLiteral("Blue"), MapColorCmyk(1, 0, 0, 0));
	addColor(source, QStringLiteral("Black"), MapColorCmyk(0, 0, 0, 1));
}

MapColor* addMixedColor(Map& map, const QString& name, const MapColor* spot, float factor)
{
	auto* mixed = new MapColor(name, map.getNumColors());
	mixed->setSpotColorComposition({ SpotColorComponent(spot, factor) });
	mixed->setCmykFromSpotColors();
	map.addColor(mixed, map.getNumColors());
	return mixed;
}

}  // namespace

// Load colours from file, Accept (1)
void ColourToolsTest::loadReplaceAndAppend()
{
	{
		Map map, source;
		makeLoadMaps(map, source);
		auto const report = loadColoursFrom(map, source, ColourLoadMode::Append);
		QCOMPARE(report.added, QStringList{ QStringLiteral("Blue") });
		QCOMPARE(report.existing, (QStringList{ QStringLiteral("Brown"), QStringLiteral("Black") }));
		QVERIFY(report.changed.isEmpty());
		QCOMPARE(colourNames(map), (QStringList{ QStringLiteral("Black"), QStringLiteral("Brown"), QStringLiteral("Red"), QStringLiteral("Blue") }));
		QVERIFY(near(map.getColor(1)->getCmyk(), 0, 0.5f, 1, 0.2f));
	}
	{
		Map map, source;
		makeLoadMaps(map, source);
		auto const* brown = map.getColor(1);
		auto const report = loadColoursFrom(map, source, ColourLoadMode::Replace);
		QCOMPARE(report.changed, QStringList{ QStringLiteral("brown") });
		QCOMPARE(report.unchanged, QStringList{ QStringLiteral("Black") });
		QCOMPARE(report.added, QStringList{ QStringLiteral("Blue") });
		QCOMPARE(report.kept, QStringList{ QStringLiteral("Red") });
		QCOMPARE(colourNames(map), (QStringList{ QStringLiteral("brown"), QStringLiteral("Blue"), QStringLiteral("Black"), QStringLiteral("Red") }));
		QCOMPARE(map.getColor(0), brown);
		for (int i = 0; i < map.getNumColors(); ++i)
			QCOMPARE(map.getColor(i)->getPriority(), i);
		QVERIFY(near(brown->getCmyk(), 0, 0.56f, 1, 0.18f));
		auto const* contour = map.getSymbol(0);
		QVERIFY(contour->containsColor(brown));
		QCOMPARE(map.getPart(0)->getObject(0)->getSymbol(), contour);
		QVERIFY(!report.text().isEmpty());
	}
}

// Load colours from file, Accept (2)
void ColourToolsTest::loadSpotComposition()
{
	Map source;
	auto* source_spot = addColor(source, QStringLiteral("PMS 471"), MapColorCmyk(0, 0.6f, 1, 0.2f), QStringLiteral("PMS 471"));
	auto const* source_mixed = addMixedColor(source, QStringLiteral("Brown 50%"), source_spot, 0.5f);

	Map map;
	auto* spot = addColor(map, QStringLiteral("PMS 471"), MapColorCmyk(0, 0.5f, 1, 0.3f), QStringLiteral("PMS 471"));
	auto const report = loadColoursFrom(map, source, ColourLoadMode::Replace);
	QCOMPARE(report.added, QStringList{ QStringLiteral("Brown 50%") });
	QCOMPARE(report.changed, QStringList{ QStringLiteral("PMS 471") });
	QCOMPARE(map.getNumColors(), 2);
	QCOMPARE(map.getColor(0), spot);
	auto const* mixed = map.getColor(1);
	QCOMPARE(mixed->getName(), QStringLiteral("Brown 50%"));
	QCOMPARE(int(mixed->getComponents().size()), 1);
	QCOMPARE(mixed->getComponents().front().spot_color, spot);
	QVERIFY(mixed->getCmyk() == source_mixed->getCmyk());
	QVERIFY(near(spot->getCmyk(), 0, 0.6f, 1, 0.2f));
}

namespace {

using Rows = std::vector<std::pair<int, int>>;

MapColor* addNumbered(Map& map, const QString& name, int number, const MapColorCmyk& cmyk = MapColorCmyk(0, 0, 0, 1))
{
	auto* color = addColor(map, name, cmyk);
	color->setNumber(number);
	return color;
}

ColourCrt table(const Rows& rows)
{
	ColourCrt crt;
	crt.rows = rows;
	return crt;
}

/// The map and the file of Loading through a cross-reference table, Accept (3).
void makeCrtMaps(Map& map, Map& source)
{
	addNumbered(map, QStringLiteral("Black"), 1);
	addNumbered(map, QStringLiteral("Brown"), 2, MapColorCmyk(0, 0.5f, 1, 0.2f));
	addNumbered(map, QStringLiteral("Green"), 3, MapColorCmyk(1, 0, 1, 0));
	addColor(map, QStringLiteral("Red"), MapColorCmyk(0, 1, 1, 0));

	addNumbered(source, QStringLiteral("Schwarz"), 10, MapColorCmyk(0, 0, 0, 1));
	addNumbered(source, QStringLiteral("Braun"), 11, MapColorCmyk(0, 0.56f, 1, 0.18f));
	addNumbered(source, QStringLiteral("Gelb"), 12, MapColorCmyk(0, 0, 1, 0));
	addNumbered(source, QStringLiteral("Grau"), 13, MapColorCmyk(0, 0, 0, 0.5f));
	addNumbered(source, QStringLiteral("Red"), 14, MapColorCmyk(0, 1, 1, 0));
	addColor(source, QStringLiteral("Blau"), MapColorCmyk(1, 0, 0, 0));
}

QByteArray saveOcd(Map& map)
{
	auto const* format = FileFormats.findFormat("OCD");
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool loadOcd(Map& map, QByteArray data)
{
	auto const* format = FileFormats.findFormat("OCD");
	QBuffer buffer(&data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

}  // namespace

// Loading through a cross-reference table, Accept (1)
void ColourToolsTest::crtRead()
{
	auto const expected = Rows{ { 5, 12 }, { 6, 13 }, { 7, -1 }, { 8, 14 } };
	QString message;
	auto crt = readColourCrt("\xef\xbb\xbf" "5\t12\r\n6\t\t13.000\r\n\r\n7;-1\r\n8 14 extra\r\n9\t15.5\r\ntitle\r\n5\t99\r\n", &message);
	QVERIFY(crt.rows == expected);
	QCOMPARE(crt.skipped, 2);
	QVERIFY(!message.isEmpty());
	int target = 0;
	QVERIFY(crt.find(7, &target));
	QCOMPARE(target, -1);
	QVERIFY(!crt.find(9, &target));

	message.clear();
	crt = readColourCrt("5\t12\n6\t\t13.000\n\n7;-1\n8 14 extra\n9\t15.5\ntitle\n5\t99\n", &message);
	QVERIFY(crt.rows == expected);
	QCOMPARE(crt.skipped, 2);

	message.clear();
	crt = readColourCrt("only\r\ntext\r\n", &message);
	QVERIFY(crt.rows.empty());
	QVERIFY(!message.isEmpty());

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto const path = dir.filePath(QStringLiteral("colours.ocdCrt"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("1\t2\r\n3\t4\r\n");
	file.close();
	message.clear();
	crt = readColourCrtFile(path, &message);
	QVERIFY(crt.rows == (Rows{ { 1, 2 }, { 3, 4 } }));
	QVERIFY2(message.isEmpty(), qPrintable(message));
	crt = readColourCrtFile(dir.filePath(QStringLiteral("missing.ocdCrt")), &message);
	QVERIFY(crt.rows.empty());
	QVERIFY(!message.isEmpty());
}

// Loading through a cross-reference table, Accept (2)
void ColourToolsTest::colourNumbers()
{
	Map map;
	addNumbered(map, QStringLiteral("Blue"), 7, MapColorCmyk(1, 0, 0, 0));
	addColor(map, QStringLiteral("Brown"), MapColorCmyk(0, 0.5f, 1, 0.2f));

	auto const data = saveXml(map, true);
	QCOMPARE(data.count(" number="), 1);
	QVERIFY(data.contains(" number=\"7\""));
	Map loaded;
	QVERIFY(loadXml(loaded, data));
	QCOMPARE(loaded.getColor(0)->getNumber(), 7);
	QCOMPARE(loaded.getColor(1)->getNumber(), -1);
	auto const plain = saveXml(map, false);
	QVERIFY(!plain.isEmpty());
	QVERIFY(!plain.contains(" number="));

	MapColor color(QStringLiteral("Blue"), 0);
	color.setNumber(7);
	MapColor copy = color;
	QCOMPARE(copy.getNumber(), 7);
	auto* duplicate = color.duplicate();
	QCOMPARE(duplicate->getNumber(), 7);
	delete duplicate;
	copy.setNumber(8);
	QVERIFY(copy.equals(color));

	Map ocd_map;
	addNumbered(ocd_map, QStringLiteral("Black"), 40, MapColorCmyk(0, 0, 0, 1));
	addNumbered(ocd_map, QStringLiteral("Brown"), 41, MapColorCmyk(0, 0.5f, 1, 0.2f));
	addColor(ocd_map, QStringLiteral("Blue"), MapColorCmyk(1, 0, 0, 0));
	auto const ocd = saveOcd(ocd_map);
	QVERIFY(!ocd.isEmpty());
	Map ocd_loaded;
	QVERIFY(loadOcd(ocd_loaded, ocd));
	QCOMPARE(ocd_loaded.getNumColors(), 3);
	for (int i = 0; i < 3; ++i)
		QCOMPARE(ocd_loaded.getColor(i)->getNumber(), i);
}

// Loading through a cross-reference table, Accept (3)
void ColourToolsTest::crtLoad()
{
	auto const crt = table({ { 10, 1 }, { 11, 2 }, { 12, 9 }, { 13, -1 } });
	{
		Map map, source;
		makeCrtMaps(map, source);
		auto const* black = map.getColor(0);
		auto const* brown = map.getColor(1);
		auto const report = loadColoursFrom(map, source, ColourLoadMode::Replace, &crt);
		QCOMPARE(colourNames(map), (QStringList{ QStringLiteral("Schwarz"), QStringLiteral("Braun"), QStringLiteral("Gelb"),
		                                         QStringLiteral("Red"), QStringLiteral("Blau"), QStringLiteral("Green") }));
		QCOMPARE(map.getColor(0), black);
		QCOMPARE(map.getColor(1), brown);
		QCOMPARE(black->getNumber(), 1);
		QCOMPARE(brown->getNumber(), 2);
		QVERIFY(near(brown->getCmyk(), 0, 0.56f, 1, 0.18f));
		QCOMPARE(map.getColor(2)->getNumber(), 9);
		QCOMPARE(map.getColor(3)->getNumber(), -1);
		QCOMPARE(map.getColor(4)->getNumber(), -1);
		QCOMPARE(map.getColor(5)->getNumber(), 3);
		QCOMPARE(report.changed, (QStringList{ QStringLiteral("Schwarz"), QStringLiteral("Braun") }));
		QCOMPARE(report.unchanged, QStringList{ QStringLiteral("Red") });
		QCOMPARE(report.added, (QStringList{ QStringLiteral("Gelb"), QStringLiteral("Blau") }));
		QCOMPARE(report.left_out, QStringList{ QStringLiteral("Grau") });
		QCOMPARE(report.kept, QStringList{ QStringLiteral("Green") });
		QVERIFY(report.text().contains(QLatin1String("Grau")));
	}
	{
		Map map, source;
		makeCrtMaps(map, source);
		auto const report = loadColoursFrom(map, source, ColourLoadMode::Append, &crt);
		QCOMPARE(colourNames(map), (QStringList{ QStringLiteral("Black"), QStringLiteral("Brown"), QStringLiteral("Green"),
		                                         QStringLiteral("Red"), QStringLiteral("Gelb"), QStringLiteral("Blau") }));
		QCOMPARE(map.getColor(4)->getNumber(), 9);
		QCOMPARE(report.existing, (QStringList{ QStringLiteral("Black"), QStringLiteral("Brown"), QStringLiteral("Red") }));
		QCOMPARE(report.left_out, QStringList{ QStringLiteral("Grau") });
		QVERIFY(near(map.getColor(1)->getCmyk(), 0, 0.5f, 1, 0.2f));
	}
	{
		Map map, source;
		makeCrtMaps(map, source);
		auto const both = table({ { 10, 1 }, { 11, 1 } });
		auto const* brown = map.getColor(1);
		auto const report = loadColoursFrom(map, source, ColourLoadMode::Replace, &both);
		QCOMPARE(map.getColor(0)->getName(), QStringLiteral("Schwarz"));
		QCOMPARE(report.left_out, QStringList{ QStringLiteral("Braun") });
		QVERIFY(report.kept.contains(QStringLiteral("Brown")));
		QVERIFY(near(brown->getCmyk(), 0, 0.5f, 1, 0.2f));
	}
	{
		Map with_empty, without, source_a, source_b;
		makeCrtMaps(with_empty, source_a);
		makeCrtMaps(without, source_b);
		auto const empty = table({});
		auto const a = loadColoursFrom(with_empty, source_a, ColourLoadMode::Replace, &empty);
		auto const b = loadColoursFrom(without, source_b, ColourLoadMode::Replace);
		QCOMPARE(colourNames(with_empty), colourNames(without));
		QCOMPARE(a.text(), b.text());
	}
}

// Colour usage report, Accept (1)
void ColourToolsTest::usageReport()
{
	Map map;
	auto* black = addColor(map, QStringLiteral("Black"), MapColorCmyk(0, 0, 0, 1));
	auto* pms = addColor(map, QStringLiteral("PMS 471"), MapColorCmyk(0, 0.6f, 1, 0.2f), QStringLiteral("PMS 471"));
	auto* mixed = addMixedColor(map, QStringLiteral("Brown 50%"), pms, 0.5f);
	auto* unused_spot = addColor(map, QStringLiteral("PMS 299"), MapColorCmyk(1, 0.2f, 0, 0), QStringLiteral("PMS 299"));
	addMixedColor(map, QStringLiteral("Blue 50%"), unused_spot, 0.5f);
	addColor(map, QStringLiteral("Green"), MapColorCmyk(1, 0, 1, 0));

	auto* road = addLineSymbol(map, 501, QStringLiteral("Road"), black);
	addLineSymbol(map, 502, QStringLiteral("Track"), black);
	auto* contour = addLineSymbol(map, 101, QStringLiteral("Contour"), mixed);
	for (int i = 0; i < 3; ++i)
		map.addObject(new PathObject(road, MapCoordVector{ MapCoord(0.0, double(i)), MapCoord(10.0, double(i)) }));
	map.addObject(new PathObject(contour, MapCoordVector{ MapCoord(0.0, 5.0), MapCoord(10.0, 5.0) }));

	auto const usage = colourUsage(map);
	QCOMPARE(int(usage.size()), 6);
	QCOMPARE(usage[0].symbols, (QStringList{ QStringLiteral("501 Road"), QStringLiteral("502 Track") }));
	QCOMPARE(usage[0].objects, 3);
	QVERIFY(usage[0].used);
	QVERIFY(usage[1].symbols.isEmpty());
	QCOMPARE(usage[1].mixed_into, QStringList{ QStringLiteral("Brown 50%") });
	QVERIFY(usage[1].used);
	QCOMPARE(usage[2].objects, 1);
	QCOMPARE(usage[3].mixed_into, QStringList{ QStringLiteral("Blue 50%") });
	QVERIFY(!usage[3].used);
	QVERIFY(!usage[4].used);
	QVERIFY(!usage[5].used);
	auto const text = colourUsageText(usage);
	QVERIFY2(text.contains(QLatin1String("Black: 2 symbol(s), 3 object(s)")), qPrintable(text));
	QVERIFY(text.contains(QLatin1String("  501 Road\n")));
	QVERIFY(text.contains(QLatin1String("Green: not used")));

	auto const deleted = deleteUnusedColours(map);
	QCOMPARE(deleted, (QStringList{ QStringLiteral("PMS 299"), QStringLiteral("Blue 50%"), QStringLiteral("Green") }));
	QCOMPARE(colourNames(map), (QStringList{ QStringLiteral("Black"), QStringLiteral("PMS 471"), QStringLiteral("Brown 50%") }));
	QVERIFY(deleteUnusedColours(map).isEmpty());
}


QTEST_MAIN(ColourToolsTest)
#include "colour_tools_t.moc"  // IWYU pragma: keep
