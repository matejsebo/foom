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
#include <map>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QString>

#include "global.h"
#include "core/foreign_data.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"

using namespace OpenOrienteering;


/**
 * Tests the pass-through of unread OCD data.
 *
 * See the design notes, "OCD round trip".
 */
class ForeignDataTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void xmlRoundTripTest();
	void ocdWriteBackTest();
	
private:
	static void setupMap(Map& map);
	static ForeignData sampleData();
	static bool roundTrip(const Map& map, const char* format_id, Map& reloaded, std::vector<QString>& export_warnings, std::vector<QString>& import_warnings);
};


void ForeignDataTest::initTestCase()
{
	doStaticInitializations();
}

void ForeignDataTest::setupMap(Map& map)
{
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setColor(black);
	line->setLineWidth(0.3);
	line->setNumberComponent(0, 501);
	map.addSymbol(line, 0);
	map.addObject(new PathObject(line, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) }));
}

ForeignData ForeignDataTest::sampleData()
{
	ForeignData data;
	data.format = QStringLiteral("OCD");
	data.version = 12;
	ForeignString string;
	string.type = 9999;
	string.object_index = 7;
	string.data = QByteArray("a=1\tb=2", 8);  // with the terminating zero
	data.strings.push_back(string);
	ForeignString second;
	second.type = 1098;
	second.data = QByteArray("x", 2);
	data.strings.push_back(second);
	ForeignObject object;
	object.entry = QByteArray(40, '\x01');
	object.data = QByteArray(64, '\x02');
	data.objects.push_back(object);
	return data;
}

bool ForeignDataTest::roundTrip(const Map& map, const char* format_id, Map& reloaded, std::vector<QString>& export_warnings, std::vector<QString>& import_warnings)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	if (!buffer.open(QIODevice::ReadWrite))
		return false;
	auto exporter = format->makeExporter({}, &map, nullptr);
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
	return true;
}


void ForeignDataTest::xmlRoundTripTest()
{
	Map map;
	setupMap(map);
	map.setForeignData(sampleData());
	std::vector<QString> export_warnings;
	std::vector<QString> import_warnings;
	Map reloaded;
	QVERIFY(roundTrip(map, "XML", reloaded, export_warnings, import_warnings));
	auto const& foreign = reloaded.foreignData();
	QCOMPARE(foreign.format, QStringLiteral("OCD"));
	QCOMPARE(foreign.version, 12);
	QCOMPARE(foreign.strings.size(), std::size_t(2));
	QCOMPARE(foreign.strings[0].type, 9999);
	QCOMPARE(foreign.strings[0].object_index, quint32(7));
	QCOMPARE(foreign.strings[0].data, QByteArray("a=1\tb=2", 8));
	QCOMPARE(foreign.strings[1].type, 1098);
	QCOMPARE(foreign.strings[1].object_index, quint32(0));
	QCOMPARE(foreign.objects.size(), std::size_t(1));
	QCOMPARE(foreign.objects[0].entry, QByteArray(40, '\x01'));
	QCOMPARE(foreign.objects[0].data, QByteArray(64, '\x02'));
	
	Map plain;
	setupMap(plain);
	auto const* format = FileFormats.findFormat("XML");
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format->makeExporter({}, &plain, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	QVERIFY(!QString::fromUtf8(buffer.data()).contains(QLatin1String("<foreign")));
}

void ForeignDataTest::ocdWriteBackTest()
{
	Map map;
	setupMap(map);
	auto data = sampleData();
	data.objects.clear();  // the fake record would not be a valid object
	map.setForeignData(data);
	std::vector<QString> export_warnings;
	std::vector<QString> import_warnings;
	
	Map reloaded;
	QVERIFY(roundTrip(map, OcdFileFormat::idForVersion(12), reloaded, export_warnings, import_warnings));
	for (auto const& warning : export_warnings)
		qInfo("export warning: %s", qPrintable(warning));
	auto const& foreign = reloaded.foreignData();
	QCOMPARE(foreign.version, 12);
	const ForeignString* found = nullptr;
	for (auto const& string : foreign.strings)
	{
		if (string.type == 9999)
			found = &string;
	}
	QVERIFY(found);
	QCOMPARE(found->object_index, quint32(7));
	QCOMPARE(found->data, QByteArray("a=1\tb=2", 8));
	
	Map other_version;
	QVERIFY(roundTrip(map, OcdFileFormat::idForVersion(9), other_version, export_warnings, import_warnings));
	auto version_warning = false;
	for (auto const& warning : export_warnings)
	{
		qInfo("export warning: %s", qPrintable(warning));
		version_warning = version_warning || warning.contains(QLatin1String("version"));
	}
	QVERIFY(version_warning);
	for (auto const& string : other_version.foreignData().strings)
		QVERIFY(string.type != 9999);
}


QTEST_MAIN(ForeignDataTest)
#include "foreign_data_t.moc"  // IWYU pragma: keep
