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
#include <functional>
#include <vector>

#include <QtEndian>
#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QIODevice>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_canvas.h"
#include "course/course_data.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/map/course_canvases_dialog.h"
#include "gui/map/course_maps_dialog.h"

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
	void dialogTest();
	void xmlScriptTest();
	void defineExtentTest();
	void ocdDefaultsTest();
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
	
	// A canvas by classes with all controls.
	auto const by_classes = QStringLiteral(
	    "<?xml version=\"1.0\"?>\r\n<OcadScript>\r\n<CsCanvas>\r\n<Activate>true</Activate>\r\n<IsCourses>false</IsCourses>\r\n"
	    "<UsedSymbols>\r\n</UsedSymbols>\r\n<UsedBackgroundMaps>\r\n</UsedBackgroundMaps>\r\n"
	    "<Canvas>\r\n<Name>Canvas 1</Name>\r\n<ExportRectangleName pageStatus=\"psSingle\"></ExportRectangleName>\r\n"
	    "<ExportScale>10000</ExportScale>\r\n<CdSymbolsSize>6.0</CdSymbolsSize>\r\n<CdMaxLength>25</CdMaxLength>\r\n"
	    "<Classes>\r\n<Class>All controls</Class>\r\n<Class>M12</Class>\r\n</Classes>\r\n</Canvas>\r\n</CsCanvas>\r\n</OcadScript>\r\n");
	CourseCanvases classes;
	QVERIFY(canvasesFromOcdXml(by_classes.toUtf8(), classes));
	QVERIFY(classes.by_classes);
	QCOMPARE(int(classes.canvases.size()), 1);
	QCOMPARE(int(classes.canvases[0].courses.size()), 2);
	QVERIFY(classes.canvases[0].courses[0].course.isEmpty());
	QCOMPARE(classes.canvases[0].courses[1].course, QStringLiteral("M12"));
	QCOMPARE(classes.canvases[0].courses[1].part, -1);
	auto const classes_written = canvasesToOcdXml(classes);
	QVERIFY(classes_written.contains(QLatin1String("<Classes>\r\n<Class>All controls</Class>\r\n<Class>M12</Class>\r\n</Classes>\r\n")));
	QVERIFY(!classes_written.contains(QLatin1String("<Courses>")));
	CourseCanvases classes_again;
	QVERIFY(canvasesFromOcdXml(classes_written.toUtf8(), classes_again));
	QVERIFY(classes_again == classes);
	
	// All controls by courses.
	auto with_all = canvases;
	with_all.canvases[0].courses.insert(with_all.canvases[0].courses.begin(), CanvasCourse{});
	auto const all_written = canvasesToOcdXml(with_all);
	QVERIFY(all_written.contains(QLatin1String("<Courses>\r\n<Course>All controls</Course>\r\n")));
	CourseCanvases all_again;
	QVERIFY(canvasesFromOcdXml(all_written.toUtf8(), all_again));
	QVERIFY(all_again.canvases[0].courses[0].course.isEmpty());
	QVERIFY(all_again == with_all);
	
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


