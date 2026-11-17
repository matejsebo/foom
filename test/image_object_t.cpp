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


#include <array>
#include <cmath>
#include <cstddef>
#include <memory>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/image_object.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/image_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "undo/object_undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

/// Writes a plain image file. dots_per_metre sets the size in the map.
bool writeImage(const QString& path, int width, int height, const QColor& color, int dots_per_metre, const char* format)
{
	QImage image(width, height, QImage::Format_RGB32);
	image.fill(color);
	image.setDotsPerMeterX(dots_per_metre);
	image.setDotsPerMeterY(dots_per_metre);
	return image.save(path, format);
}

enum Shade { Red, Blue, LightRed };

/// Counts pixels of a shade in the 40 by 40 mm around the origin, at 5 pixels per mm.
int countPixels(Map& map, Shade shade)
{
	map.updateObjects();
	QRectF const extent(-20, -20, 40, 40);
	constexpr double pixel_per_mm = 5;
	QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.scale(pixel_per_mm, pixel_per_mm);
	painter.translate(-extent.topLeft());
	map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
	painter.end();
	auto count = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			auto const c = QColor(image.pixel(x, y));
			switch (shade)
			{
			case Red:
				count += c.red() > 200 && c.blue() < 60;
				break;
			case Blue:
				count += c.blue() > 200 && c.red() < 60;
				break;
			case LightRed:
				count += c.red() > 200 && c.blue() > 100 && c.blue() < 160;
				break;
			}
		}
	}
	return count;
}

QByteArray save(const Map& map, bool foom)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setOption(QStringLiteral("foom"), foom);
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

bool saveToFile(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

const ImageObject* imageAt(const Map& map, int part, int index)
{
	auto const* object = map.getPart(std::size_t(part))->getObject(index);
	return object->getType() == Object::Image ? static_cast<const ImageObject*>(object) : nullptr;
}

}  // namespace



/**
 * Tests image objects.
 *
 * See the design notes, "Image object".
 */
