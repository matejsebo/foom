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
#include <vector>

#include <QtEndian>
#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "core/extents.h"
#include "core/map.h"
#include "course/course_canvas.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

using namespace OpenOrienteering;


namespace {

/// The record of a canvas, without its folder.
QString ocdText()
{
	return QString::fromUtf8(
	    "<?xml version=\"1.0\"?>\r\n<OcadScript>\r\n<CsCanvas>\r\n"
	    "<ExportFolder>C:\\somewhere\\</ExportFolder>\r\n<Activate>true</Activate>\r\n<IsCourses>true</IsCourses>\r\n"
	    "<UsedSymbols>\r\n<Symbol>704.000</Symbol>\r\n</UsedSymbols>\r\n"
	    "<UsedBackgroundMaps>\r\n<BackgroundMap>Forest Orienteering Map B\xc3\xbcrenflue.ocd</BackgroundMap>\r\n</UsedBackgroundMaps>\r\n"
	    "<Canvas>\r\n<Name>Canvas 1</Name>\r\n<ExportRectangleName pageStatus=\"psSingle\">Whole</ExportRectangleName>\r\n"
	    "<ExportRectangleFrontside>\r\n<L>-142.03</L>\r\n<B>-291.03</B>\r\n<R>255.37</R>\r\n<T>77.26</T>\r\n</ExportRectangleFrontside>\r\n"
	    "<ExportScale>10000</ExportScale>\r\n<CdSymbolsSize>6.0</CdSymbolsSize>\r\n<CdMaxLength>25</CdMaxLength>\r\n"
	    "<BackgroundMaps>\r\n<BackgroundMap visible=\"false\">Forest Orienteering Map B\xc3\xbcrenflue.ocd</BackgroundMap>\r\n</BackgroundMaps>\r\n"
	    "<Symbols>\r\n<Symbol visible=\"false\">704.000</Symbol>\r\n</Symbols>\r\n"
	    "<Courses>\r\n<Course>Easy Course</Course>\r\n"
	    "<Course courseType=\"MapExchange\" mapNumber=\"1\">Map Exchange</Course>\r\n"
	    "<Course courseType=\"MapFlip\" mapNumber=\"1\">Map Flip</Course>\r\n"
	    "<Course courseType=\"MapFlip\" mapNumber=\"2\">Map Flip</Course>\r\n"
	    "<Course>Relay Women</Course>\r\n</Courses>\r\n</Canvas>\r\n</CsCanvas>\r\n</OcadScript>\r\n");
}

CourseCanvases sampleCanvases()
{
	CourseCanvases canvases;
	canvasesFromOcdXml(ocdText().toUtf8(), canvases);
	return canvases;
}

Extent wholeExtent()
{
	Extent extent;
	extent.name = QStringLiteral("Whole");
	extent.rect_mm = QRectF(-142.03, -77.26, 397.40, 368.29);
	return extent;
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

/// The parameter strings of a type in an OCD file of version 9 or later.
std::vector<QByteArray> parameterStrings(const QByteArray& file, qint32 type)
{
	std::vector<QByteArray> found;
	auto const read = [&file](int offset) {
		return offset + 4 <= file.size() ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(file.constData()) + offset) : 0u;
	};
	auto block = int(read(32));
	while (block > 0 && block + 4 + 256 * 16 <= file.size())
	{
		for (int i = 0; i < 256; ++i)
		{
			auto const entry = block + 4 + 16 * i;
			auto const pos = int(read(entry));
			auto const length = int(read(entry + 4));
			if (pos > 0 && length > 0 && qint32(read(entry + 8)) == type && pos + length <= file.size())
			{
				auto bytes = file.mid(pos, length);
				auto const end = bytes.indexOf('\0');
				found.push_back(end < 0 ? bytes : bytes.left(end));
			}
		}
		block = int(read(block));
	}
	return found;
}

}  // namespace



/**
 * @test Tests the canvases: the model, the OCD text, the map file and OCD files.
 *
 * See the design notes, "Canvases: the model, the files and the export".
 */
class CourseCanvasTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void ocdXmlTest();
	void mapFileTest();
	void ocdTest();
	void canvasForTest();
};


void CourseCanvasTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseCanvasTest::ocdXmlTest()
{
	CourseCanvases canvases;
	QVERIFY(canvasesFromOcdXml(ocdText().toUtf8(), canvases));
	QVERIFY(canvases.active);
	QVERIFY(!canvases.by_classes);
	QCOMPARE(canvases.used_symbols, QStringList{ QStringLiteral("704.000") });
	auto const background = QString::fromUtf8("Forest Orienteering Map B\xc3\xbcrenflue.ocd");
	QCOMPARE(canvases.used_templates, QStringList{ background });
	QCOMPARE(int(canvases.canvases.size()), 1);
	auto const& canvas = canvases.canvases[0];
	QCOMPARE(canvas.name, QStringLiteral("Canvas 1"));
	QCOMPARE(canvas.extent, QStringLiteral("Whole"));
	QCOMPARE(canvas.page_status, QStringLiteral("psSingle"));
	QCOMPARE(canvas.scale, 10000u);
	QCOMPARE(canvas.box_size, 6.0);
	QCOMPARE(canvas.max_rows, 25);
	QCOMPARE(canvas.hidden_symbols, QStringList{ QStringLiteral("704.000") });
	QCOMPARE(canvas.hidden_templates, QStringList{ background });
	QCOMPARE(int(canvas.courses.size()), 5);
	QCOMPARE(canvas.courses[0].course, QStringLiteral("Easy Course"));
	QCOMPARE(canvas.courses[0].part, -1);
	QCOMPARE(canvas.courses[1].course, QStringLiteral("Map Exchange"));
	QCOMPARE(canvas.courses[1].part, 0);
	QVERIFY(!canvas.courses[1].flip);
	QCOMPARE(canvas.courses[2].part, 0);
	QVERIFY(canvas.courses[2].flip);
	QCOMPARE(canvas.courses[3].course, QStringLiteral("Map Flip"));
	QCOMPARE(canvas.courses[3].part, 1);
	QCOMPARE(canvas.courses[4].course, QStringLiteral("Relay Women"));
	QCOMPARE(canvas.courses[4].part, -1);
	
	// Written and read again.
	auto const written = canvasesToOcdXml(canvases);
	QVERIFY(written.contains(QLatin1String("<Course courseType=\"MapFlip\" mapNumber=\"2\">Map Flip</Course>\r\n")));
	QVERIFY(written.contains(QLatin1String("<Symbol visible=\"false\">704.000</Symbol>")));
	QVERIFY(written.contains(background));
	CourseCanvases again;
	QVERIFY(canvasesFromOcdXml(written.toUtf8(), again));
	QVERIFY(again == canvases);
	
	// The same text in Windows-1252, as Save as XML writes it, and with a zero behind it as a record has.
	auto bytes = QTextCodec::codecForName("Windows-1252")->fromUnicode(ocdText());
	bytes.append('\0');
	bytes.append("rubbish");
	CourseCanvases from_code_page;
	QVERIFY(canvasesFromOcdXml(bytes, from_code_page));
	QVERIFY(from_code_page == canvases);
	
	// A text which is none.
	CourseCanvases none;
	QVERIFY(!canvasesFromOcdXml("<OcadScript><File.Open/></OcadScript>", none));
	QVERIFY(none.isEmpty());
	
	QCOMPARE(canvasSymbolNumber(704, -1), QStringLiteral("704.000"));
	QCOMPARE(canvasSymbolNumber(722, 1), QStringLiteral("722.001"));
}


/// Accept (2)
void CourseCanvasTest::mapFileTest()
{
	CourseData data;
	QVERIFY(data.isEmpty());
	data.canvases = sampleCanvases();
	data.canvases.by_classes = true;
	data.canvases.canvases[0].page_status = QStringLiteral("psFront");
	QVERIFY(!data.isEmpty());
	QVERIFY(data != CourseData{});
	
	QByteArray bytes;
	{
		QXmlStreamWriter xml(&bytes);
		saveCourseData(data, xml);
	}
	QVERIFY(bytes.contains("<canvases"));
	QXmlStreamReader xml(bytes);
	QVERIFY(xml.readNextStartElement());
	auto const loaded = loadCourseData(xml);
	QVERIFY(!xml.hasError());
	QVERIFY(loaded.canvases == data.canvases);
	QVERIFY(loaded == data);
	
	// Without canvases nothing of them is written.
	CourseData plain;
	Course course;
	course.name = QStringLiteral("A");
	plain.courses.push_back(course);
	bytes.clear();
	{
		QXmlStreamWriter writer(&bytes);
		saveCourseData(plain, writer);
	}
	QVERIFY(!bytes.contains("canvases"));
}