/// The dialog, Accept (1) to (3)
void CourseCanvasTest::dialogTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto* control = new PointSymbol();
	control->setNumberComponent(0, 703);
	control->setCourseRole(Symbol::CourseControl);
	map.addSymbol(control, 0);
	auto* number = new TextSymbol();
	number->setNumberComponent(0, 704);
	map.addSymbol(number, 1);
	auto* plain_symbol = new PointSymbol();
	plain_symbol->setNumberComponent(0, 101);
	map.addSymbol(plain_symbol, 2);

	auto const itemOf = [](CourseItem::Type type, const char* code) {
		CourseItem item;
		item.type = type;
		item.code = QString::fromLatin1(code);
		return item;
	};
	CourseData data;
	Course plain;
	plain.name = QStringLiteral("Plain");
	plain.items = { itemOf(CourseItem::Start, "S1"), itemOf(CourseItem::Control, "31"), itemOf(CourseItem::Finish, "F1") };
	Course exchange;
	exchange.name = QStringLiteral("X");
	exchange.items = { itemOf(CourseItem::Start, "S1"), itemOf(CourseItem::Control, "31"), itemOf(CourseItem::MapExchange, ""),
	                   itemOf(CourseItem::Start, "S2"), itemOf(CourseItem::Control, "32"), itemOf(CourseItem::Finish, "F1") };
	data.courses = { plain, exchange };
	map.setCourseData(data);
	Extent sheet;
	sheet.name = QStringLiteral("Sheet");
	sheet.rect_mm = QRectF(0.0, 0.0, 100.0, 80.0);
	map.setExtents({ sheet });

	CourseCanvasesDialog dialog(&map);
	auto* list = dialog.findChild<QListWidget*>(QStringLiteral("canvases-list"));
	auto* courses = dialog.findChild<QListWidget*>(QStringLiteral("canvas-courses"));
	auto* symbols = dialog.findChild<QListWidget*>(QStringLiteral("canvas-symbols"));
	auto* scale = dialog.findChild<QSpinBox*>(QStringLiteral("canvas-scale"));
	auto* extent = dialog.findChild<QComboBox*>(QStringLiteral("canvas-extent"));
	auto* box = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("canvas-box"));
	auto* rows = dialog.findChild<QSpinBox*>(QStringLiteral("canvas-rows"));
	auto* active = dialog.findChild<QCheckBox*>(QStringLiteral("canvases-active"));
	QVERIFY(list && courses && symbols && scale && extent && box && rows && active);

	// (1)
	QCOMPARE(list->count(), 0);
	QVERIFY(!scale->isEnabled());
	QVERIFY(dialog.canvases().isEmpty());
	QVERIFY(dialog.addCanvas(QStringLiteral("A")));
	QVERIFY(!dialog.addCanvas(QStringLiteral(" A ")));
	QVERIFY(!dialog.addCanvas(QStringLiteral("  ")));
	QCOMPARE(list->count(), 1);
	QCOMPARE(dialog.currentCanvas(), 0);
	QVERIFY(scale->isEnabled());
	QStringList texts;
	for (int i = 0; i < courses->count(); ++i)
		texts.push_back(courses->item(i)->text());
	QCOMPARE(texts, (QStringList{ CourseCanvasesDialog::tr("All controls"), QStringLiteral("Plain"),
	                              CourseCanvasesDialog::tr("%1 - Map exchange #%2").arg(QStringLiteral("X")).arg(1),
	                              CourseCanvasesDialog::tr("%1 - Map exchange #%2").arg(QStringLiteral("X")).arg(2) }));
	QCOMPARE(symbols->count(), 2);
	QVERIFY(symbols->item(0)->text().startsWith(QLatin1String("703")));
	QVERIFY(symbols->item(1)->text().startsWith(QLatin1String("704")));
	QCOMPARE(extent->count(), 2);

	// (2)
	courses->item(2)->setCheckState(Qt::Checked);
	QCOMPARE(int(dialog.canvases().canvases[0].courses.size()), 1);
	QCOMPARE(dialog.canvases().canvases[0].courses[0].course, QStringLiteral("X"));
	QCOMPARE(dialog.canvases().canvases[0].courses[0].part, 0);
	QVERIFY(!dialog.canvases().canvases[0].courses[0].flip);
	courses->item(1)->setCheckState(Qt::Checked);
	QCOMPARE(int(dialog.canvases().canvases[0].courses.size()), 2);

	QVERIFY(dialog.addCanvas(QStringLiteral("B")));
	QCOMPARE(dialog.currentCanvas(), 1);
	QCOMPARE(courses->item(2)->checkState(), Qt::Unchecked);
	courses->item(2)->setCheckState(Qt::Checked);
	QCOMPARE(int(dialog.canvases().canvases[0].courses.size()), 1);
	QCOMPARE(dialog.canvases().canvases[0].courses[0].course, QStringLiteral("Plain"));
	QCOMPARE(int(dialog.canvases().canvases[1].courses.size()), 1);
	QCOMPARE(dialog.canvases().canvases[1].courses[0].part, 0);

	extent->setCurrentIndex(1);
	scale->setValue(5000);
	box->setValue(5.0);
	rows->setValue(20);
	symbols->item(1)->setCheckState(Qt::Checked);
	active->setChecked(true);
	auto const& b = dialog.canvases().canvases[1];
	QCOMPARE(b.extent, QStringLiteral("Sheet"));
	QCOMPARE(b.scale, 5000u);
	QCOMPARE(b.box_size, 5.0);
	QCOMPARE(b.max_rows, 20);
	QCOMPARE(b.hidden_symbols, QStringList{ QStringLiteral("704.000") });
	QCOMPARE(dialog.canvases().used_symbols, QStringList{ QStringLiteral("704.000") });
	QVERIFY(dialog.canvases().active);
	// The first canvas has its own values.
	dialog.setCurrentCanvas(0);
	QCOMPARE(scale->value(), 0);
	QCOMPARE(extent->currentIndex(), 0);
	QCOMPARE(symbols->item(1)->checkState(), Qt::Unchecked);
	QCOMPARE(courses->item(1)->checkState(), Qt::Checked);
	QCOMPARE(dialog.canvases().canvases[1].scale, 5000u);

	// (3)
	QVERIFY(dialog.renameCanvas(1, QStringLiteral("C")));
	QVERIFY(!dialog.renameCanvas(1, QStringLiteral("A")));
	QVERIFY(!dialog.renameCanvas(5, QStringLiteral("D")));
	dialog.removeCanvas(0);
	QCOMPARE(int(dialog.canvases().canvases.size()), 1);
	QCOMPARE(dialog.canvases().canvases[0].name, QStringLiteral("C"));
	QCOMPARE(list->count(), 1);

	// A map which has canvases.
	data.canvases = dialog.canvases();
	map.setCourseData(data);
	CourseCanvasesDialog second(&map);
	QCOMPARE(second.findChild<QListWidget*>(QStringLiteral("canvases-list"))->count(), 1);
	QCOMPARE(second.currentCanvas(), 0);
	QCOMPARE(second.findChild<QSpinBox*>(QStringLiteral("canvas-scale"))->value(), 5000);
	QCOMPARE(second.findChild<QComboBox*>(QStringLiteral("canvas-extent"))->currentText(), QStringLiteral("Sheet"));
	QVERIFY(second.findChild<QCheckBox*>(QStringLiteral("canvases-active"))->isChecked());
	QVERIFY(second.canvases() == data.canvases);

	CourseMapsDialog maps(&map, QStringLiteral("C:/out"));
	auto* by_canvases = maps.findChild<QCheckBox*>(QStringLiteral("course-maps-canvases"));
	QVERIFY(by_canvases);
	QVERIFY(by_canvases->isEnabled());
	QVERIFY(by_canvases->isChecked());
	QVERIFY(maps.options().use_canvases);
	QVERIFY(!maps.findChild<QComboBox*>(QStringLiteral("course-maps-extent"))->isEnabled());
	by_canvases->setChecked(false);
	QVERIFY(!maps.options().use_canvases);
}


