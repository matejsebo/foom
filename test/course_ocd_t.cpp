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


#include <cstring>
#include <map>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "global.h"
#include "core/foreign_data.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "course/ocd_course_records.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

using namespace OpenOrienteering;


namespace {

quint32 u32(const QByteArray& data, int position)
{
	quint32 value = 0;
	if (position >= 0 && position + 4 <= data.size())
		memcpy(&value, data.constData() + position, 4);
	return value;  // the tests run on little endian machines, as the format is
}

quint16 u16(const QByteArray& data, int position)
{
	quint16 value = 0;
	if (position >= 0 && position + 2 <= data.size())
		memcpy(&value, data.constData() + position, 2);
	return value;
}

/// What the index and the header of an object of an OCD 12 file say about it.
struct ObjectRecord
{
	int symbol = 0;
	int status = 0;
	int view_type = 0;
	int string_type = 0;
	QString string;
};

std::vector<ObjectRecord> objectRecords(const QByteArray& data)
{
	std::vector<ObjectRecord> records;
	auto block = int(u32(data, 12));
	while (block > 0 && block < data.size())
	{
		for (int i = 0; i < 256; ++i)
		{
			auto const entry = block + 4 + 40 * i;
			auto const position = int(u32(data, entry + 16));
			auto const size = int(u32(data, entry + 20));
			if (!position || !size)
				continue;
			ObjectRecord record;
			record.symbol = int(u32(data, entry + 24));
			record.status = quint8(data[entry + 30]);
			record.view_type = quint8(data[entry + 31]);
			auto const num_items = int(u32(data, position + 44));
			auto const num_text = int(u16(data, position + 48));
			auto const string_units = int(u16(data, position + 50));
			if (string_units)
			{
				record.string_type = quint8(data[position + 54]);
				auto const start = position + 56 + 8 * (num_items + num_text);
				for (int at = start; at + 1 < start + 8 * string_units; at += 2)
				{
					auto const unit = u16(data, at);
					if (!unit)
						break;
					record.string.append(QChar(unit));
				}
			}
			records.push_back(record);
		}
		block = int(u32(data, block));
	}
	return records;
}

/// The parameter strings of some types, as "type text".
QStringList parameterStrings(const QByteArray& data, std::initializer_list<int> types)
{
	QStringList strings;
	auto block = int(u32(data, 32));
	while (block > 0 && block < data.size())
	{
		for (int i = 0; i < 256; ++i)
		{
			auto const entry = block + 4 + 16 * i;
			auto const position = int(u32(data, entry));
			auto const size = int(u32(data, entry + 4));
			auto const type = int(u32(data, entry + 8));
			if (!position || !size)
				continue;
			for (auto wanted : types)
			{
				if (type == wanted)
				{
					auto const raw = data.mid(position, size);
					auto const end = raw.indexOf('\0');
					strings.push_back(QString::number(type) + QLatin1Char(' ') + QString::fromUtf8(raw.constData(), end < 0 ? raw.size() : end));
				}
			}
		}
		block = int(u32(data, block));
	}
	return strings;
}

/// A record with its fields in the order in which FOOM writes them.
QString canonical(int type, const QString& record)
{
	switch (type)
	{
	case 1:
	case 31:
		{
			CourseObject object;
			return OcdCourse::parseObject(record, object) ? OcdCourse::formatObject(object) : QStringLiteral("?") + record;
		}
	case 2:
		{
			Course course;
			return OcdCourse::parseCourse(record, course) ? OcdCourse::formatCourse(course) : QStringLiteral("?") + record;
		}
	case 3:
		{
			CourseClass course_class;
			return OcdCourse::parseClass(record, course_class) ? OcdCourse::formatClass(course_class) : QStringLiteral("?") + record;
		}
	case 1031:
		return OcdCourse::formatOptions(OcdCourse::parseOptions(record));
	default:
		return record;
	}
}

/// The objects of a file as sorted lines, the course records in FOOM's field order.
QStringList objectLines(const QByteArray& data)
{
	QStringList lines;
	for (auto const& record : objectRecords(data))
	{
		QString string = record.string;
		if (record.string_type == 1)
			string = canonical(1, record.string);
		else if (record.string_type == 2)
		{
			CourseOverride override;
			string = OcdCourse::parseOverride(record.string, override) ? OcdCourse::formatOverride(override) : QStringLiteral("?") + record.string;
		}
		lines.push_back(QStringLiteral("symbol %1 status %2 view %3 string %4: %5")
		                .arg(record.symbol).arg(record.status).arg(record.view_type).arg(record.string_type).arg(string));
	}
	lines.sort();
	return lines;
}

QStringList stringLines(const QByteArray& data)
{
	QStringList lines;
	for (auto const& line : parameterStrings(data, { 2, 3, 31, 1031 }))
	{
		auto const space = line.indexOf(QLatin1Char(' '));
		auto const type = line.left(space).toInt();
		lines.push_back(QString::number(type) + QLatin1Char(' ') + canonical(type, line.mid(space + 1)));
	}
	lines.sort();
	return lines;
}

/// The course data without the uuids of map objects, which an OCD file does not keep.
CourseData withoutObjects(CourseData data)
{
	for (auto& object : data.objects)
		object.object = QUuid();
	for (auto& override : data.overrides)
		override.object = QUuid();
	return data;
}

QByteArray exportTo(const Map& map, const char* format_id, std::vector<QString>* warnings = nullptr)
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

bool importFrom(const QByteArray& data, const char* format_id, Map& map)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format->makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

}  // namespace



