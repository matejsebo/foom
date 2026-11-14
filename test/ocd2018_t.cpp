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
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"

using namespace OpenOrienteering;


/**
 * Tests the OCD 2018 writer.
 *
 * See the design notes, "OCD 2018 writer".
 */
class Ocd2018Test : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void headerTest();
	void objectFieldsTest();
	
private:
	static void setupMap(Map& map, PointObject** point = nullptr);
	static QByteArray exportTo(const Map& map, const char* format_id, std::vector<QString>* warnings = nullptr);
	static bool import(const QByteArray& data, Map& map, std::vector<QString>* warnings = nullptr);
	
	struct Summary
	{
		std::map<qint32, int> foreign_types;
		std::map<int, int> object_types;
		int elevations = 0;
		int strings = 0;
		int string_bytes = 0;
	};
	static Summary summarize(const Map& map);
};


void Ocd2018Test::initTestCase()
{
	doStaticInitializations();
}

void Ocd2018Test::setupMap(Map& map, PointObject** point)
{
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* dot = new PointSymbol();
	dot->setInnerColor(black);
	dot->setInnerRadius(500);
	dot->setNumberComponent(0, 101);
	map.addSymbol(dot, 0);
	auto* object = new PointObject(dot);
	object->setPosition(MapCoord(10.0, 10.0));
	map.addObject(object);
	if (point)
		*point = object;
}

QByteArray Ocd2018Test::exportTo(const Map& map, const char* format_id, std::vector<QString>* warnings)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return {};
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	if (warnings)
		*warnings = exporter->warnings();
	return buffer.data();
}

bool Ocd2018Test::import(const QByteArray& data, Map& map, std::vector<QString>* warnings)
{
	auto const* format = FileFormats.findFormat("OCD");
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	auto const ok = importer->doImport();
	if (warnings)
		*warnings = importer->warnings();
	return ok;
}

Ocd2018Test::Summary Ocd2018Test::summarize(const Map& map)
{
	Summary summary;
	for (auto const& string : map.foreignData().strings)
	{
		auto own = false;
		for (auto type : { 8, 9, 10, 11, 18, 1030, 1039, 1061 })
			own = own || string.type == type;
		if (!own)
			++summary.foreign_types[string.type];
	}
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			++summary.object_types[object->getType()];
			summary.elevations += !object->getTag(QStringLiteral("ele")).isEmpty();
			auto const string = object->getTag(QStringLiteral("ocd:object_string"));
			summary.strings += !string.isEmpty();
			summary.string_bytes += string.size();
		}
	}
	return summary;
}


void Ocd2018Test::headerTest()
{
	Map map;
	setupMap(map);
	std::vector<QString> warnings;
	auto const data = exportTo(map, "OCD2018", &warnings);
	QVERIFY(data.size() > 8);
	QCOMPARE(quint8(data[0]), quint8(0xad));
	QCOMPARE(quint8(data[1]), quint8(0x0c));
	QCOMPARE(quint8(data[2]), quint8(0));
	QCOMPARE(quint8(data[4]), quint8(0xe2));
	QCOMPARE(quint8(data[5]), quint8(0x07));
	QCOMPARE(quint8(data[6]), quint8(8));
	QCOMPARE(quint8(data[7]), quint8(4));
	
	Map reloaded;
	QVERIFY(import(data, reloaded, &warnings));
	for (auto const& warning : warnings)
	{
		qInfo("import warning: %s", qPrintable(warning));
		QVERIFY(!warning.contains(QLatin1String("not supported")) && !warning.contains(QLatin1String("experimental")));
	}
	QCOMPARE(reloaded.property(OcdFileFormat::versionProperty()).toInt(), 2018);
	QCOMPARE(reloaded.getNumObjects(), 1);
	
	// Saving the reloaded map through the generic OCD format keeps the version.
	auto const again = exportTo(reloaded, "OCD");
	QVERIFY(again.size() > 8);
	QCOMPARE(quint8(again[4]), quint8(0xe2));
	QCOMPARE(quint8(again[5]), quint8(0x07));
}

void Ocd2018Test::objectFieldsTest()
{
	Map map;
	PointObject* point = nullptr;
	setupMap(map, &point);
	point->setTag(QStringLiteral("ele"), QStringLiteral("512.5"));
	point->setTag(QStringLiteral("ocd:object_string_type"), QStringLiteral("1"));
	point->setTag(QStringLiteral("ocd:object_string"), QStringLiteral("a=1\tb=2"));
	auto* plain = new PointObject(map.getSymbol(0)->asPoint());
	plain->setPosition(MapCoord(20.0, 20.0));
	map.addObject(plain);
	
	Map reloaded;
	QVERIFY(import(exportTo(map, "OCD2018"), reloaded));
	QCOMPARE(reloaded.getNumObjects(), 2);
	auto tagged = 0;
	for (int i = 0; i < reloaded.getPart(0)->getNumObjects(); ++i)
	{
		auto const* object = reloaded.getPart(0)->getObject(i);
		if (object->getTag(QStringLiteral("ele")).isEmpty())
		{
			QVERIFY(object->getTag(QStringLiteral("ocd:object_string")).isEmpty());
			QVERIFY(object->getTag(QStringLiteral("ocd:object_string_type")).isEmpty());
			continue;
		}
		++tagged;
		QCOMPARE(object->getTag(QStringLiteral("ele")), QStringLiteral("512.5"));
		QCOMPARE(object->getTag(QStringLiteral("ocd:object_string_type")), QStringLiteral("1"));
		QCOMPARE(object->getTag(QStringLiteral("ocd:object_string")), QStringLiteral("a=1\tb=2"));
	}
	QCOMPARE(tagged, 1);
}


QTEST_MAIN(Ocd2018Test)
#include "ocd2018_t.moc"  // IWYU pragma: keep