/// Load from XML and Save as XML, Accept (7)
void CourseCanvasTest::xmlScriptTest()
{
	// A canvas XML file: the rectangle of the extent is in it.
	CourseCanvases read;
	std::vector<Extent> extents;
	QVERIFY(canvasesFromXmlScript(ocdText().toUtf8(), read, extents));
	QCOMPARE(read, sampleCanvases());
	QCOMPARE(int(extents.size()), 1);
	auto const whole = wholeExtent();
	QCOMPARE(extents[0].name, whole.name);
	QVERIFY(qAbs(extents[0].rect_mm.left() - whole.rect_mm.left()) < 0.01);
	QVERIFY(qAbs(extents[0].rect_mm.top() - whole.rect_mm.top()) < 0.01);
	QVERIFY(qAbs(extents[0].rect_mm.width() - whole.rect_mm.width()) < 0.01);
	QVERIFY(qAbs(extents[0].rect_mm.height() - whole.rect_mm.height()) < 0.01);

	// Written as Save as XML writes it: in Windows-1252, with the rectangle behind the name of the extent.
	auto const bytes = canvasesToXmlScript(sampleCanvases(), { whole });
	// UTF-8 with a BOM and the encoding declared, as the format has it (a byte 0xFC makes the file unreadable).
	QVERIFY(bytes.startsWith("\xef\xbb\xbf<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<OcadScript>\r\n<CsCanvas>\r\n"));
	QVERIFY(bytes.contains("Forest Orienteering Map B\xc3\xbcrenflue.ocd"));
	QVERIFY(!bytes.contains("B\xfcrenflue"));
	QVERIFY(bytes.contains("<ExportRectangleName pageStatus=\"psSingle\">Whole</ExportRectangleName>\r\n"
	                       "<ExportRectangleFrontside>\r\n<L>-142.03</L>\r\n<B>-291.03</B>\r\n<R>255.37</R>\r\n<T>77.26</T>\r\n</ExportRectangleFrontside>\r\n"));
	QVERIFY(bytes.endsWith("</CsCanvas>\r\n</OcadScript>\r\n"));
	read = {};
	extents.clear();
	QVERIFY(canvasesFromXmlScript(bytes, read, extents));
	QCOMPARE(read, sampleCanvases());
	QCOMPARE(int(extents.size()), 1);

	// A canvas whose extent is not given has no rectangle; the record 1093 has none either.
	auto const without = canvasesToXmlScript(sampleCanvases(), {});
	QVERIFY(!without.contains("ExportRectangleFrontside"));
	QVERIFY(!canvasesToOcdXml(sampleCanvases()).contains(QLatin1String("ExportRectangleFrontside")));
	extents.clear();
	QVERIFY(canvasesFromXmlScript(without, read, extents));
	QVERIFY(extents.empty());
	QVERIFY(!canvasesFromXmlScript("not xml", read, extents));

	// In the dialog.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	Map map;
	map.setScaleDenominator(10000);
	Extent sheet;
	sheet.name = QStringLiteral("Sheet");
	sheet.rect_mm = QRectF(0.0, 0.0, 100.0, 80.0);
	map.setExtents({ sheet });
	auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("canvases.xml"));
	{
		CourseCanvasesDialog dialog(&map);
		QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("canvases-load-xml")));
		QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("canvases-save-xml")));
		QVERIFY(dialog.addCanvas(QStringLiteral("A")));
		auto* extent = dialog.findChild<QComboBox*>(QStringLiteral("canvas-extent"));
		QVERIFY(extent);
		extent->setCurrentIndex(extent->findText(QStringLiteral("Sheet")));
		QVERIFY(dialog.saveXml(path));
	}
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto const saved = file.readAll();
	file.close();
	QVERIFY(saved.contains("<Name>A</Name>"));
	QVERIFY(saved.contains("<L>0.00</L>\r\n<B>-80.00</B>\r\n<R>100.00</R>\r\n<T>0.00</T>"));

	// Loaded into a dialog of a map which has not the extent: the canvases are replaced, the extent comes with them.
	Map other;
	other.setScaleDenominator(10000);
	CourseCanvasesDialog loading(&other);
	QVERIFY(loading.addCanvas(QStringLiteral("Old")));
	QVERIFY(loading.loadXml(path));
	QCOMPARE(int(loading.canvases().canvases.size()), 1);
	QCOMPARE(loading.canvases().canvases[0].name, QStringLiteral("A"));
	QCOMPARE(loading.canvases().canvases[0].extent, QStringLiteral("Sheet"));
	QCOMPARE(loading.currentCanvas(), 0);
	QCOMPARE(int(loading.loadedExtents().size()), 1);
	QCOMPARE(loading.loadedExtents()[0].name, QStringLiteral("Sheet"));
	QCOMPARE(loading.loadedExtents()[0].rect_mm, sheet.rect_mm);
	auto* loaded_extent = loading.findChild<QComboBox*>(QStringLiteral("canvas-extent"));
	QCOMPARE(loaded_extent->currentText(), QStringLiteral("Sheet"));
	// A file which is none leaves the canvases as they are.
	auto const bad = QDir(dir.path()).absoluteFilePath(QStringLiteral("bad.xml"));
	QFile bad_file(bad);
	QVERIFY(bad_file.open(QIODevice::WriteOnly));
	bad_file.write("<OcadScript><File.Open/></OcadScript>");
	bad_file.close();
	QVERIFY(!loading.loadXml(bad));
	QVERIFY(!loading.loadXml(QDir(dir.path()).absoluteFilePath(QStringLiteral("none.xml"))));
	QCOMPARE(loading.canvases().canvases[0].name, QStringLiteral("A"));
}

