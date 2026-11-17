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


#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPageSize>
#include <QPrinter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/extent_printing.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setColor(black);
	line->setLineWidth(0.5);
	map.addSymbol(line, 0);
	map.addObject(new PathObject(line, MapCoordVector{ MapCoord(20, 20), MapCoord(180, 250) }));
	map.addObject(new PathObject(line, MapCoordVector{ MapCoord(20, 420), MapCoord(180, 650) }));
}

Extent a4Sheet()
{
	Extent extent;
	extent.name = QStringLiteral("sheet");
	extent.rect_mm = QRectF(0, 0, 210, 297);
	extent.paper = QStringLiteral("A4");
	extent.margin_mm = 10;
	return extent;
}

Extent backOfSheet()
{
	Extent extent;
	extent.name = QStringLiteral("back");
	extent.rect_mm = QRectF(0, 400, 210, 297);
	extent.back_of = QStringLiteral("sheet");
	return extent;
}

QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

bool exportPdf(MapPrinter& printer, const QString& path)
{
	auto qprinter = printer.makePrinter();
	if (!qprinter)
		return false;
	qprinter->setOutputFormat(QPrinter::PdfFormat);
	qprinter->setOutputFileName(path);
	return printer.printMap(qprinter.get());
}

/// The page sizes of a PDF written by Qt, in points, rounded.
std::vector<QSize> pdfPages(const QByteArray& pdf)
{
	std::vector<QSize> pages;
	QRegularExpression box(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]"));
	// The file has zero bytes, so the size is given.
	auto matches = box.globalMatch(QString::fromLatin1(pdf.constData(), pdf.size()));
	while (matches.hasNext())
	{
		auto const match = matches.next();
		pages.emplace_back(qRound(match.captured(1).toDouble()), qRound(match.captured(2).toDouble()));
	}
	return pages;
}

QByteArray save(const Map& map)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool load(const QByteArray& data, Map& map)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.setData(data);
	buffer.open(QIODevice::ReadOnly);
	auto importer = format.makeImporter({}, &map, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

}  // namespace



/**
 * Tests extents in print and export.
 *
 * See the design notes, "Extents in print and export".
 */
