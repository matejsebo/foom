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
#include <QLineF>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QSize>
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
#include "core/objects/object_mover.h"
#include "core/ops/gradient.h"
#include "core/print_output.h"
#include "core/renderables/renderable.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/image_symbol.h"
#include "core/symbols/line_symbol.h"
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

/// The corners of an upright image of the size, centred at the origin.
std::array<MapCoordF, 4> uprightCorners(double width, double height)
{
	return { MapCoordF(-width / 2, -height / 2), MapCoordF(width / 2, -height / 2),
	         MapCoordF(width / 2, height / 2), MapCoordF(-width / 2, height / 2) };
}

bool near(const MapCoordF& a, const MapCoordF& b, double tolerance = 0.002)
{
	return a.distanceTo(b) < tolerance;
}

PathObject* addSquarePath(Map& map, const Symbol* symbol, double x, double y, double width, double height)
{
	MapCoordVector coords { MapCoord(x, y), MapCoord(x + width, y), MapCoord(x + width, y + height), MapCoord(x, y + height), MapCoord(x, y) };
	coords.back().setClosePoint(true);
	auto* path = new PathObject(symbol, coords);
	map.addObject(path);
	return path;
}

/// The ink of a plate at a point of the map, 0 to 255; area and dpi as in plateInk() callers.
int inkAt(const RasterPlate& plate, const QRectF& area, double dpi, const MapCoordF& point)
{
	auto const scale = dpi / 25.4;
	auto const x = int((point.x() - area.left()) * scale);
	auto const y = int((point.y() - area.top()) * scale);
	return qGray(plate.ink.pixel(x, y));
}

const RasterPlate* plateNamed(const std::vector<RasterPlate>& plates, const QString& name)
{
	for (auto const& plate : plates)
	{
		if (plate.name == name)
			return &plate;
	}
	return nullptr;
}