/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


/// the design notes, "Canvases: defining an extent in the dialog", Accept

/**
 * A canvas file with an export scale of 0 is of no use. The format has no value for the
 * scale of the map or for the box and the rows of the course options, and
 * FOOM writes theirs in place of 0.
 * the design notes, "Canvases: the XML files".
 */
void CourseCanvasTest::ocdDefaultsTest()
{
	CourseCanvases canvases;
	CourseCanvas as_map;
	as_map.name = QStringLiteral("As the map");
	CourseCanvas own;
	own.name = QStringLiteral("Own");
	own.scale = 7500;
	own.box_size = 5.0;
	own.max_rows = 20;
	canvases.canvases = { as_map, own };

	auto const defaults = CanvasDefaults { 10000, 6.0, 25 };
	auto const xml = canvasesToOcdXml(canvases, defaults);
	QVERIFY(xml.contains(QLatin1String("<Name>As the map</Name>\r\n<ExportRectangleName pageStatus=\"psSingle\"></ExportRectangleName>\r\n"
	                                   "<ExportScale>10000</ExportScale>\r\n<CdSymbolsSize>6.0</CdSymbolsSize>\r\n<CdMaxLength>25</CdMaxLength>\r\n")));
	QVERIFY(xml.contains(QLatin1String("<ExportScale>7500</ExportScale>\r\n<CdSymbolsSize>5.0</CdSymbolsSize>\r\n<CdMaxLength>20</CdMaxLength>\r\n")));
	QVERIFY(!xml.contains(QLatin1String("<ExportScale>0</ExportScale>")));
	auto const script = canvasesToXmlScript(canvases, {}, defaults);
	QVERIFY(script.contains("<ExportScale>10000</ExportScale>"));

	// Course options without rows ('i' absent) leave the rows at 0.
	auto const no_rows = canvasesToOcdXml(canvases, CanvasDefaults { 10000, 6.0, 0 });
	QVERIFY(no_rows.contains(QLatin1String("<CdSymbolsSize>6.0</CdSymbolsSize>\r\n<CdMaxLength>0</CdMaxLength>")));

	// From a map: its scale and its course options.
	Map map;
	map.setScaleDenominator(15000);
	auto data = map.courseData();
	data.options.setValue(QLatin1Char('s'), QStringLiteral("5"));
	data.options.setValue(QLatin1Char('i'), QStringLiteral("30"));
	map.setCourseData(data);
	auto const from_map = canvasDefaults(map);
	QCOMPARE(from_map.scale, 15000u);
	QCOMPARE(from_map.box_size, 5.0);
	QCOMPARE(from_map.max_rows, 30);
}

