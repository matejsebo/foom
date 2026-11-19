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
#include <QStringList>

#include "global.h"
#include "core/foreign_data.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/ocd_course_records.h"
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
		QStringList object_strings;  ///< sorted
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
			if (!string.isEmpty())
				summary.object_strings.push_back(object->getTag(QStringLiteral("ocd:object_string_type")) + QLatin1Char(' ') + string);
		}
	}
	// The records of course objects and overrides are course data since the importer puts them there.
	for (auto const& object : map.courseData().objects)
	{
		if (object.kind == CourseObject::TextBlock)
			continue;
		auto const string = OcdCourse::formatObject(object);
		++summary.strings;
		summary.string_bytes += string.size();
		summary.object_strings.push_back(QStringLiteral("1 ") + string);
	}
	for (auto const& override : map.courseData().overrides)
	{
		auto const string = OcdCourse::formatOverride(override);
		++summary.strings;
		summary.string_bytes += string.size();
		summary.object_strings.push_back(QStringLiteral("2 ") + string);
	}
	summary.object_strings.sort();
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
	// The string of 32 characters needs a second block of 64 bytes for its terminating zero.
	auto const short_string = QStringLiteral("a=1\tb=2");
	auto const long_string = QString(QStringLiteral("1099\tYc\ta99\tt") + QString(19, QChar(0x159)));
	QCOMPARE(long_string.size(), 32);

	Map map;
	PointObject* point = nullptr;
	setupMap(map, &point);
	point->setTag(QStringLiteral("ele"), QStringLiteral("512.5"));
	point->setTag(QStringLiteral("ocd:object_string_type"), QStringLiteral("1"));
	point->setTag(QStringLiteral("ocd:object_string"), short_string);
	auto* plain = new PointObject(map.getSymbol(0)->asPoint());
	plain->setPosition(MapCoord(20.0, 20.0));
	map.addObject(plain);
	auto* second = new PointObject(map.getSymbol(0)->asPoint());
	second->setPosition(MapCoord(30.0, 20.0));
	second->setTag(QStringLiteral("ocd:object_string_type"), QStringLiteral("2"));
	second->setTag(QStringLiteral("ocd:object_string"), long_string);
	map.addObject(second);

	auto const data = exportTo(map, "OCD2018");

	// In the file the string is UTF-16 with a terminating zero, in an area which
	// is a multiple of 64 bytes, and the header counts that area in units of 8 bytes.
	auto const utf16 = [](const QString& text) {
		QByteArray bytes;
		for (auto c : text)
		{
			bytes.append(char(c.unicode() & 0xff));
			bytes.append(char(c.unicode() >> 8));
		}
		return bytes;
	};
	auto const field_at = [&data](int position) {
		return int(quint8(data[position])) | (int(quint8(data[position + 1])) << 8);
	};
	{
		auto const bytes = utf16(short_string);
		auto const at = data.indexOf(bytes);
		QVERIFY(at > 64);
		QCOMPARE(data.mid(at + bytes.size(), 64 - bytes.size()), QByteArray(64 - bytes.size(), '\0'));
		// A point object has one coordinate between its header of 56 bytes and the string.
		auto const header = at - 8 - 56;
		QCOMPARE(field_at(header + 50), 8);
		QCOMPARE(int(quint8(data[header + 54])), 1);
	}
	{
		auto const bytes = utf16(long_string);
		QCOMPARE(bytes.size(), 64);
		auto const at = data.indexOf(bytes);
		QVERIFY(at > 64);
		QCOMPARE(data.mid(at + 64, 64), QByteArray(64, '\0'));
		auto const header = at - 8 - 56;
		QCOMPARE(field_at(header + 50), 16);
		QCOMPARE(int(quint8(data[header + 54])), 2);
	}

	Map reloaded;
	QVERIFY(import(data, reloaded));
	QCOMPARE(reloaded.getNumObjects(), 3);
	auto with_short = 0;
	auto with_long = 0;
	auto without = 0;
	// An object with a string of type 2 is read into the hidden part of the overrides
	// (the design notes, "OCD files from the course data"), so all parts are looked at.
	QCOMPARE(reloaded.getNumParts(), 2);
	QCOMPARE(reloaded.getPart(1)->role(), MapPart::Role::Course);
	QCOMPARE(reloaded.getPart(1)->getNumObjects(), 1);
	std::vector<const Object*> objects;
	for (int p = 0; p < reloaded.getNumParts(); ++p)
	{
		for (int i = 0; i < reloaded.getPart(std::size_t(p))->getNumObjects(); ++i)
			objects.push_back(reloaded.getPart(std::size_t(p))->getObject(i));
	}
	for (auto const* object : objects)
	{
		auto const string = object->getTag(QStringLiteral("ocd:object_string"));
		if (string == short_string)
		{
			++with_short;
			QCOMPARE(object->getTag(QStringLiteral("ele")), QStringLiteral("512.5"));
			QCOMPARE(object->getTag(QStringLiteral("ocd:object_string_type")), QStringLiteral("1"));
		}
		else if (string == long_string)
		{
			++with_long;
			QVERIFY(object->getTag(QStringLiteral("ele")).isEmpty());
			QCOMPARE(object->getTag(QStringLiteral("ocd:object_string_type")), QStringLiteral("2"));
		}
		else
		{
			++without;
			QVERIFY2(string.isEmpty(), qPrintable(string));
			QVERIFY(object->getTag(QStringLiteral("ocd:object_string_type")).isEmpty());
		}
	}
	QCOMPARE(with_short, 1);
	QCOMPARE(with_long, 1);
	QCOMPARE(without, 1);
}


QTEST_MAIN(Ocd2018Test)
#include "ocd2018_t.moc"  // IWYU pragma: keep