class ImageObjectTest : public QObject
{
Q_OBJECT
	
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/// 200 by 100 pixels at 200 dots per mm: 1 by 0.5 mm.
	QString smallPng() const { return file("small.png"); }
	/// 100 by 100 red pixels at 10 dots per mm: 10 by 10 mm.
	QString redPng() const { return file("red.png"); }
	QString photoJpeg() const { return file("photo.jpg"); }
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QVERIFY(writeImage(smallPng(), 200, 100, QColor(Qt::green), 200000, "PNG"));
		QVERIFY(writeImage(redPng(), 100, 100, QColor(255, 0, 0), 10000, "PNG"));
		QVERIFY(writeImage(photoJpeg(), 64, 48, QColor(40, 90, 200), 10000, "JPEG"));
	}
	
	/// Accept (1)
	void addTest()
	{
		Map map;
		QString error;
		auto* image = addImage(map, smallPng(), true, MapCoordF(10, 20), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		QCOMPARE(map.getNumObjects(), 1);
		QVERIFY(image->getType() == Object::Image);
		QCOMPARE(image->getSymbol(), static_cast<const Symbol*>(Map::getImageSymbol()));
		QCOMPARE(image->size(), QSizeF(1.0, 0.5));
		QCOMPARE(QPointF(image->anchor()), QPointF(10, 20));
		QCOMPARE(image->imageRotation(), 0.0);
		QVERIFY(!image->isLinked());
		QCOMPARE(image->data(), fileBytes(smallPng()));
		QCOMPARE(image->format(), QStringLiteral("png"));
		QCOMPARE(image->image().size(), QSize(200, 100));
		QCOMPARE(map.undoManager().undoStepCount(), 1);
		QVERIFY(map.isObjectSelected(image));
		
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumObjects(), 0);
		
		Map limited;
		auto* reduced = addImage(limited, smallPng(), true, MapCoordF(0, 0), QSizeF(0.5, 0.5), &error);
		QVERIFY2(reduced, qPrintable(error));
		QCOMPARE(reduced->size(), QSizeF(0.5, 0.25));
		
		auto const text_path = file("notes.txt");
		{
			QFile text(text_path);
			QVERIFY(text.open(QIODevice::WriteOnly));
			text.write("not an image");
		}
		error.clear();
		QVERIFY(!addImage(limited, text_path, true, MapCoordF(0, 0), QSizeF(), &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(limited.getNumObjects(), 1);
	}
	
	/// Accept (2)
	void fileTest()
	{
		Map map;
		QString error;
		auto* png = addImage(map, smallPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(png, qPrintable(error));
		png->setCorners(MapCoordF(5, 5), QSizeF(10, 4), 0.3);
		png->setOpacity(0.4);
		png->setLayer(ImageObject::BelowMap);
		auto* jpeg = addImage(map, photoJpeg(), true, MapCoordF(30, 0), QSizeF(), &error);
		QVERIFY2(jpeg, qPrintable(error));
		QCOMPARE(jpeg->format(), QStringLiteral("jpeg"));
		
		auto const with = save(map, true);
		QVERIFY(!with.isEmpty());
		Map loaded;
		QVERIFY(load(with, loaded));
		QCOMPARE(loaded.getNumObjects(), 2);
		auto const* loaded_png = imageAt(loaded, 0, 0);
		auto const* loaded_jpeg = imageAt(loaded, 0, 1);
		QVERIFY(loaded_png && loaded_jpeg);
		QCOMPARE(loaded_png->data(), fileBytes(smallPng()));
		QCOMPARE(loaded_jpeg->data(), fileBytes(photoJpeg()));
		for (MapCoordVector::size_type i = 0; i < 4; ++i)
		{
			QVERIFY(loaded_png->getRawCoordinateVector()[i] == png->getRawCoordinateVector()[i]);
			QVERIFY(loaded_jpeg->getRawCoordinateVector()[i] == jpeg->getRawCoordinateVector()[i]);
		}
		QCOMPARE(loaded_png->opacity(), 0.4);
		QVERIFY(loaded_png->layer() == ImageObject::BelowMap);
		QVERIFY(loaded_jpeg->layer() == ImageObject::AboveMap);
		QCOMPARE(loaded_jpeg->opacity(), 1.0);
		QCOMPARE(loaded_png->href(), QStringLiteral("small.png"));
		QVERIFY(loaded_png->equals(png, true));
		QVERIFY(loaded_jpeg->equals(jpeg, true));
		
		auto const without = save(map, false);
		QVERIFY(!without.isEmpty());
		QVERIFY(!without.contains("type=\"3\""));
		QVERIFY(!without.contains("<image"));
		QVERIFY(without.contains("<objects count=\"0\""));
	}
	
	/// Accept (3)
	void drawTest()
	{
		Map map;
		auto* color = new MapColor(QStringLiteral("Blue"), 0);
		color->setCmyk({ 1.0f, 1.0f, 0.0f, 0.0f });
		color->setRgbFromCmyk();
		map.addColor(color, 0);
		auto* area_symbol = new AreaSymbol();
		area_symbol->setColor(color);
		map.addSymbol(area_symbol, 0);
		MapCoordVector coords { MapCoord(-10, -10), MapCoord(10, -10), MapCoord(10, 10), MapCoord(-10, 10), MapCoord(-10, -10) };
		coords.back().setClosePoint(true);
		map.addObject(new PathObject(area_symbol, coords));
		
		auto* part = new MapPart(QStringLiteral("Images"), &map);
		map.addPart(part, 1);
		map.setCurrentPartIndex(1);
		QString error;
		auto* image = addImage(map, redPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		QCOMPARE(image->size(), QSizeF(10, 10));
		
		// 10 mm at 5 pixels per mm: 2,500 pixels of the 10,000 of the area.
		QCOMPARE(countPixels(map, Red), 2500);
		QCOMPARE(countPixels(map, Blue), 7500);
		
		image->setLayer(ImageObject::BelowMap);
		QCOMPARE(countPixels(map, Red), 0);
		QCOMPARE(countPixels(map, Blue), 10000);
		
		image->setLayer(ImageObject::AboveMap);
		part->setVisible(false);
		QCOMPARE(countPixels(map, Red), 0);
		QCOMPARE(countPixels(map, Blue), 10000);
		part->setVisible(true);
		QCOMPARE(countPixels(map, Red), 2500);
		
		Map plain;
		auto* faint = addImage(plain, redPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(faint, qPrintable(error));
		faint->setOpacity(0.5);
		QCOMPARE(countPixels(plain, LightRed), 2500);
		QCOMPARE(countPixels(plain, Red), 0);
	}
	
	/// Accept (4)
	void scaleUndoTest()
	{
		Map map;
		QString error;
		auto* image = addImage(map, smallPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		image->setCorners(MapCoordF(3.3, 4.4), QSizeF(10, 5), 0.2);
		std::array<MapCoord, 4> before;
		for (std::size_t i = 0; i < 4; ++i)
			before[i] = image->getRawCoordinateVector()[MapCoordVector::size_type(i)];
		
		auto* undo = new ReplaceObjectsUndoStep(&map);
		undo->addObject(0, image->duplicate());
		image->scale(MapCoordF(1, 1), 1.37);
		image->update();
		map.push(undo);
		QVERIFY(qAbs(image->size().width() - 13.7) < 0.01);
		QVERIFY(qAbs(image->size().height() - 6.85) < 0.01);
		QVERIFY(qAbs(image->imageRotation() - 0.2) < 0.001);
		
		QVERIFY(map.undoManager().undo());
		auto const* restored = imageAt(map, 0, 0);
		QVERIFY(restored);
		for (std::size_t i = 0; i < 4; ++i)
			QVERIFY(restored->getRawCoordinateVector()[MapCoordVector::size_type(i)] == before[i]);
		QCOMPARE(restored->data(), fileBytes(smallPng()));
	}
	
	/// Accept (5)
	void hitTest()
	{
		Map map;
		QString error;
		auto* image = addImage(map, redPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		image->setCorners(MapCoordF(0, 0), QSizeF(10, 10), M_PI / 4);
		image->update();
		QVERIFY(image->getExtent().contains(QPointF(6.5, 6.5)));
		QVERIFY(image->isPointOnObject(MapCoordF(0, 0), 0.1, false, false) != Symbol::NoSymbol);
		QVERIFY(image->isPointOnObject(MapCoordF(3, 3), 0.1, false, false) != Symbol::NoSymbol);
		QCOMPARE(image->isPointOnObject(MapCoordF(6.5, 6.5), 0.1, false, false), int(Symbol::NoSymbol));
		QVERIFY(image->intersectsBox(QRectF(-1, -1, 2, 2)));
		QVERIFY(!image->intersectsBox(QRectF(6, 6, 1, 1)));
	}
	
	/// Accept (6)
	void linkTest()
	{
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("project")));
		auto const logo = file("project/logo.png");
		QVERIFY(writeImage(logo, 100, 100, QColor(255, 0, 0), 10000, "PNG"));
		auto const map_path = file("project/linked.foom");
		{
			Map map;
			QString error;
			auto* image = addImage(map, logo, false, MapCoordF(0, 0), QSizeF(), &error);
			QVERIFY2(image, qPrintable(error));
			QVERIFY(image->isLinked());
			QVERIFY(image->data().isEmpty());
			QCOMPARE(image->image().size(), QSize(100, 100));
			QCOMPARE(countPixels(map, Red), 2500);
			QVERIFY(saveToFile(map, map_path));
		}
		auto const text = fileBytes(map_path);
		QVERIFY(text.contains("href=\"logo.png\""));
		QVERIFY(!text.contains("<data>"));
		
		Map loaded;
		QVERIFY(loaded.loadFrom(map_path));
		QCOMPARE(loaded.getNumObjects(), 1);
		auto* image = const_cast<ImageObject*>(imageAt(loaded, 0, 0));
		QVERIFY(image);
		QVERIFY(image->isLinked());
		QCOMPARE(QFileInfo(image->linkedFile()).canonicalFilePath(), QFileInfo(logo).canonicalFilePath());
		QCOMPARE(image->image().size(), QSize(100, 100));
		QVERIFY(!isLinkedImageSelected(loaded));
		
		loaded.addObjectToSelection(image, false);
		QVERIFY(isLinkedImageSelected(loaded));
		auto const steps = loaded.undoManager().undoStepCount();
		QCOMPARE(embedSelectedImages(loaded), 1);
		QCOMPARE(loaded.undoManager().undoStepCount(), steps + 1);
		QVERIFY(!image->isLinked());
		QCOMPARE(image->data(), fileBytes(logo));
		QCOMPARE(image->href(), QStringLiteral("logo.png"));
		QCOMPARE(embedSelectedImages(loaded), 0);
		
		QVERIFY(loaded.undoManager().undo());
		auto const* again = imageAt(loaded, 0, 0);
		QVERIFY(again);
		QVERIFY(again->isLinked());
		QVERIFY(again->data().isEmpty());
	}
	
	/// Accept (7)
	void duplicateTest()
	{
		Map map;
		QString error;
		auto* image = addImage(map, smallPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		std::unique_ptr<ImageObject> copy { image->duplicate() };
		QVERIFY(copy->data().constData() == image->data().constData());
		QVERIFY(copy->equals(image, true));
		copy->setOpacity(0.5);
		QVERIFY(!copy->equals(image, true));
	}
	
	/// Accept (9)
	void propertiesTest()
	{
		Map map;
		auto* color = new MapColor(QStringLiteral("Blue"), 0);
		map.addColor(color, 0);
		auto* area_symbol = new AreaSymbol();
		area_symbol->setColor(color);
		map.addSymbol(area_symbol, 0);
		MapCoordVector coords { MapCoord(-10, -10), MapCoord(10, -10), MapCoord(10, 10), MapCoord(-10, -10) };
		coords.back().setClosePoint(true);
		auto* area = new PathObject(area_symbol, coords);
		map.addObject(area);
		QVERIFY(!firstSelectedImage(map));
		
		QString error;
		auto* first = addImage(map, smallPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(first, qPrintable(error));
		auto* second = addImage(map, redPng(), true, MapCoordF(30, 0), QSizeF(), &error);
		QVERIFY2(second, qPrintable(error));
		map.clearObjectSelection(false);
		map.addObjectToSelection(area, false);
		QVERIFY(!firstSelectedImage(map));
		QCOMPARE(setSelectedImageProperties(map, 0.4, ImageObject::BelowMap), 0);
		map.addObjectToSelection(first, false);
		map.addObjectToSelection(second, false);
		QVERIFY(firstSelectedImage(map) == first || firstSelectedImage(map) == second);
		
		auto const steps = map.undoManager().undoStepCount();
		QCOMPARE(setSelectedImageProperties(map, 0.4, ImageObject::BelowMap), 2);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(first->opacity(), 0.4);
		QCOMPARE(second->opacity(), 0.4);
		QVERIFY(first->layer() == ImageObject::BelowMap);
		QVERIFY(second->layer() == ImageObject::BelowMap);
		QVERIFY(map.getPart(0)->getObject(0) == area);
		
		QCOMPARE(setSelectedImageProperties(map, 0.4, ImageObject::BelowMap), 0);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		
		QVERIFY(map.undoManager().undo());
		for (int i = 1; i <= 2; ++i)
		{
			auto const* restored = imageAt(map, 0, i);
			QVERIFY(restored);
			QCOMPARE(restored->opacity(), 1.0);
			QVERIFY(restored->layer() == ImageObject::AboveMap);
		}
	}
	
	/// Accept (8)
	void pdfTest()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));
		
		// 2,000 by 1,000 pixels at 20 dots per mm: 100 by 50 mm.
		auto const big = file("big.png");
		{
			QImage image(2000, 1000, QImage::Format_RGB32);
			for (int y = 0; y < image.height(); ++y)
			{
				auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
				for (int x = 0; x < image.width(); ++x)
					line[x] = qRgb(x % 256, y % 256, (x + y) % 256);
			}
			image.setDotsPerMeterX(20000);
			image.setDotsPerMeterY(20000);
			QVERIFY(image.save(big, "PNG"));
		}
		auto const map_path = file("big.foom");
		{
			Map map;
			QString error;
			auto* image = addImage(map, big, true, MapCoordF(0, 0), QSizeF(), &error);
			QVERIFY2(image, qPrintable(error));
			QCOMPARE(image->size(), QSizeF(100, 50));
			QVERIFY(saveToFile(map, map_path));
		}
		
		auto const pdf_path = file("big.pdf");
		auto const job = QJsonObject{ { QStringLiteral("steps"), QJsonArray{
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } },
		    QJsonObject{ { QStringLiteral("step"), QStringLiteral("export") }, { QStringLiteral("path"), pdf_path } },
		} } };
		auto const job_path = file("pdf.json");
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
		output += process.readAllStandardError();
		QVERIFY2(process.exitCode() == 0, output.constData());
		
		auto const pdf = fileBytes(pdf_path);
		QVERIFY(pdf.startsWith("%PDF"));
		QCOMPARE(pdf.count("/Subtype /Image"), 1);
		QVERIFY(pdf.contains("/Width 2000"));
		QVERIFY(pdf.contains("/Height 1000"));
	}
};



/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(ImageObjectTest)
#include "image_object_t.moc"  // IWYU pragma: keep