void CourseCanvasTest::defineExtentTest()
{
	Map map;
	map.setScaleDenominator(10000);
	Extent sheet;
	sheet.name = QStringLiteral("Sheet");
	sheet.rect_mm = QRectF(0.0, 0.0, 100.0, 80.0);
	map.setExtents({ sheet });

	// (1) Without a definer the button stays disabled.
	{
		CourseCanvasesDialog dialog(&map);
		auto* define = dialog.findChild<QPushButton*>(QStringLiteral("canvas-define-extent"));
		QVERIFY(define);
		QVERIFY(dialog.addCanvas(QStringLiteral("A")));
		QVERIFY(!define->isEnabled());
	}

	CourseCanvasesDialog dialog(&map);
	auto* define = dialog.findChild<QPushButton*>(QStringLiteral("canvas-define-extent"));
	auto* extent_box = dialog.findChild<QComboBox*>(QStringLiteral("canvas-extent"));
	QVERIFY(define && extent_box);
	Extent handed;
	int calls = 0;
	std::function<bool (Extent&)> answer;
	dialog.setExtentDefiner([&](Extent& extent) {
		++calls;
		handed = extent;
		return answer(extent);
	});
	QVERIFY(!define->isEnabled());
	QVERIFY(dialog.addCanvas(QStringLiteral("A")));
	QVERIFY(define->isEnabled());

	// (5) A canvas without an extent is handed a new name.
	// (2) Front is defined.
	answer = [](Extent& extent) {
		extent.name = QStringLiteral("Front");
		extent.rect_mm = QRectF(10.0, 20.0, 100.0, 80.0);
		return true;
	};
	define->click();
	QCOMPARE(calls, 1);
	QVERIFY(!handed.name.isEmpty());
	QVERIFY(handed.name != QStringLiteral("Sheet"));
	QCOMPARE(extent_box->currentText(), QStringLiteral("Front"));
	QCOMPARE(dialog.canvases().canvases.front().extent, QStringLiteral("Front"));
	QCOMPARE(int(dialog.loadedExtents().size()), 1);
	QCOMPARE(dialog.loadedExtents().front().rect_mm, QRectF(10.0, 20.0, 100.0, 80.0));
	QCOMPARE(int(map.extents().size()), 1);

	// (3) Defined again, wider.
	answer = [](Extent& extent) {
		extent.rect_mm.setWidth(120.0);
		return true;
	};
	define->click();
	QCOMPARE(handed.name, QStringLiteral("Front"));
	QCOMPARE(handed.rect_mm, QRectF(10.0, 20.0, 100.0, 80.0));
	QCOMPARE(int(dialog.loadedExtents().size()), 1);
	QCOMPARE(dialog.loadedExtents().front().rect_mm.width(), 120.0);
	QCOMPARE(extent_box->findText(QStringLiteral("Front")), extent_box->findText(QStringLiteral("Front"), Qt::MatchExactly));

	// (4) Cancelled, or without a name: nothing changes.
	answer = [](Extent& extent) {
		extent.name = QStringLiteral("Other");
		return false;
	};
	define->click();
	answer = [](Extent& extent) {
		extent.name.clear();
		return true;
	};
	define->click();
	QCOMPARE(dialog.canvases().canvases.front().extent, QStringLiteral("Front"));
	QCOMPARE(int(dialog.loadedExtents().size()), 1);
	QCOMPARE(extent_box->findText(QStringLiteral("Other")), -1);

	// (5) A canvas on Sheet is handed the map's rectangle.
	QVERIFY(dialog.addCanvas(QStringLiteral("B")));
	extent_box->setCurrentIndex(extent_box->findText(QStringLiteral("Sheet")));
	QCOMPARE(dialog.canvases().canvases.back().extent, QStringLiteral("Sheet"));
	answer = [](Extent&) { return false; };
	define->click();
	QCOMPARE(handed.name, QStringLiteral("Sheet"));
	QCOMPARE(handed.rect_mm, QRectF(0.0, 0.0, 100.0, 80.0));
}


QTEST_MAIN(CourseCanvasTest)
#include "course_canvas_t.moc"  // IWYU pragma: keep
