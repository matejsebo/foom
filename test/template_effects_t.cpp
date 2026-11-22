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

#include <cstdlib>
#include <memory>

#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/map_view.h"
#include "core/renderables/renderable.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "templates/template_map.h"
#ifdef MAPPER_TEST_GDAL
#  include <cpl_string.h>
#  include <gdal.h>
#  include "gdal/gdal_image_reader.h"
#  include "gdal/gdal_template.h"
#endif

using namespace OpenOrienteering;


namespace {

const QRectF area(0, 0, 20, 10);
constexpr int dpi = 254;  // 10 pixels per mm

bool saveMap(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

bool loadMap(Map& map, const QString& path)
{
	XMLFileFormat format;
	auto importer = format.makeImporter(path, &map, nullptr);
	return importer && importer->doImport();
}

/** The map of the acceptance criteria: a black square from (2, 2) to (6, 6) mm. */
void fillMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* symbol = new AreaSymbol();
	symbol->setColor(black);
	map.addSymbol(symbol, 0);
	MapCoordVector coords { MapCoord(2.0, 2.0), MapCoord(6.0, 2.0), MapCoord(6.0, 6.0), MapCoord(2.0, 6.0), MapCoord(2.0, 2.0) };
	coords.back().setClosePoint(true);
	map.addObject(new PathObject(symbol, coords));
}

/** Draws the area as the printer does: white, the templates below the map, the map, the templates above. */
QImage printed(Map& map, const MapView* view = nullptr)
{
	MapPrinter printer(map, view);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setResolution(dpi);
	printer.setPrintArea(area);
	printer.setPrintTemplates(true);
	QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image, RenderConfig::NoOptions);
	}
	return image;
}

/** Draws the area as the map widget does, in two passes of the templates. */
QImage onScreen(Map& map)
{
	QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	QPainter painter(&image);
	painter.scale(10.0, 10.0);
	map.drawTemplates(&painter, area, 0, map.getFirstFrontTemplate() - 1, nullptr, true);
	RenderConfig config = { map, area, 10.0, RenderConfig::Screen, 1.0 };
	map.draw(&painter, config);
	map.drawTemplates(&painter, area, map.getFirstFrontTemplate(), map.getNumTemplates() - 1, nullptr, true);
	return image;
}

QColor square(const QImage& image)
{
	return image.pixelColor(40, 40);
}

QColor open(const QImage& image)
{
	return image.pixelColor(100, 50);
}

bool near(const QColor& color, int r, int g, int b, int tolerance = 2)
{
	return std::abs(color.red() - r) <= tolerance && std::abs(color.green() - g) <= tolerance && std::abs(color.blue() - b) <= tolerance;
}

QByteArray describe(const QColor& color)
{
	return QByteArray::number(color.red()) + ' ' + QByteArray::number(color.green()) + ' ' + QByteArray::number(color.blue());
}

#ifdef MAPPER_TEST_GDAL
/** Writes a GeoTIFF of 4 by 4 pixels, one value per band. */
bool writeTiff(const QString& path, const std::vector<int>& values, const char* photometric, const char* alpha, GDALColorInterp fourth)
{
	GDALAllRegister();
	auto* driver = GDALGetDriverByName("GTiff");
	if (!driver)
		return false;
	char** options = nullptr;
	options = CSLSetNameValue(options, "PHOTOMETRIC", photometric);
	if (alpha)
		options = CSLSetNameValue(options, "ALPHA", alpha);
	auto dataset = GDALCreate(driver, path.toUtf8().constData(), 4, 4, int(values.size()), GDT_Byte, options);
	CSLDestroy(options);
	if (!dataset)
		return false;
	for (int b = 0; b < int(values.size()); ++b)
	{
		auto band = GDALGetRasterBand(dataset, b + 1);
		if (GDALFillRaster(band, values[std::size_t(b)], 0) != CE_None)
			return false;
	}
	if (values.size() >= 4 && fourth != GCI_Max)
		GDALSetRasterColorInterpretation(GDALGetRasterBand(dataset, 4), fourth);
	GDALClose(dataset);
	return true;
}
#endif

}  // namespace



/**
 * @test Tests block out, dim and colour infrared of templates.
 *
 * See the design notes, "Block out, dim and colour infrared".
 */
class TemplateEffectsTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;

	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}

	/** Writes a uniform image of the colour and adds it as a template at pos, below the map unless above. */
	TemplateImage* addTemplate(Map& map, const QColor& color, int pos = 0, bool above = false) const
	{
		auto const name = QString(QStringLiteral("t%1.png")).arg(map.getNumTemplates());
		auto const path = QDir(dir.path()).absoluteFilePath(name);
		QImage image(200, 100, QImage::Format_RGB32);
		image.fill(color);
		if (!image.save(path, "PNG"))
			return nullptr;
		map.addTemplate(pos, std::make_unique<TemplateImage>(path, &map));
		if (!above)
			map.setFirstFrontTemplate(map.getFirstFrontTemplate() + 1);
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(pos));
		temp->setTemplateScaleX(1.0);
		temp->setTemplateScaleY(1.0);
		if (!temp->loadTemplateFile())
			return nullptr;
		temp->setTemplateState(Template::Loaded);
		return temp;
	}

private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
	}

	/// Accept (1)
	void flagTest()
	{
		Map map;
		fillMap(map);
		auto* temp = addTemplate(map, Qt::white);
		auto* other = addTemplate(map, Qt::white, 1);
		QVERIFY(temp && other);
		QVERIFY(!temp->isBlockOut());
		QCOMPARE(temp->dim(), 0.0);
		QVERIFY(!temp->isInfrared());

		map.setTemplatesDirty();
		map.setHasUnsavedChanges(false);
		temp->setBlockOut(true);
		QVERIFY(map.hasUnsavedChanges());
		map.setHasUnsavedChanges(false);
		temp->setDim(0.4);
		QVERIFY(map.hasUnsavedChanges());
		map.setHasUnsavedChanges(false);
		temp->setInfrared(true);
		QVERIFY(map.hasUnsavedChanges());
		QVERIFY(temp->isBlockOut());
		QCOMPARE(temp->dim(), 0.4);
		QVERIFY(temp->isInfrared());

		std::unique_ptr<Template> copy { temp->duplicate() };
		QVERIFY(copy->isBlockOut());
		QCOMPARE(copy->dim(), 0.4);
		QVERIFY(copy->isInfrared());

		auto const path = file("flags.xmap");
		QVERIFY(saveMap(map, path));
		QFile saved(path);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		auto const content = saved.readAll();
		QCOMPARE(content.count("block_out=\"true\""), 1);
		QCOMPARE(content.count("dim=\"40\""), 1);
		QCOMPARE(content.count("infrared=\"true\""), 1);

		Map reopened;
		QVERIFY(loadMap(reopened, path));
		QCOMPARE(reopened.getNumTemplates(), 2);
		QVERIFY(reopened.getTemplate(0)->isBlockOut());
		QCOMPARE(reopened.getTemplate(0)->dim(), 0.4);
		QVERIFY(reopened.getTemplate(0)->isInfrared());
		QVERIFY(!reopened.getTemplate(1)->isBlockOut());
		QCOMPARE(reopened.getTemplate(1)->dim(), 0.0);
		QVERIFY(!reopened.getTemplate(1)->isInfrared());

		temp->setDim(1.5);
		QCOMPARE(temp->dim(), 1.0);
		temp->setDim(-1);
		QCOMPARE(temp->dim(), 0.0);
	}

	/// Accept (2)
	void blockOutTest()
	{
		{
			Map map;
			fillMap(map);
			auto* temp = addTemplate(map, QColor(255, 0, 0));
			QVERIFY(temp);
			auto image = printed(map);
			QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
			QVERIFY2(near(open(image), 255, 0, 0), describe(open(image)));
			temp->setBlockOut(true);
			image = printed(map);
			QVERIFY2(near(square(image), 255, 0, 0), describe(square(image)));
			QVERIFY2(near(open(image), 255, 0, 0), describe(open(image)));
		}
		{
			Map map;
			fillMap(map);
			auto* temp = addTemplate(map, Qt::white);
			QVERIFY(temp);
			temp->setBlockOut(true);
			auto const image = printed(map);
			QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
			QVERIFY2(near(open(image), 255, 255, 255), describe(open(image)));
		}
		{
			Map map;
			fillMap(map);
			auto* temp = addTemplate(map, QColor(128, 128, 128));
			QVERIFY(temp);
			temp->setBlockOut(true);
			auto const image = printed(map);
			QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
			QVERIFY2(near(open(image), 128, 128, 128), describe(open(image)));
		}
	}

	/// Accept (3)
	void screenPassesTest()
	{
		Map map;
		fillMap(map);
		auto* temp = addTemplate(map, QColor(255, 0, 0));
		QVERIFY(temp);
		QCOMPARE(map.getFirstFrontTemplate(), 1);
		auto image = onScreen(map);
		QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
		temp->setBlockOut(true);
		image = onScreen(map);
		QVERIFY2(near(square(image), 255, 0, 0), describe(square(image)));
		QVERIFY2(near(open(image), 255, 0, 0), describe(open(image)));

		// A white template above the map covers the block-out one.
		auto* above = addTemplate(map, Qt::white, 1, true);
		QVERIFY(above);
		QCOMPARE(map.getFirstFrontTemplate(), 1);
		image = onScreen(map);
		QVERIFY2(near(open(image), 255, 255, 255), describe(open(image)));
		image = printed(map);
		QVERIFY2(near(open(image), 255, 255, 255), describe(open(image)));
	}

	/// Accept (4)
	void dimTest()
	{
		Map map;
		fillMap(map);
		auto* temp = addTemplate(map, QColor(100, 100, 100));
		QVERIFY(temp);
		auto image = printed(map);
		QVERIFY2(near(open(image), 100, 100, 100), describe(open(image)));
		temp->setDim(0.5);
		image = printed(map);
		QVERIFY2(near(open(image), 177, 177, 177), describe(open(image)));
		QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
		temp->setDim(1.0);
		image = printed(map);
		QVERIFY2(near(open(image), 255, 255, 255), describe(open(image)));

		temp->setDim(0.5);
		MapView view(&map);
		view.setTemplateVisibility(temp, { 0.5f, true });
		image = printed(map, &view);
		QVERIFY2(near(open(image), 216, 216, 216, 3), describe(open(image)));
	}

	/// Accept (5)
	void dimBlockOutTest()
	{
		Map map;
		fillMap(map);
		auto* temp = addTemplate(map, Qt::black);
		QVERIFY(temp);
		temp->setDim(0.5);
		temp->setBlockOut(true);
		auto const image = printed(map);
		QVERIFY2(near(open(image), 128, 128, 128), describe(open(image)));
		QVERIFY2(near(square(image), 0, 0, 0), describe(square(image)));
	}

	/// Accept (6)
	void infraredTest()
	{
#ifndef MAPPER_TEST_GDAL
		QSKIP("Needs GDAL");
#else
		auto const cir = file("cir.tif");
		auto const with_alpha = file("alpha.tif");
		auto const rgb = file("rgb.tif");
		auto const gray = file("gray.tif");
		QVERIFY(writeTiff(cir, { 10, 20, 30, 200 }, "RGB", "UNSPECIFIED", GCI_Undefined));
		QVERIFY(writeTiff(with_alpha, { 10, 20, 30, 200 }, "RGB", "YES", GCI_AlphaBand));
		QVERIFY(writeTiff(rgb, { 10, 20, 30 }, "RGB", nullptr, GCI_Max));
		QVERIFY(writeTiff(gray, { 10, 20, 30, 200 }, "MINISBLACK", nullptr, GCI_Max));

		QCOMPARE(GdalImageReader(cir).infraredBands(), (std::vector<int>{ 1, 2, 3, 4 }));
		QVERIFY(GdalImageReader(with_alpha).infraredBands().empty());
		QVERIFY(GdalImageReader(rgb).infraredBands().empty());
		QCOMPARE(GdalImageReader(gray).infraredBands(), (std::vector<int>{ 1, 2, 3, 4 }));

		Map map;
		map.addTemplate(-1, std::make_unique<GdalTemplate>(cir, &map));
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		QVERIFY(temp);
		QVERIFY(temp->loadTemplateFile());
		temp->setTemplateState(Template::Loaded);
		QCOMPARE(QColor(temp->getImage().pixel(1, 1)), QColor(10, 20, 30));
		temp->setInfrared(true);
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QCOMPARE(QColor(temp->getImage().pixel(1, 1)), QColor(200, 10, 20));

		{
			Map other;
			other.addTemplate(-1, std::make_unique<GdalTemplate>(cir, &other));
			auto* t = qobject_cast<TemplateImage*>(other.getTemplate(0));
			t->setInfrared(true);
			QVERIFY(t->loadTemplateFile());
			QCOMPARE(QColor(t->getImage().pixel(1, 1)), QColor(200, 10, 20));
		}
		{
			Map other;
			other.addTemplate(-1, std::make_unique<GdalTemplate>(gray, &other));
			auto* t = qobject_cast<TemplateImage*>(other.getTemplate(0));
			t->setInfrared(true);
			QVERIFY(t->loadTemplateFile());
			QCOMPARE(QColor(t->getImage().pixel(1, 1)), QColor(200, 10, 20));
		}
		{
			Map other;
			other.addTemplate(-1, std::make_unique<GdalTemplate>(rgb, &other));
			auto* t = qobject_cast<TemplateImage*>(other.getTemplate(0));
			QVERIFY(t->loadTemplateFile());
			QCOMPARE(QColor(t->getImage().pixel(1, 1)), QColor(10, 20, 30));
			t->setInfrared(true);
			QCOMPARE(QColor(t->getImage().pixel(1, 1)), QColor(10, 20, 30));
		}
#endif
	}
};


QTEST_MAIN(TemplateEffectsTest)
#include "template_effects_t.moc"  // IWYU pragma: keep