/// The screen colour at a point of the map, drawn as in countPixels().
QColor screenColorAt(Map& map, const MapCoordF& point)
{
	map.updateObjects();
	QRectF const extent(-20, -20, 40, 40);
	constexpr double pixel_per_mm = 5;
	QImage image(int(extent.width() * pixel_per_mm), int(extent.height() * pixel_per_mm), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(Qt::white));
	QPainter painter(&image);
	painter.scale(pixel_per_mm, pixel_per_mm);
	painter.translate(-extent.topLeft());
	map.draw(&painter, RenderConfig{ map, extent, pixel_per_mm, RenderConfig::DisableAntialiasing, 1 });
	painter.end();
	return QColor(image.pixel(int((point.x() - extent.left()) * pixel_per_mm), int((point.y() - extent.top()) * pixel_per_mm)));
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
	/// 100 by 100 black pixels at 10 dots per mm: 10 by 10 mm.
	QString blackPng() const { return file("black.png"); }
	/// 100 by 100 white pixels at 10 dots per mm: 10 by 10 mm.
	QString whitePng() const { return file("white.png"); }
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QVERIFY(writeImage(smallPng(), 200, 100, QColor(Qt::green), 200000, "PNG"));
		QVERIFY(writeImage(redPng(), 100, 100, QColor(255, 0, 0), 10000, "PNG"));
		QVERIFY(writeImage(photoJpeg(), 64, 48, QColor(40, 90, 200), 10000, "JPEG"));
		QVERIFY(writeImage(blackPng(), 100, 100, QColor(0, 0, 0), 10000, "PNG"));
		QVERIFY(writeImage(whitePng(), 100, 100, QColor(255, 255, 255), 10000, "PNG"));
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
		// All four corners are in the extent (seen on 2026-11-24: the first was left out,
		// so a turned image was not redrawn there).
		for (auto const& corner : image->corners())
			QVERIFY(image->getExtent().adjusted(-0.001, -0.001, 0.001, 0.001).contains(QPointF(corner)));
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

	/// Accept (10)
	void cornerHandleTest()
	{
		auto const corners = uprightCorners(20, 10);
		auto kept = imageCornersDraggedTo(corners, 2, MapCoordF(20, 20), true);
		QVERIFY(near(kept[0], MapCoordF(-10, -5)));
		QVERIFY(near(kept[2], MapCoordF(24, 12)));  // 34 by 17
		QVERIFY(near(kept[1], MapCoordF(24, -5)));
		QVERIFY(near(kept[3], MapCoordF(-10, 12)));

		auto free = imageCornersDraggedTo(corners, 2, MapCoordF(20, 20), false);
		QVERIFY(near(free[0], MapCoordF(-10, -5)));
		QVERIFY(near(free[2], MapCoordF(20, 20)));  // 30 by 25

		auto smallest = imageCornersDraggedTo(corners, 2, MapCoordF(-20, -20), false);
		QVERIFY(near(smallest[2], MapCoordF(-9.9, -4.9)));
		smallest = imageCornersDraggedTo(corners, 2, MapCoordF(-20, -20), true);
		QVERIFY(smallest[0].distanceTo(smallest[1]) >= 0.0999);
		QVERIFY(smallest[0].distanceTo(smallest[3]) >= 0.0999);
		QVERIFY(near(smallest[0], MapCoordF(-10, -5)));

		ImageObject rotated;
		rotated.setCorners(MapCoordF(0, 0), QSizeF(20, 10), M_PI / 2);
		rotated.setCorners(imageCornersDraggedTo(rotated.corners(), 2, MapCoordF(30, 30), true));
		QVERIFY(qAbs(rotated.imageRotation() - M_PI / 2) < 0.001);
		QVERIFY(qAbs(rotated.size().width() / rotated.size().height() - 2) < 0.001);

		// The edit point tool's mover
		for (auto keep_aspect : { true, false })
		{
			Map map;
			QString error;
			auto* image = addImage(map, redPng(), true, MapCoordF(0, 0), QSizeF(), &error);
			QVERIFY2(image, qPrintable(error));
			auto const before = image->corners();
			ObjectMover mover(&map, before[2]);
			mover.addImageCorner(image, 2);
			mover.setKeepImageAspect(keep_aspect);
			mover.move(MapCoordF(before[2].x() + 10, before[2].y() + 15), ObjectMover::Never);
			auto const expected = imageCornersDraggedTo(before, 2, MapCoordF(before[2].x() + 10, before[2].y() + 15), keep_aspect);
			auto const after = image->corners();
			for (std::size_t i = 0; i < 4; ++i)
				QVERIFY(near(after[i], expected[i]));
		}
	}

	/// Accept (11)
	void clipTest()
	{
		Map map;
		auto* color = new MapColor(QStringLiteral("Blue"), 0);
		color->setCmyk({ 1.0f, 1.0f, 0.0f, 0.0f });
		color->setRgbFromCmyk();
		map.addColor(color, 0);
		auto* line_symbol = new LineSymbol();
		line_symbol->setColor(color);
		line_symbol->setLineWidth(0.01);
		map.addSymbol(line_symbol, 0);

		QString error;
		auto* image = addImage(map, redPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(image, qPrintable(error));
		QCOMPARE(countPixels(map, Red), 2500);
		auto* path = addSquarePath(map, line_symbol, -6, -6, 6, 12);
		map.clearObjectSelection(false);
		map.addObjectToSelection(image, false);
		map.addObjectToSelection(path, false);
		QVERIFY(canClipSelectedImages(map));
		auto const steps = map.undoManager().undoStepCount();
		QCOMPARE(clipSelectedImages(map), 1);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QCOMPARE(map.getNumObjects(), 1);
		QVERIFY(!image->clip().empty());
		QCOMPARE(countPixels(map, Red), 1250);
		QVERIFY(!image->containsPoint(MapCoordF(2.5, 0)));
		QVERIFY(image->containsPoint(MapCoordF(-2.5, 0)));

		Map reloaded;
		QVERIFY(load(save(map, true), reloaded));
		auto const* reloaded_image = imageAt(reloaded, 0, 0);
		QVERIFY(reloaded_image);
		QCOMPARE(reloaded_image->clip().size(), image->clip().size());
		for (std::size_t r = 0; r < image->clip().size(); ++r)
		{
			auto const& a = image->clip()[r];
			auto const& b = reloaded_image->clip()[r];
			QCOMPARE(a.size(), b.size());
			for (int i = 0; i < a.size(); ++i)
				QVERIFY(QLineF(a[i], b[i]).length() < 0.000001);
		}

		image->scale(MapCoordF(0, 0), 2);
		QCOMPARE(countPixels(map, Red), 5000);
		image->scale(MapCoordF(0, 0), 0.5);

		map.clearObjectSelection(false);
		map.addObjectToSelection(image, false);
		QCOMPARE(unclipSelectedImages(map), 1);
		auto const* unclipped = imageAt(map, 0, 0);
		QVERIFY(unclipped->clip().empty());
		QCOMPARE(countPixels(map, Red), 2500);
		QVERIFY(map.undoManager().undo());
		QVERIFY(!imageAt(map, 0, 0)->clip().empty());
		QCOMPARE(countPixels(map, Red), 1250);
	}

	/// Accept (12)
	void spotColorTest()
	{
		Map map;
		auto* black = new MapColor(QStringLiteral("Black"), 0);
		black->setCmyk({ 0.0f, 0.0f, 0.0f, 1.0f });
		black->setRgbFromCmyk();
		map.addColor(black, 0);
		auto* blue = new MapColor(QStringLiteral("Blue"), 1);
		blue->setSpotColorName(QStringLiteral("Blue"));
		blue->setCmyk({ 1.0f, 0.5f, 0.0f, 0.0f });
		blue->setRgbFromCmyk();
		map.addColor(blue, 1);

		QString error;
		auto* spot = addImage(map, blackPng(), true, MapCoordF(0, 0), QSizeF(), &error);
		QVERIFY2(spot, qPrintable(error));
		spot->setSpotColorName(QStringLiteral("Blue"));
		QCOMPARE(spot->spotColor(), blue);
		auto* process = addImage(map, blackPng(), true, MapCoordF(15, 0), QSizeF(), &error);
		QVERIFY2(process, qPrintable(error));
		map.updateObjects();

		QRectF const area(-20, -20, 40, 40);
		constexpr double dpi = 127;  // 5 pixels per mm
		auto const spot_plates = renderSpotPlates(map, area, dpi);
		auto const* blue_plate = plateNamed(spot_plates, QStringLiteral("Blue"));
		QVERIFY(blue_plate);
		QVERIFY(inkAt(*blue_plate, area, dpi, MapCoordF(0, 0)) > 250);
		QVERIFY(inkAt(*blue_plate, area, dpi, MapCoordF(15, 0)) < 5);
		auto const process_plates = renderProcessPlates(map, area, dpi);
		QCOMPARE(int(process_plates.size()), 4);
		for (auto const& plate : process_plates)
			QVERIFY2(inkAt(plate, area, dpi, MapCoordF(0, 0)) < 5, qPrintable(plate.name));
		QVERIFY(inkAt(process_plates[3], area, dpi, MapCoordF(15, 0)) > 250);

		auto const on_screen = screenColorAt(map, MapCoordF(0, 0));
		auto const blue_rgb = static_cast<const QColor&>(*blue);
		QVERIFY(qAbs(on_screen.red() - blue_rgb.red()) < 4);
		QVERIFY(qAbs(on_screen.green() - blue_rgb.green()) < 4);
		QVERIFY(qAbs(on_screen.blue() - blue_rgb.blue()) < 4);

		// White: no ink
		auto* white = addImage(map, whitePng(), true, MapCoordF(-12, 12), QSizeF(), &error);
		QVERIFY2(white, qPrintable(error));
		white->setSpotColorName(QStringLiteral("Blue"));
		map.updateObjects();
		auto const plates_with_white = renderSpotPlates(map, area, dpi);
		QVERIFY(inkAt(*plateNamed(plates_with_white, QStringLiteral("Blue")), area, dpi, MapCoordF(-12, 12)) < 5);
		QVERIFY(screenColorAt(map, MapCoordF(-12, 12)).lightness() > 250);

		// A name the map does not have
		process->setSpotColorName(QStringLiteral("Green"));
		QVERIFY(!process->spotColor());
		map.updateObjects();
		QVERIFY(inkAt(renderProcessPlates(map, area, dpi)[3], area, dpi, MapCoordF(15, 0)) > 250);
		QVERIFY(screenColorAt(map, MapCoordF(15, 0)).lightness() < 5);

		Map reloaded;
		QVERIFY(load(save(map, true), reloaded));
		QCOMPARE(imageAt(reloaded, 0, 0)->spotColorName(), QStringLiteral("Blue"));
		QCOMPARE(imageAt(reloaded, 0, 0)->spotColor(), reloaded.getColor(1));
		QCOMPARE(imageAt(reloaded, 0, 1)->spotColorName(), QStringLiteral("Green"));

		map.clearObjectSelection(false);
		map.addObjectToSelection(process, false);
		map.addObjectToSelection(white, false);
		auto const steps = map.undoManager().undoStepCount();
		QCOMPARE(setSelectedImageProperties(map, 1, ImageObject::AboveMap, QString()), 2);
		QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
		QVERIFY(imageAt(map, 0, 1)->spotColorName().isEmpty());
		QVERIFY(imageAt(map, 0, 2)->spotColorName().isEmpty());
		QCOMPARE(setSelectedImageProperties(map, 1, ImageObject::AboveMap, QString()), 0);
	}

	/// Accept (13)
	void gradientTest()
	{
		Map map;
		auto* color = new MapColor(QStringLiteral("Green"), 0);
		color->setCmyk({ 1.0f, 0.0f, 1.0f, 0.0f });
		color->setRgbFromCmyk();
		map.addColor(color, 0);
		auto* area_symbol = new AreaSymbol();
		area_symbol->setColor(color);
		map.addSymbol(area_symbol, 0);
		auto* line_symbol = new LineSymbol();
		line_symbol->setColor(color);
		line_symbol->setLineWidth(0.1);
		map.addSymbol(line_symbol, 1);
		auto const* square = addSquarePath(map, area_symbol, 0, 0, 10, 10);

		ops::GradientParams params;
		params.from_color = QColor(255, 0, 0);
		params.to_color = QColor(0, 0, 255);
		params.angle = 0;
		auto result = ops::gradientFill(map, { square }, params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 1);
		QCOMPARE(int(result.consumed.size()), 1);
		QCOMPARE(result.consumed[0], square);
		QCOMPARE(result.out[0]->getType(), Object::Image);
		auto const* image = static_cast<const ImageObject*>(result.out[0].get());
		QVERIFY(near(image->corners()[0], MapCoordF(0, 0)));
		QVERIFY(near(image->corners()[2], MapCoordF(10, 10)));
		QCOMPARE(image->format(), QStringLiteral("png"));
		auto pixels = image->image().convertToFormat(QImage::Format_ARGB32);
		QCOMPARE(pixels.size(), QSize(118, 118));
		auto const left = QColor(pixels.pixel(0, 59));
		auto const right = QColor(pixels.pixel(117, 59));
		QVERIFY(left.red() > 240 && left.blue() < 15);
		QVERIFY(right.blue() > 240 && right.red() < 15);

		params.keep_areas = true;
		result = ops::gradientFill(map, { square }, params);
		QCOMPARE(int(result.out.size()), 1);
		QVERIFY(result.consumed.empty());

		params.angle = 90;
		result = ops::gradientFill(map, { square }, params);
		pixels = static_cast<const ImageObject*>(result.out[0].get())->image().convertToFormat(QImage::Format_ARGB32);
		QVERIFY(QColor(pixels.pixel(59, 117)).red() > 240);
		QVERIFY(QColor(pixels.pixel(59, 0)).blue() > 240);

		auto* line = new PathObject(line_symbol, MapCoordVector{ MapCoord(5.0, 0.0), MapCoord(5.0, 10.0) });
		map.addObject(line);
		params.use_line = true;
		result = ops::gradientFill(map, { square, line }, params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 1);
		pixels = static_cast<const ImageObject*>(result.out[0].get())->image().convertToFormat(QImage::Format_ARGB32);
		QVERIFY(QColor(pixels.pixel(59, 0)).red() > 240);
		QVERIFY(QColor(pixels.pixel(59, 117)).blue() > 240);
		params.use_line = false;

		MapCoordVector triangle { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0), MapCoord(0.0, 10.0), MapCoord(0.0, 0.0) };
		triangle.back().setClosePoint(true);
		auto* triangle_object = new PathObject(area_symbol, triangle);
		map.addObject(triangle_object);
		result = ops::gradientFill(map, { triangle_object }, params);
		pixels = static_cast<const ImageObject*>(result.out[0].get())->image().convertToFormat(QImage::Format_ARGB32);
		QCOMPARE(qAlpha(pixels.pixel(117, 117)), 0);
		QCOMPARE(qAlpha(pixels.pixel(2, 2)), 255);

		result = ops::gradientFill(map, { line }, params);
		QVERIFY(!result.error.isEmpty());
		QVERIFY(result.out.empty());
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
