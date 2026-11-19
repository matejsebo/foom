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
#include <memory>
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

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
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

/// The three bytes of a symbol record of an OCD file of version 9 or later.
struct CourseBytes
{
	int mode = 0;
	int type = 0;
	int columns = 0;
	
	bool isSet() const { return mode || type || columns; }
	bool operator==(const CourseBytes& other) const { return mode == other.mode && type == other.type && columns == other.columns; }
};

/// The course bytes of the symbols of a file, by symbol number times 1000.
std::multimap<int, CourseBytes> symbolBytes(const QByteArray& data)
{
	std::multimap<int, CourseBytes> result;
	auto block = int(u32(data, 8));
	while (block > 0 && block < data.size())
	{
		for (int i = 0; i < 256; ++i)
		{
			auto const position = int(u32(data, block + 4 + 4 * i));
			if (!position || position + 16 > data.size())
				continue;
			CourseBytes bytes;
			bytes.mode = quint8(data[position + 13]);
			bytes.type = quint8(data[position + 14]);
			bytes.columns = quint8(data[position + 15]);
			result.insert({ int(u32(data, position + 4)), bytes });
		}
		block = int(u32(data, block));
	}
	return result;
}

QByteArray exportTo(const Map& map, const char* format_id)
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

const Symbol* symbolWithNumber(const Map& map, int first, int second)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getNumberComponent(0) == first && symbol->getNumberComponent(1) == second)
			return symbol;
	}
	return nullptr;
}

PointSymbol* addPointSymbol(Map& map, int number)
{
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setInnerRadius(300);
	symbol->setInnerColor(map.getColor(0));
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

}  // namespace



/**
 * @test Tests the course role of symbols: in the symbol, in the map
 *       file and in OCD files.
 *
 * the design notes, "Course role of symbols".
 */
class CourseSymbolTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void symbolTest();
	void mapFileTest();
	void ocdTest();
};


void CourseSymbolTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseSymbolTest::symbolTest()
{
	PointSymbol symbol;
	QCOMPARE(symbol.courseMode(), 0);
	QCOMPARE(symbol.courseType(), 0);
	QCOMPARE(symbol.descriptionColumns(), 0);
	QCOMPARE(symbol.courseRole(), Symbol::NoCourseRole);
	
	// The bytes of OCD course files.
	struct Case { int mode; int type; int columns; Symbol::CourseRole role; };
	const Case cases[] = {
	    { 1, 0, 0, Symbol::CourseStart },
	    { 1, 1, 0, Symbol::CourseControl },
	    { 1, 2, 0, Symbol::CourseFinish },
	    { 1, 3, 0, Symbol::CourseMarkedRoute },
	    { 1, 4, 0, Symbol::CourseDescription },
	    { 1, 5, 0, Symbol::CourseTitle },
	    { 1, 6, 0, Symbol::CourseStartNumber },
	    { 1, 7, 0, Symbol::CourseVariantCode },
	    { 2, 0, 24, Symbol::DescriptionSymbol },
	    { 2, 0, 2, Symbol::DescriptionSymbol },
	    // A type without the mode says nothing, and neither does a type which is not known.
	    { 0, 5, 0, Symbol::NoCourseRole },
	    { 0, 2, 0, Symbol::NoCourseRole },
	    { 1, 9, 0, Symbol::NoCourseRole },
	    { 3, 1, 0, Symbol::NoCourseRole },
	};
	for (auto const& c : cases)
	{
		symbol.setCourseFields(c.mode, c.type, c.columns);
		QCOMPARE(symbol.courseMode(), c.mode);
		QCOMPARE(symbol.courseType(), c.type);
		QCOMPARE(symbol.descriptionColumns(), c.columns);
		QCOMPARE(symbol.courseRole(), c.role);
	}
	
	// The columns of a description symbol.
	symbol.setCourseFields(2, 0, 24);
	QVERIFY(symbol.isForDescriptionColumn(Symbol::DescriptionColumnD));
	QVERIFY(symbol.isForDescriptionColumn(Symbol::DescriptionColumnE));
	QVERIFY(!symbol.isForDescriptionColumn(Symbol::DescriptionColumnC));
	QVERIFY(!symbol.isForDescriptionColumn(Symbol::DescriptionColumnG));
	QCOMPARE(int(Symbol::DescriptionColumnC), 32);
	QCOMPARE(int(Symbol::DescriptionColumnH), 1);
	// Only a description symbol belongs to columns.
	symbol.setCourseFields(1, 1, 24);
	QVERIFY(!symbol.isForDescriptionColumn(Symbol::DescriptionColumnD));
	
	// Setting a role sets the bytes.
	symbol.setCourseRole(Symbol::CourseFinish);
	QCOMPARE(symbol.courseMode(), 1);
	QCOMPARE(symbol.courseType(), 2);
	QCOMPARE(symbol.descriptionColumns(), 0);
	symbol.setCourseRole(Symbol::DescriptionSymbol, Symbol::DescriptionColumnG);
	QCOMPARE(symbol.courseMode(), 2);
	QCOMPARE(symbol.courseType(), 0);
	QCOMPARE(symbol.descriptionColumns(), 2);
	symbol.setCourseRole(Symbol::NoCourseRole);
	QCOMPARE(symbol.courseMode(), 0);
	QCOMPARE(symbol.courseType(), 0);
	QCOMPARE(symbol.descriptionColumns(), 0);
	
	// A copy has them, and they count when symbols are compared.
	symbol.setCourseFields(2, 0, 8);
	std::unique_ptr<Symbol> copy { Symbol::duplicate(symbol) };
	QCOMPARE(copy->courseMode(), 2);
	QCOMPARE(copy->descriptionColumns(), 8);
	QVERIFY(symbol.equals(copy.get()));
	copy->setCourseFields(2, 0, 4);
	QVERIFY(!symbol.equals(copy.get()));
}


