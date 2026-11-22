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

// the design notes, "PDF import"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMarginsF>
#include <QObject>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QPrinter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRadioButton>
#include <QRectF>
#include <QSizeF>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "test_config.h"

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "pdf/pdf_import.h"
#include "pdf/pdf_import_dialog.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// Device units per mm of the test PDFs.
constexpr double units = 100.0;

/// The centre of an A4 page in mm as Qt's PDF writer makes it: it rounds the page size to whole points
/// (595 x 842 pt).
const QPointF a4_centre { 595 * 25.4 / 72 / 2, 842 * 25.4 / 72 / 2 };

QPointF mm(double x, double y) { return { x * units, y * units }; }

QPen pen(const QColor& color, double width_mm)
{
	QPen p(color, width_mm * units);
	p.setCapStyle(Qt::FlatCap);
	p.setJoinStyle(Qt::MiterJoin);
	return p;
}

/// Writes a PDF with one page for each painting function, of the given sizes in mm.
bool writePdf(const QString& path, const std::vector<QSizeF>& sizes, const std::vector<std::function<void(QPainter&)>>& pages)
{
	QPdfWriter writer(path);
	writer.setResolution(int(units * 25.4));
	writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
	writer.setPageSize(QPageSize(sizes.front(), QPageSize::Millimeter));
	QPainter painter;
	if (!painter.begin(&writer))
		return false;
	for (std::size_t i = 0; i < pages.size(); ++i)
	{
		if (i > 0)
		{
			writer.setPageSize(QPageSize(sizes[i], QPageSize::Millimeter));
			writer.newPage();
		}
		painter.save();
		pages[i](painter);
		painter.restore();
	}
	return painter.end();
}

void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
}

std::vector<const Object*> objectsOfType(const Map& map, Object::Type type)
{
	std::vector<const Object*> result;
	map.applyOnAllObjects([&](const Object* object) {
		if (object->getType() == type)
			result.push_back(object);
	});
	return result;
}

const PathObject* lineWithSymbol(const Map& map, const std::function<bool(const LineSymbol*)>& predicate)
{
	const PathObject* found = nullptr;
	map.applyOnAllObjects([&](const Object* object) {
		if (!found && object->getSymbol() && object->getSymbol()->getType() == Symbol::Line
		    && predicate(object->getSymbol()->asLine()))
			found = object->asPath();
	});
	return found;
}

QString hexOf(const MapColor* color)
{
	if (!color)
		return {};
	auto const& rgb = color->getRgb();
	return QColor::fromRgbF(rgb.r, rgb.g, rgb.b).name();
}

bool near(const MapCoordF& a, const QPointF& b, double tolerance)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

}  // namespace


class PdfImportTest : public QObject
{
Q_OBJECT

	QTemporaryDir dir;

	QString path(const char* name) const { return dir.filePath(QString::fromLatin1(name)); }

	/// Page 1: the shapes of Accept (2); page 2: a single line.
	QString shapesPdf() const
	{
		auto const file = path("shapes.pdf");
		if (QFileInfo::exists(file))
			return file;
		writePdf(file, { QSizeF(210, 297), QSizeF(297, 210) }, {
		    [](QPainter& p) {
			    p.setBrush(Qt::NoBrush);
			    p.setPen(pen(Qt::red, 0.5));
			    p.drawPolyline(QPolygonF(QVector<QPointF>{ mm(20, 30), mm(120, 30), mm(120, 80) }));

			    QPainterPath holed;
			    holed.setFillRule(Qt::OddEvenFill);
			    holed.addRect(QRectF(mm(20, 100), mm(80, 140)));
			    holed.addRect(QRectF(mm(40, 110), mm(60, 130)));
			    p.fillPath(holed, QColor(Qt::blue));

			    p.setPen(pen(Qt::green, 0.3));
			    p.drawEllipse(QRectF(mm(100, 100), mm(160, 140)));

			    auto dashed = pen(Qt::black, 0.5);
			    dashed.setCapStyle(Qt::RoundCap);
			    dashed.setDashPattern({ 4.0, 2.0 });  // in pen widths: 2 mm and 1 mm
			    p.setPen(dashed);
			    p.drawLine(mm(20, 200), mm(180, 200));
		    },
		    [](QPainter& p) {
			    p.setPen(pen(Qt::black, 0.25));
			    p.drawLine(mm(10, 10), mm(50, 10));
		    },
		});
		return file;
	}


private slots:
	void initTestCase()
	{
		QVERIFY(dir.isValid());
		doStaticInitializations();
	}