/// Accept (3)
void CourseCanvasTest::ocdTest()
{
	Extent extent;
	QVERIFY(extentFromOcdRecord(QStringLiteral("Whole\tl-3635968\tb-7450368\tr6537472\tt1977856"), extent));
	QCOMPARE(extent.name, QStringLiteral("Whole"));
	QVERIFY(qAbs(extent.rect_mm.left() + 142.03) < 0.001 && qAbs(extent.rect_mm.top() + 77.26) < 0.001);
	QVERIFY(qAbs(extent.rect_mm.width() - 397.40) < 0.001 && qAbs(extent.rect_mm.height() - 368.29) < 0.001);
	QCOMPARE(extentToOcdRecord(wholeExtent()), QStringLiteral("Whole\tl-3635968\tb-7450368\tr6537472\tt1977856"));
	QVERIFY(!extentFromOcdRecord(QStringLiteral("Whole\tl1\tb2"), extent));
	QVERIFY(!extentFromOcdRecord(QString{}, extent));
	
	Map map;
	map.setScaleDenominator(10000);
	auto data = map.courseData();
	Course course;
	course.name = QStringLiteral("Easy Course");
	data.courses.push_back(course);
	data.canvases = sampleCanvases();
	map.setCourseData(data);
	map.setExtents({ wholeExtent() });
	
	auto const written = exportTo(map, "OCD2018");
	QVERIFY(!written.isEmpty());
	auto const canvas_records = parameterStrings(written, 1093);
	QCOMPARE(int(canvas_records.size()), 1);
	QVERIFY(canvas_records[0].contains("<Name>Canvas 1</Name>"));
	// UTF-8, as the record has it: in another encoding the name of the background map is lost.
	QVERIFY(canvas_records[0].contains("B\xc3\xbcrenflue"));
	auto const extent_records = parameterStrings(written, 29);
	QCOMPARE(int(extent_records.size()), 1);
	QCOMPARE(QString::fromUtf8(extent_records[0]), QStringLiteral("Whole\tl-3635968\tb-7450368\tr6537472\tt1977856"));
	
	Map reloaded;
	QVERIFY(importFrom(written, "OCD", reloaded));
	QVERIFY(reloaded.courseData().canvases == data.canvases);
	QCOMPARE(int(reloaded.extents().size()), 1);
	QCOMPARE(reloaded.extents()[0].name, QStringLiteral("Whole"));
	auto const& rect = reloaded.extents()[0].rect_mm;
	QVERIFY(qAbs(rect.left() + 142.03) < 0.01 && qAbs(rect.top() + 77.26) < 0.01);
	QVERIFY(qAbs(rect.width() - 397.40) < 0.01 && qAbs(rect.height() - 368.29) < 0.01);
	
	// Written a second time there is still one record of each.
	auto const again = exportTo(reloaded, "OCD2018");
	QCOMPARE(int(parameterStrings(again, 1093).size()), 1);
	QCOMPARE(int(parameterStrings(again, 29).size()), 1);
}


/// Accept (4)
void CourseCanvasTest::canvasForTest()
{
	auto canvases = sampleCanvases();
	CourseCanvas second;
	second.name = QStringLiteral("Canvas 2");
	CanvasCourse whole;
	whole.course = QStringLiteral("Map Exchange");
	second.courses.push_back(whole);
	canvases.canvases.push_back(second);
	
	auto const nameOf = [&canvases](const char* course, int part) {
		auto const* canvas = canvasFor(canvases, QString::fromLatin1(course), part);
		return canvas ? canvas->name : QString{};
	};
	QCOMPARE(nameOf("Map Exchange", 0), QStringLiteral("Canvas 1"));
	// The other parts have that of the course.
	QCOMPARE(nameOf("Map Exchange", 1), QStringLiteral("Canvas 2"));
	QCOMPARE(nameOf("Map Exchange", -1), QStringLiteral("Canvas 2"));
	QCOMPARE(nameOf("Map Flip", 1), QStringLiteral("Canvas 1"));
	QCOMPARE(nameOf("Map Flip", 2), QString{});
	QCOMPARE(nameOf("Easy Course", -1), QStringLiteral("Canvas 1"));
	QCOMPARE(nameOf("Relay Women", -1), QStringLiteral("Canvas 1"));
	QCOMPARE(nameOf("Difficult Course", -1), QString{});
}


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(CourseCanvasTest)
#include "course_canvas_t.moc"  // IWYU pragma: keep