/**
 * @test Tests the course data in OCD files: the part of the overrides,
 *       and writing from the course data.
 *
 * the design notes, "OCD files from the course data".
 */
class CourseOcdTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void roleTest();
	void fromScratchTest();
};


void CourseOcdTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseOcdTest::roleTest()
{
	QCOMPARE(MapPart::roleName(MapPart::Role::Course), QStringLiteral("course"));
	auto ok = false;
	QCOMPARE(MapPart::roleFromName(QStringLiteral("course"), &ok), MapPart::Role::Course);
	QVERIFY(ok);
	
	Map map;
	QCOMPARE(findCourseOverridesPart(map), -1);
	auto* part = ensureCourseOverridesPart(map);
	QVERIFY(part);
	QCOMPARE(part->role(), MapPart::Role::Course);
	QVERIFY(!part->isVisible());
	QCOMPARE(map.getNumParts(), 2);
	QCOMPARE(findCourseOverridesPart(map), 1);
	// It is made once.
	QCOMPARE(ensureCourseOverridesPart(map), part);
	QCOMPARE(map.getNumParts(), 2);
	// The part in which is drawn stays the one it was.
	QCOMPARE(map.getCurrentPartIndex(), std::size_t(0));
	
	// Role and hidden state are kept in the map file.
	Map reloaded;
	QVERIFY(importFrom(exportTo(map, "XML"), "XML", reloaded));
	QCOMPARE(reloaded.getNumParts(), 2);
	QCOMPARE(reloaded.getPart(1)->role(), MapPart::Role::Course);
	QVERIFY(!reloaded.getPart(1)->isVisible());
}