	/// Accept (1)
	void pageSizesTest()
	{
		QString error;
		auto const sizes = pdfPageSizes(shapesPdf(), &error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(int(sizes.size()), 2);
		QVERIFY(std::abs(sizes[0].width() - 210) < 0.1);
		QVERIFY(std::abs(sizes[0].height() - 297) < 0.1);
		QVERIFY(std::abs(sizes[1].width() - 297) < 0.1);
		QVERIFY(std::abs(sizes[1].height() - 210) < 0.1);

		Map map;
		setupMap(map);
		PdfImportOptions options;
		options.page = 0;
		QVERIFY(!readPdfPage(shapesPdf(), map, options).ok);
		options.page = 3;
		auto const third = readPdfPage(shapesPdf(), map, options);
		QVERIFY(!third.ok);
		QVERIFY(!third.error.isEmpty());
		QCOMPARE(map.getNumObjects(), 0);

		auto const not_pdf = path("not.pdf");
		QFile file(not_pdf);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("This is no PDF.\n");
		file.close();
		error.clear();
		QVERIFY(pdfPageSizes(not_pdf, &error).empty());
		QVERIFY(!error.isEmpty());
		auto const bad = readPdfPage(not_pdf, map, {});
		QVERIFY(!bad.ok);
		QVERIFY(!bad.error.isEmpty());
	}

	/// Accept (2)
	void shapesTest()
	{
		Map map;
		setupMap(map);
		auto const result = readPdfPage(shapesPdf(), map, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.areas, 1);
		QCOMPARE(result.lines, 3);
		QCOMPARE(result.texts, 0);
		QCOMPARE(map.getNumObjects(), 4);

		auto const* polyline = lineWithSymbol(map, [](const LineSymbol* s) { return hexOf(s->getColor()) == QLatin1String("#ff0000"); });
		QVERIFY(polyline);
		auto const* red = polyline->getSymbol()->asLine();
		QCOMPARE(red->getLineWidth(), 500);
		QCOMPARE(polyline->getRawCoordinateVector().size(), std::size_t(3));
		auto const& coords = polyline->getRawCoordinateVector();
		QVERIFY2(near(MapCoordF(coords[0]), QPointF(20, 30) - a4_centre, 0.01), qPrintable(QStringLiteral("%1 %2, page %3 x %4").arg(coords[0].x()).arg(coords[0].y()).arg(result.page_size_mm.width()).arg(result.page_size_mm.height())));
		QVERIFY(near(MapCoordF(coords[1]), QPointF(120, 30) - a4_centre, 0.01));
		QVERIFY(near(MapCoordF(coords[2]), QPointF(120, 80) - a4_centre, 0.01));

		auto const areas = objectsOfType(map, Object::Path);
		const PathObject* area = nullptr;
		for (auto const* object : areas)
			if (object->getSymbol()->getType() == Symbol::Area)
				area = object->asPath();
		QVERIFY(area);
		QCOMPARE(hexOf(area->getSymbol()->asArea()->getColor()), QStringLiteral("#0000ff"));
		QCOMPARE(int(area->parts().size()), 2);
		QVERIFY(area->parts()[0].isClosed());
		QVERIFY(area->parts()[1].isClosed());

		auto const* ellipse = lineWithSymbol(map, [](const LineSymbol* s) { return hexOf(s->getColor()) == QLatin1String("#00ff00"); });
		QVERIFY(ellipse);
		QVERIFY(ellipse->parts().front().isClosed());
		bool has_curve = false;
		for (auto const& coord : ellipse->getRawCoordinateVector())
			has_curve |= coord.isCurveStart();
		QVERIFY(has_curve);

		auto const* dashed = lineWithSymbol(map, [](const LineSymbol* s) { return s->isDashed(); });
		QVERIFY(dashed);
		auto const* dash_symbol = dashed->getSymbol()->asLine();
		QCOMPARE(dash_symbol->getDashLength(), 2000);
		QCOMPARE(dash_symbol->getBreakLength(), 1000);
		QCOMPARE(dash_symbol->getCapStyle(), LineSymbol::RoundCap);
		QCOMPARE(hexOf(dash_symbol->getColor()), QStringLiteral("#000000"));
	}

	/// Accept (3) and (4)
	void symbolsAndColoursTest()
	{
		auto const file = path("symbols.pdf");
		QVERIFY(writePdf(file, { QSizeF(210, 297) }, { [](QPainter& p) {
			p.setPen(pen(Qt::red, 0.5));
			p.drawLine(mm(10, 10), mm(50, 10));
			p.drawLine(mm(10, 20), mm(50, 20));
			p.setPen(pen(Qt::red, 1.0));
			p.drawLine(mm(10, 30), mm(50, 30));
			p.fillRect(QRectF(mm(10, 40), mm(50, 60)), QColor(Qt::yellow));
		} }));
		Map map;
		setupMap(map);
		auto const result = readPdfPage(file, map, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumObjects(), 4);
		QCOMPARE(map.getNumSymbols(), 3);
		QCOMPARE(map.getSymbol(0)->getNumberAsString(), QStringLiteral("1"));
		QCOMPARE(map.getSymbol(1)->getNumberAsString(), QStringLiteral("2"));
		QCOMPARE(map.getSymbol(2)->getNumberAsString(), QStringLiteral("3"));
		QCOMPARE(map.getSymbol(0)->getName(), QStringLiteral("PDF line 0.5 mm #ff0000"));
		QCOMPARE(map.getSymbol(1)->getName(), QStringLiteral("PDF line 1 mm #ff0000"));
		QCOMPARE(map.getSymbol(2)->getName(), QStringLiteral("PDF area #ffff00"));

		// Two colours; yellow, painted last, is on top (index 0).
		QCOMPARE(map.getNumColors(), 2);
		QCOMPARE(map.getColor(0)->getName(), QStringLiteral("#ffff00"));
		QCOMPARE(map.getColor(1)->getName(), QStringLiteral("#ff0000"));
	}

	/// Accept (5)
	void scaleCentrePageTest()
	{
		Map map;
		setupMap(map);
		PdfImportOptions options;
		options.pdf_scale = 15000;
		options.centre = { 100, -50 };
		auto const result = readPdfPage(shapesPdf(), map, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		auto const* polyline = lineWithSymbol(map, [](const LineSymbol* s) { return hexOf(s->getColor()) == QLatin1String("#ff0000"); });
		QVERIFY(polyline);
		auto const& coords = polyline->getRawCoordinateVector();
		auto expected = [](double x, double y) { return (QPointF(x, y) - a4_centre) * 1.5 + QPointF(100, -50); };
		QVERIFY(near(MapCoordF(coords[0]), expected(20, 30), 0.02));
		QVERIFY(near(MapCoordF(coords[2]), expected(120, 80), 0.02));
		QCOMPARE(polyline->getSymbol()->asLine()->getLineWidth(), 750);

		Map second;
		setupMap(second);
		PdfImportOptions page2;
		page2.page = 2;
		auto const result2 = readPdfPage(shapesPdf(), second, page2);
		QVERIFY2(result2.ok, qPrintable(result2.error));
		QCOMPARE(second.getNumObjects(), 1);
		QVERIFY(std::abs(result2.page_size_mm.width() - 297) < 0.1);
		auto const* line = lineWithSymbol(second, [](const LineSymbol*) { return true; });
		QVERIFY(line);
		QVERIFY(near(MapCoordF(line->getRawCoordinateVector().front()), QPointF(10, 10) - QPointF(842 * 25.4 / 72 / 2, 595 * 25.4 / 72 / 2), 0.01));
	}

	/// Accept (6)
	void textTest()
	{
		auto const file = path("text.pdf");
		QVERIFY(writePdf(file, { QSizeF(210, 297) }, { [](QPainter& p) {
			QFont font(QStringLiteral("Arial"));
			font.setPointSizeF(12);
			p.setFont(font);
			p.setPen(Qt::black);
			p.drawText(mm(30, 150), QStringLiteral("Hello map"));
			p.save();
			p.translate(mm(30, 200));
			p.rotate(-30);
			p.drawText(QPointF(0, 0), QStringLiteral("Turned"));
			p.restore();
		} }));

		Map map;
		setupMap(map);
		auto const result = readPdfPage(file, map, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.texts, 2);
		auto const texts = objectsOfType(map, Object::Text);
		QCOMPARE(int(texts.size()), 2);
		const TextObject* hello = nullptr;
		const TextObject* turned = nullptr;
		for (auto const* object : texts)
		{
			auto const* text = object->asText();
			if (text->getText() == QLatin1String("Hello map"))
				hello = text;
			else if (text->getText() == QLatin1String("Turned"))
				turned = text;
			else
				QFAIL(qPrintable(QStringLiteral("unexpected text \"%1\"").arg(text->getText())));
		}
		QVERIFY(hello);
		QVERIFY(turned);
		QVERIFY(near(hello->getAnchorCoordF(), QPointF(30, 150) - a4_centre, 0.5));
		QCOMPARE(hello->getHorizontalAlignment(), TextObject::AlignLeft);
		QCOMPARE(hello->getVerticalAlignment(), TextObject::AlignBaseline);
		auto const* symbol = hello->getSymbol()->asText();
		QVERIFY2(std::abs(symbol->getFontSize() - 12 * 25.4 / 72) < 0.05, qPrintable(QString::number(symbol->getFontSize())));
		QVERIFY(std::abs(hello->getRotation()) < 0.01);
		QVERIFY2(std::abs(qRadiansToDegrees(turned->getRotation()) - 30) < 0.5, qPrintable(QString::number(qRadiansToDegrees(turned->getRotation()))));

		Map no_text;
		setupMap(no_text);
		PdfImportOptions options;
		options.text = false;
		auto const without = readPdfPage(file, no_text, options);
		QVERIFY(without.ok);
		QCOMPARE(without.texts, 0);
		QCOMPARE(no_text.getNumObjects(), 0);
	}

	/// Accept (7)
	void imageTest()
	{
		QImage image(16, 16, QImage::Format_RGB32);
		image.fill(Qt::darkGreen);
		auto const file = path("image.pdf");
		QVERIFY(writePdf(file, { QSizeF(210, 297), QSizeF(210, 297) }, {
		    [&image](QPainter& p) {
			    p.drawImage(QRectF(mm(10, 10), mm(60, 60)), image);
			    p.setPen(pen(Qt::black, 0.25));
			    p.drawLine(mm(10, 100), mm(50, 100));
		    },
		    [&image](QPainter& p) {
			    p.drawImage(QRectF(mm(10, 10), mm(60, 60)), image);
		    },
		}));
		Map map;
		setupMap(map);
		auto const result = readPdfPage(file, map, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.images, 1);
		QCOMPARE(map.getNumObjects(), 1);
		QVERIFY(!result.warnings.isEmpty());

		Map only_image;
		setupMap(only_image);
		PdfImportOptions options;
		options.page = 2;
		auto const second = readPdfPage(file, only_image, options);
		QVERIFY(second.ok);
		QCOMPARE(second.images, 1);
		QCOMPARE(only_image.getNumObjects(), 0);
		QVERIFY(second.warnings.join(QLatin1Char(' ')).contains(QLatin1String("no vector")));
	}

	/// Accept (8), opacity
	void opacityTest()
	{
		auto const file = path("opacity.pdf");
		QVERIFY(writePdf(file, { QSizeF(210, 297) }, { [](QPainter& p) {
			p.setOpacity(0.5);
			p.fillRect(QRectF(mm(10, 10), mm(50, 50)), QColor(Qt::red));
		} }));
		Map map;
		setupMap(map);
		auto const result = readPdfPage(file, map, {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map.getNumColors(), 1);
		QVERIFY(std::abs(map.getColor(0)->getOpacity() - 0.5) < 0.01);
	}

	/// Accept (9), and (8) for CMYK
	void roundTripTest()
	{
		Map source;
		setupMap(source);
		auto* magenta_red = new MapColor(QStringLiteral("Red"), 0);
		magenta_red->setCmyk(MapColorCmyk(0, 1, 1, 0));
		magenta_red->setRgbFromCmyk();
		source.addColor(magenta_red, 0);
		auto* line_symbol = new LineSymbol();
		line_symbol->setColor(magenta_red);
		line_symbol->setLineWidth(1.0);
		line_symbol->setNumberComponent(0, 101);
		source.addSymbol(line_symbol, 0);
		auto* area_symbol = new AreaSymbol();
		area_symbol->setColor(magenta_red);
		area_symbol->setNumberComponent(0, 102);
		source.addSymbol(area_symbol, 1);
		auto* line = new PathObject(line_symbol, MapCoordVector{ MapCoord(0, 0), MapCoord(40, 10), MapCoord(40, 30) });
		source.addObject(line);
		auto* area = new PathObject(area_symbol, MapCoordVector{ MapCoord(50, 0), MapCoord(70, 0), MapCoord(70, 20), MapCoord(50, 0) });
		area->closeAllParts();
		source.addObject(area);

		auto const file = path("roundtrip.pdf");
		{
			MapPrinter printer(source, nullptr);
			printer.setTarget(MapPrinter::pdfTarget());
			printer.setMode(MapPrinterOptions::Vector);
			printer.setPrintArea(QRectF(-20, -20, 100, 80));
			printer.setCustomPageSize(printer.getPrintAreaPaperSize());
			auto qprinter = printer.makePrinter();
			QVERIFY(qprinter);
			qprinter->setOutputFormat(QPrinter::PdfFormat);
			qprinter->setOutputFileName(file);
			QVERIFY(printer.printMap(qprinter.get()));
		}

		Map map;
		setupMap(map);
		// The page is the print area's size, rounded by Qt to whole points, with the print area at its top left.
		auto const sizes = pdfPageSizes(file);
		QCOMPARE(int(sizes.size()), 1);
		PdfImportOptions options;
		options.centre = QPointF(-20, -20) + QPointF(sizes[0].width() / 2, sizes[0].height() / 2);
		auto const result = readPdfPage(file, map, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QVERIFY2(std::abs(result.page_size_mm.width() - 100) < 0.2, qPrintable(QStringLiteral("%1 x %2").arg(result.page_size_mm.width()).arg(result.page_size_mm.height())));
		QCOMPARE(result.lines, 1);
		QCOMPARE(result.areas, 1);
		auto const* back = lineWithSymbol(map, [](const LineSymbol*) { return true; });
		QVERIFY(back);
		auto const& coords = back->getRawCoordinateVector();
		QCOMPARE(int(coords.size()), 3);
		QVERIFY(near(MapCoordF(coords[0]), QPointF(0, 0), 0.05));
		QVERIFY(near(MapCoordF(coords[1]), QPointF(40, 10), 0.05));
		QVERIFY(near(MapCoordF(coords[2]), QPointF(40, 30), 0.05));
		QVERIFY(std::abs(back->getSymbol()->asLine()->getLineWidth() - 1000) <= 10);

		auto const& cmyk = back->getSymbol()->asLine()->getColor()->getCmyk();
		QVERIFY2(std::abs(cmyk.c) < 0.01 && std::abs(cmyk.m - 1) < 0.01 && std::abs(cmyk.y - 1) < 0.01 && std::abs(cmyk.k) < 0.01,
		         qPrintable(QStringLiteral("%1 %2 %3 %4").arg(cmyk.c).arg(cmyk.m).arg(cmyk.y).arg(cmyk.k)));
	}

	/// Accept (10)
	void importUndoTest()
	{
		Map map;
		setupMap(map);
		auto* color = new MapColor(QStringLiteral("Black"), 0);
		map.addColor(color, 0);
		auto* own = new LineSymbol();
		own->setColor(color);
		own->setNumberComponent(0, 101);
		map.addSymbol(own, 0);

		auto const result = importPdf(map, shapesPdf(), {});
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.objects, 4);
		QCOMPARE(map.getNumObjects(), 4);
		QVERIFY(map.getNumSymbols() > 1);
		QVERIFY(map.undoManager().canUndo());
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumObjects(), 0);
	}

	/// Accept (11)
	void formatTest()
	{
		auto const* format = FileFormats.findFormatForFilename(QStringLiteral("x.pdf"), &FileFormat::supportsFileImport);
		QVERIFY(format);
		QCOMPARE(QByteArray(format->id()), QByteArray("PDF"));
		QVERIFY(!format->supportsWriting());

		Map map;
		setupMap(map);
		auto importer = FileFormats.makeImporter(shapesPdf(), map, nullptr);
		QVERIFY(importer);
		QVERIFY(importer->doImport());
		QCOMPARE(map.getNumObjects(), 4);
	}

	/// Accept (11), the runner
	void runnerTest()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));