/// Accept (2)
void CourseSymbolTest::mapFileTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	addPointSymbol(map, 100);
	
	// Without a course role nothing is written.
	auto const plain = exportTo(map, "XML");
	QVERIFY(!plain.isEmpty());
	QVERIFY(!plain.contains("course_mode"));
	QVERIFY(!plain.contains("course_type"));
	QVERIFY(!plain.contains("description_columns"));
	
	addPointSymbol(map, 703)->setCourseFields(1, 1, 0);
	addPointSymbol(map, 701)->setCourseRole(Symbol::CourseStart);
	addPointSymbol(map, 12)->setCourseFields(2, 0, 2);
	addPointSymbol(map, 704)->setCourseFields(0, 5, 0);
	
	Map reloaded;
	QVERIFY(importFrom(exportTo(map, "XML"), "XML", reloaded));
	QCOMPARE(reloaded.getNumSymbols(), 5);
	QCOMPARE(symbolWithNumber(reloaded, 100, -1)->courseRole(), Symbol::NoCourseRole);
	QCOMPARE(symbolWithNumber(reloaded, 703, -1)->courseRole(), Symbol::CourseControl);
	// The start has the mode 1 and the type 0, so its type is not written; the mode is.
	QCOMPARE(symbolWithNumber(reloaded, 701, -1)->courseRole(), Symbol::CourseStart);
	auto const* description = symbolWithNumber(reloaded, 12, -1);
	QCOMPARE(description->courseRole(), Symbol::DescriptionSymbol);
	QCOMPARE(description->descriptionColumns(), 2);
	auto const* text = symbolWithNumber(reloaded, 704, -1);
	QCOMPARE(text->courseMode(), 0);
	QCOMPARE(text->courseType(), 5);
}


/// Accept (3)
void CourseSymbolTest::ocdTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	addPointSymbol(map, 100);
	addPointSymbol(map, 703)->setCourseFields(1, 1, 0);
	addPointSymbol(map, 12)->setCourseFields(2, 0, 2);
	addPointSymbol(map, 704)->setCourseFields(0, 5, 0);
	
	for (auto const* format : { "OCD2018", "OCD12", "OCD11", "OCD9" })
	{
		auto const written = exportTo(map, format);
		QVERIFY2(!written.isEmpty(), format);
		auto const bytes = symbolBytes(written);
		QCOMPARE(int(bytes.size()), 4);
		QVERIFY2(bytes.find(100000)->second == CourseBytes{}, format);
		QVERIFY2((bytes.find(703000)->second == CourseBytes{ 1, 1, 0 }), format);
		QVERIFY2((bytes.find(12000)->second == CourseBytes{ 2, 0, 2 }), format);
		QVERIFY2((bytes.find(704000)->second == CourseBytes{ 0, 5, 0 }), format);
		
		Map reloaded;
		QVERIFY2(importFrom(written, "OCD", reloaded), format);
		QCOMPARE(symbolWithNumber(reloaded, 100, 0)->courseRole(), Symbol::NoCourseRole);
		QCOMPARE(symbolWithNumber(reloaded, 703, 0)->courseRole(), Symbol::CourseControl);
		QCOMPARE(symbolWithNumber(reloaded, 12, 0)->courseRole(), Symbol::DescriptionSymbol);
		QCOMPARE(symbolWithNumber(reloaded, 12, 0)->descriptionColumns(), 2);
		QCOMPARE(symbolWithNumber(reloaded, 704, 0)->courseType(), 5);
	}
	
	// The symbol record of version 8 has no place for them.
	auto const old_file = exportTo(map, "OCD8");
	QVERIFY(!old_file.isEmpty());
	Map reloaded;
	QVERIFY(importFrom(old_file, "OCD", reloaded));
	for (int i = 0; i < reloaded.getNumSymbols(); ++i)
		QCOMPARE(reloaded.getSymbol(i)->courseMode(), 0);
}



QTEST_MAIN(CourseSymbolTest)
#include "course_symbol_t.moc"  // IWYU pragma: keep