/// Accept (5)
void CourseOcdTest::fromScratchTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, 703);
	symbol->setInnerRadius(300);
	symbol->setInnerColor(black);
	map.addSymbol(symbol, 0);
	
	auto* control = new PointObject(symbol);
	control->setPosition(MapCoord(10.0, 10.0));
	map.addObject(control);
	auto* plain = new PointObject(symbol);
	plain->setPosition(MapCoord(20.0, 10.0));
	map.addObject(plain);
	auto* number = new PointObject(symbol);
	number->setPosition(MapCoord(12.0, 8.0));
	ensureCourseOverridesPart(map)->addObject(number);
	// A course object whose map object is gone.
	auto const gone = QUuid::createUuid();
	
	CourseData data;
	data.options.setValue(QLatin1Char('e'), QStringLiteral("Training"));
	data.options.setValue(QLatin1Char('s'), QStringLiteral("5.00"));
	CourseObject control_object;
	control_object.kind = CourseObject::Control;
	control_object.code = QStringLiteral("31");
	control_object.object = control->uuid();
	control_object.cell[CourseObject::ColumnD] = 1010;
	control_object.text = QStringLiteral("K\u0159i\u017eovatka");
	data.objects.push_back(control_object);
	CourseObject lost;
	lost.kind = CourseObject::Control;
	lost.code = QStringLiteral("32");
	lost.object = gone;
	data.objects.push_back(lost);
	CourseObject block;
	block.kind = CourseObject::TextBlock;
	block.code = QStringLiteral("T1");
	block.text = QStringLiteral("Have fun");
	data.objects.push_back(block);
	Course course;
	OcdCourse::parseCourse(QStringLiteral("Short\tc31\ttT1\tC5"), course);
	data.courses.push_back(course);
	CourseClass course_class;
	course_class.name = QStringLiteral("Open");
	course_class.course = QStringLiteral("Short");
	data.classes.push_back(course_class);
	CourseOverride override;
	override.course = QStringLiteral("Short");
	override.code_from = QStringLiteral("31");
	override.object = number->uuid();
	data.overrides.push_back(override);
	map.setCourseData(data);
	
	std::vector<QString> warnings;
	auto const written = exportTo(map, "OCD2018", &warnings);
	QVERIFY(!written.isEmpty());
	auto lost_warnings = 0;
	for (auto const& warning : warnings)
		lost_warnings += warning.contains(QLatin1String("no map object"));
	QCOMPARE(lost_warnings, 1);
	
	auto const objects = objectLines(written);
	QCOMPARE(objects.size(), 3);
	QVERIFY2(objects.contains(QStringLiteral("symbol 703000 status 1 view 1 string 1: 1031\tYc\ta31\td1.010\ttK\u0159i\u017eovatka")), qPrintable(objects.join(QLatin1Char('\n'))));
	QVERIFY2(objects.contains(QStringLiteral("symbol 703000 status 2 view 2 string 2: Short\td31")), qPrintable(objects.join(QLatin1Char('\n'))));
	QVERIFY2(objects.contains(QStringLiteral("symbol 703000 status 1 view 0 string 0: ")), qPrintable(objects.join(QLatin1Char('\n'))));
	auto const strings = stringLines(written);
	QCOMPARE(strings, (QStringList{ QStringLiteral("1031 \teTraining\ts5.00"),
	                                QStringLiteral("2 Short\tc31\ttT1\tC5"),
	                                QStringLiteral("3 Open\tcShort\tr\tf\tt"),
	                                QStringLiteral("31 80T1\tYt\taT1\ttHave fun") }));
	
	Map reloaded;
	QVERIFY(importFrom(written, "OCD", reloaded));
	// The course object without a map object was not written.
	auto expected = withoutObjects(data);
	expected.objects.erase(expected.objects.begin() + 1);
	// The reader takes the course objects first, then the text blocks; here that is the order they had.
	QVERIFY(withoutObjects(reloaded.courseData()) == expected);
	auto const index = findCourseOverridesPart(reloaded);
	QVERIFY(index > 0);
	QCOMPARE(reloaded.getPart(std::size_t(index))->getNumObjects(), 1);
	QVERIFY(!reloaded.getPart(std::size_t(index))->isVisible());
	QCOMPARE(reloaded.getPart(0)->getNumObjects(), 2);
	QCOMPARE(int(quint8(written[2])), 1);
	
	// Without course data the file is a map file.
	map.setCourseData({});
	auto const plain_file = exportTo(map, "OCD2018");
	QVERIFY(!plain_file.isEmpty());
	QCOMPARE(int(quint8(plain_file[2])), 0);
	
	// A course project which has nothing but its options yet.
	CourseData only_options;
	only_options.options.setValue(QLatin1Char('e'), QStringLiteral("Next year"));
	map.setCourseData(only_options);
	auto const new_project = exportTo(map, "OCD2018");
	QCOMPARE(int(quint8(new_project[2])), 1);
	Map project;
	QVERIFY(importFrom(new_project, "OCD", project));
	QVERIFY(project.courseData() == only_options);
	// The same strings in a file which says that it is a map are no course data.
	auto as_map = new_project;
	as_map[2] = 0;
	Map no_project;
	QVERIFY(importFrom(as_map, "OCD", no_project));
	QVERIFY(no_project.courseData().isEmpty());
	// An OCD version before 12 cannot hold course data, so it is a map file.
	auto const old_file = exportTo(map, "OCD11");
	QVERIFY(!old_file.isEmpty());
	QCOMPARE(int(quint8(old_file[2])), 0);
}


QTEST_MAIN(CourseOcdTest)
#include "course_ocd_t.moc"  // IWYU pragma: keep