		Map map;
		setupMap(map);
		auto const map_path = path("target.omap");
		auto const* format = FileFormats.findFormat("XML");
		QVERIFY(format);
		QVERIFY(format->makeExporter(map_path, &map, nullptr)->doExport());

		auto const out = path("runner.foom");
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } },
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("import-pdf") }, { QStringLiteral("path"), shapesPdf() },
		                 { QStringLiteral("page"), 1 }, { QStringLiteral("scale"), 15000 },
		                 { QStringLiteral("centre"), QJsonArray{ 100, -50 } } },
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("save-as") }, { QStringLiteral("path"), out } },
		} } };
		auto const job_path = path("runner.json");
		QFile file(job_path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write(QJsonDocument(job).toJson());
		file.close();

		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(executable, { QStringLiteral("--run"), job_path });
		QVERIFY(process.waitForFinished(120000));
		QByteArray const output = process.readAllStandardOutput() + process.readAllStandardError();
		QVERIFY2(process.exitCode() == 0, output.constData());
		QVERIFY2(output.contains("\"lines\""), output.constData());

		Map result;
		QVERIFY(result.loadFrom(out));
		QCOMPARE(result.getNumObjects(), 4);
		auto const* polyline = lineWithSymbol(result, [](const LineSymbol* s) { return hexOf(s->getColor()) == QLatin1String("#ff0000"); });
		QVERIFY(polyline);
		auto const expected = (QPointF(20, 30) - a4_centre) * 1.5 + QPointF(100, -50);
		QVERIFY(near(MapCoordF(polyline->getRawCoordinateVector().front()), expected, 0.02));
	}

	/// Accept (12)
	void dialogTest()
	{
		Map map;
		setupMap(map);
		PdfImportDialog dialog(shapesPdf(), map, QPointF(7, 8));
		auto* page = dialog.findChild<QComboBox*>(QStringLiteral("page"));
		auto* scale = dialog.findChild<QSpinBox*>(QStringLiteral("scale"));
		auto* origin = dialog.findChild<QRadioButton*>(QStringLiteral("placeOrigin"));
		auto* view = dialog.findChild<QRadioButton*>(QStringLiteral("placeView"));
		auto* typed = dialog.findChild<QRadioButton*>(QStringLiteral("placeTyped"));
		auto* x = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("centreX"));
		auto* y = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("centreY"));
		auto* text = dialog.findChild<QCheckBox*>(QStringLiteral("text"));
		QVERIFY(page && scale && origin && view && typed && x && y && text);

		QCOMPARE(page->count(), 2);
		QVERIFY(page->itemText(0).contains(QLatin1String("210")));
		QVERIFY2(page->itemText(1).contains(QString::fromUtf8("297 × 210")), qPrintable(page->itemText(1)));
		QCOMPARE(page->currentIndex(), 0);
		QCOMPARE(scale->value(), 10000);
		QVERIFY(origin->isChecked());
		QVERIFY(text->isChecked());
		auto options = dialog.options();
		QCOMPARE(options.page, 1);
		QCOMPARE(options.pdf_scale, 10000);
		QCOMPARE(options.centre, QPointF(0, 0));
		QVERIFY(options.text);

		view->setChecked(true);
		QCOMPARE(dialog.options().centre, QPointF(7, 8));

		page->setCurrentIndex(1);
		scale->setValue(15000);
		typed->setChecked(true);
		x->setValue(12.5);
		y->setValue(-3);
		text->setChecked(false);
		options = dialog.options();
		QCOMPARE(options.page, 2);
		QCOMPARE(options.pdf_scale, 15000);
		QCOMPARE(options.centre, QPointF(12.5, -3));
		QVERIFY(!options.text);
	}
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
#ifdef Q_OS_WIN
	// The offscreen platform knows no fonts on Windows unless it is told where they are (as foom-cli does).
	auto Q_DECL_UNUSED fonts_selected = qputenv("QT_QPA_FONTDIR", (qEnvironmentVariableIsEmpty("WINDIR") ? QByteArray("C:/Windows") : qgetenv("WINDIR")) + "/Fonts");  // clazy:exclude=non-pod-global-static
#endif
}


QTEST_MAIN(PdfImportTest)
#include "pdf_import_t.moc"  // IWYU pragma: keep