class ExtentPrintTest : public QObject
{
Q_OBJECT
	
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
	}
	
	/// Accept (1)
	void paperAndMarginTest()
	{
		Map map;
		setupMap(map);
		MapPrinter printer(map, nullptr);
		printer.setTarget(MapPrinter::pdfTarget());
		QCOMPARE(applyExtentToPrinter(printer, map, a4Sheet()), 1);
		QVERIFY(printer.getPageFormat().page_size == QPageSize::A4);
		QVERIFY(printer.getPageFormat().orientation == MapPrinterPageFormat::Portrait);
		QCOMPARE(printer.getPrintArea(), QRectF(10, 10, 190, 277));
		QCOMPARE(printer.horizontalPagePositions().size(), std::size_t(1));
		QCOMPARE(printer.verticalPagePositions().size(), std::size_t(1));
		QVERIFY(printer.backSides().empty());
		
		auto wide = a4Sheet();
		wide.rect_mm = QRectF(0, 0, 297, 210);
		QCOMPARE(applyExtentToPrinter(printer, map, wide), 1);
		QVERIFY(printer.getPageFormat().page_size == QPageSize::A4);
		QVERIFY(printer.getPageFormat().orientation == MapPrinterPageFormat::Landscape);
		QCOMPARE(printer.getPageFormat().paper_dimensions, QSizeF(297, 210));
		QCOMPARE(printer.getPrintArea(), QRectF(10, 10, 277, 190));
	}
	
	/// Accept (2)
	void customPageTest()
	{
		Map map;
		setupMap(map);
		MapPrinter printer(map, nullptr);
		printer.setTarget(MapPrinter::pdfTarget());
		Extent extent;
		extent.name = QStringLiteral("detail");
		extent.rect_mm = QRectF(0, 0, 100, 80);
		extent.scale = 5000;
		QCOMPARE(applyExtentToPrinter(printer, map, extent), 1);
		QCOMPARE(printer.getOptions().scale, 5000u);
		QVERIFY(printer.getPageFormat().page_size == QPageSize::Custom);
		QCOMPARE(printer.getPageFormat().paper_dimensions, QSizeF(200, 160));
		QCOMPARE(printer.getPrintArea(), QRectF(0, 0, 100, 80));
	}
	
	/// Accept (3), (4)
	void twoSidesTest()
	{
		Map map;
		setupMap(map);
		map.setExtents({ a4Sheet(), backOfSheet() });
		MapPrinter printer(map, nullptr);
		printer.setTarget(MapPrinter::pdfTarget());
		QCOMPARE(applyExtentToPrinter(printer, map, map.extents().front()), 2);
		QCOMPARE(printer.backSides().size(), std::size_t(1));
		QCOMPARE(printer.backSides().front().area, QRectF(0, 400, 210, 297));
		
		auto const two = file("two.pdf");
		QVERIFY(exportPdf(printer, two));
		auto pages = pdfPages(fileBytes(two));
		QCOMPARE(pages.size(), std::size_t(2));
		QCOMPARE(pages[0], QSize(595, 842));
		QCOMPARE(pages[1], QSize(595, 842));
		QCOMPARE(printer.getPrintArea(), QRectF(10, 10, 190, 277));
		QCOMPARE(printer.getOptions().scale, 10000u);
		QCOMPARE(printer.backSides().size(), std::size_t(1));
		
		printer.setPrintArea(QRectF(0, 0, 100, 100));
		QVERIFY(printer.backSides().empty());
		auto const one = file("one.pdf");
		QVERIFY(exportPdf(printer, one));
		pages = pdfPages(fileBytes(one));
		QCOMPARE(pages.size(), std::size_t(1));
	}
	
	/// Accept (5)
	void defaultExtentTest()
	{
		Map map;
		setupMap(map);
		MapPrinterConfig config(map);
		config.print_area = QRectF(5, 6, 100, 80);
		config.options.scale = 15000;
		config.page_format.page_size = QPageSize::A4;
		config.page_format.orientation = MapPrinterPageFormat::Landscape;
		config.page_format.paper_dimensions = QSizeF(297, 210);
		map.setPrinterConfig(config);
		
		{
			Map loaded;
			QVERIFY(load(save(map), loaded));
			QCOMPARE(loaded.extents().size(), std::size_t(1));
			auto const& extent = loaded.extents().front();
			QCOMPARE(extent.name, QStringLiteral("default"));
			QCOMPARE(extent.rect_mm, QRectF(5, 6, 100, 80));
			QCOMPARE(extent.scale, 15000u);
			QCOMPARE(extent.paper, QStringLiteral("A4"));
			QCOMPARE(extent.margin_mm, 0.0);
			QVERIFY(extent.back_of.isEmpty());
		}
		{
			// Adding the extent leaves the change flag alone, and happens once.
			map.setHasUnsavedChanges(false);
			QVERIFY(addDefaultExtent(map));
			QVERIFY(!map.hasUnsavedChanges());
			QVERIFY(!addDefaultExtent(map));
			QCOMPARE(map.extents().size(), std::size_t(1));
		}
		{
			map.setExtents({ a4Sheet() });
			Map loaded;
			QVERIFY(load(save(map), loaded));
			QCOMPARE(loaded.extents().size(), std::size_t(1));
			QCOMPARE(loaded.extents().front().name, QStringLiteral("sheet"));
		}
		{
			Map plain;
			setupMap(plain);
			Map loaded;
			QVERIFY(load(save(plain), loaded));
			QVERIFY(loaded.extents().empty());
		}
	}
	
	/// Accept (6)
	void importTest()
	{
		auto const named = [](const char* name, double x) {
			Extent extent;
			extent.name = QString::fromLatin1(name);
			extent.rect_mm = QRectF(x, 0, 50, 40);
			return extent;
		};
		Map map;
		setupMap(map);
		map.setExtents({ named("a", 1), named("c", 3) });
		auto const steps = map.undoManager().undoStepCount();
		
		QCOMPARE(importExtents(map, { named("a", 10), named("b", 20) }), 2);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(map.extents().size(), std::size_t(3));
		QCOMPARE(map.extents()[0].name, QStringLiteral("a"));
		QCOMPARE(map.extents()[0].rect_mm.x(), 10.0);
		QCOMPARE(map.extents()[1].name, QStringLiteral("c"));
		QCOMPARE(map.extents()[2].name, QStringLiteral("b"));
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.extents().size(), std::size_t(2));
		QCOMPARE(map.extents()[0].rect_mm.x(), 1.0);
		QCOMPARE(map.extents()[1].name, QStringLiteral("c"));
		
		QCOMPARE(importExtents(map, {}), 0);
		QString error;
		QCOMPARE(importExtentsFromFile(map, file("missing.foom"), &error), -1);
		QVERIFY(!error.isEmpty());
	}
	
	/// Accept (7)
	void runnerTest()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));
		
		auto const map_path = file("sides.foom");
		{
			Map map;
			setupMap(map);
			map.setExtents({ a4Sheet(), backOfSheet() });
			XMLFileFormat format;
			auto exporter = format.makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter && exporter->doExport());
		}
		auto const pdf_path = file("sides.pdf");
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } },
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("export") }, { QStringLiteral("path"), pdf_path }, { QStringLiteral("extent"), QStringLiteral("sheet") } },
		} } };
		auto const job_path = file("sides.json");
		{
			QFile job_file(job_path);
			QVERIFY(job_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			job_file.write(QJsonDocument(job).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(executable, { QStringLiteral("--run"), job_path });
		QVERIFY(process.waitForFinished(120000));
		QByteArray output = process.readAllStandardOutput();
		auto const errors = process.readAllStandardError();
		QByteArray all = output;
		all += errors;
		QVERIFY2(process.exitCode() == 0, all.constData());
		QVERIFY2(QRegularExpression(QStringLiteral("\"pages\":\\s*2")).match(QString::fromUtf8(output)).hasMatch(), output.constData());
		
		auto const pages = pdfPages(fileBytes(pdf_path));
		QCOMPARE(pages.size(), std::size_t(2));
		QCOMPARE(pages[0], QSize(595, 842));
		QCOMPARE(pages[1], QSize(595, 842));
	}
};



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(ExtentPrintTest)
#include "extent_print_t.moc"  // IWYU pragma: keep
