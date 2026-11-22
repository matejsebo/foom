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


#include <cmath>
#include <memory>

#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "templates/world_file.h"
#ifdef MAPPER_USE_GDAL
#  include "gdal/gdal_template.h"
#endif

using namespace OpenOrienteering;


namespace {

constexpr qint64 two_mb = 2 * 1024 * 1024;

bool saveMap(const Map& map, const QString& path)
{
	auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsWriting);
	if (!format)
		return false;
	auto exporter = format->makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

bool loadMap(Map& map, const QString& path)
{
	auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsReading);
	if (!format)
		return false;
	auto importer = format->makeImporter(path, &map, nullptr);
	return importer && importer->doImport();
}

bool writeImage(const QString& path, const QColor& color)
{
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(color);
	return image.save(path, "PNG");
}

bool writeText(const QString& path, const QByteArray& text)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text) == text.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace



/**
 * @test Tests raster templates embedded in the map file.
 *
 * See the design notes, "Embedded raster templates".
 */
class TemplateEmbedTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	void writeSource() const
	{
		QVERIFY(writeImage(file("a.png"), Qt::green));
		// 1 m pixels, the upper left corner at (500000, 5000000)
		QVERIFY(writeText(file("a.pgw"), "1.0\n0.0\n0.0\n-1.0\n500000.5\n4999999.5\n"));
	}
	
	/** Makes a map with the embedded template a.png and saves it. */
	void makeEmbeddedMap(const QString& map_path) const
	{
		writeSource();
		Map map;
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("a.png"), &map));
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		QVERIFY(temp->loadTemplateFile());
		QString error;
		QVERIFY2(temp->embed(two_mb, &error), qPrintable(error));
		QVERIFY(saveMap(map, map_path));
	}
	
	/** Opens the map and loads its template. */
	TemplateImage* openMap(Map& map, const QString& map_path) const
	{
		if (!loadMap(map, map_path) || map.getNumTemplates() != 1)
			return nullptr;
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		if (temp && temp->getTemplateState() != Template::Loaded)
			temp->loadTemplateFile();
		return temp;
	}
	
private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
	}
	
	/// Accept (1), (4)
	void embedTest()
	{
		writeSource();
		Map map;
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("a.png"), &map));
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		QVERIFY(temp->loadTemplateFile());
		QVERIFY(!temp->isEmbedded());
		QString error;
		QVERIFY2(temp->embed(two_mb, &error), qPrintable(error));
		QVERIFY(temp->isEmbedded());
		QVERIFY(map.areTemplatesDirty());
		
		auto copy = std::unique_ptr<Template>(temp->duplicate());
		QVERIFY(qobject_cast<TemplateImage*>(copy.get())->isEmbedded());
		
		QVERIFY(saveMap(map, file("embed.foom")));
		auto const xml = readFile(file("embed.foom"));
		QCOMPARE(xml.count("<data "), 1);
		QVERIFY(xml.contains("format=\""));
		QVERIFY(xml.contains("source=\""));
		QVERIFY(xml.contains("pixel_to_world=\""));
	}
	
	/// Accept (2)
	void withoutFileTest()
	{
		makeEmbeddedMap(file("gone.foom"));
		QVERIFY(QFile::remove(file("a.png")));
		QVERIFY(QFile::remove(file("a.pgw")));
		Map map;
		auto* temp = openMap(map, file("gone.foom"));
		QVERIFY(temp);
		QVERIFY(temp->isEmbedded());
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QCOMPARE(temp->getImage().size(), QSize(4, 4));
		QCOMPARE(QColor(temp->getImage().pixel(1, 1)), QColor(Qt::green));
		QVERIFY(temp->canChangeTemplateGeoreferenced());
	}
	
	/// Accept (3)
	void changedFileTest()
	{
		makeEmbeddedMap(file("changed.foom"));
		QVERIFY(writeImage(file("a.png"), Qt::red));
		Map map;
		auto* temp = openMap(map, file("changed.foom"));
		QVERIFY(temp);
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QCOMPARE(QColor(temp->getImage().pixel(1, 1)), QColor(Qt::green));
	}
	
	/// Accept (5)
	void refusedTest()
	{
		writeSource();
		Map map;
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("a.png"), &map));
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		QVERIFY(temp->loadTemplateFile());
		QString error;
		QVERIFY(!temp->embed(10, &error));
		QVERIFY(!error.isEmpty());
		QVERIFY(!temp->isEmbedded());
		
		QVERIFY(writeText(file("b.png"), "this is no image"));
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("b.png"), &map));
		auto* text = qobject_cast<TemplateImage*>(map.getTemplate(1));
		QVERIFY(!text->loadTemplateFile());
		error.clear();
		QVERIFY(!text->embed(two_mb, &error));
		QVERIFY(!error.isEmpty());
	}
	
	/// Accept (6)
	void unembedTest()
	{
		makeEmbeddedMap(file("unembed.foom"));
		auto const original = readFile(file("a.png"));
		QVERIFY(QFile::remove(file("a.png")));
		Map map;
		auto* temp = openMap(map, file("unembed.foom"));
		QVERIFY(temp);
		QString error;
		QVERIFY2(temp->unembed(&error), qPrintable(error));
		QVERIFY(!temp->isEmbedded());
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QCOMPARE(readFile(file("a.png")), original);
		
		QVERIFY(temp->embed(two_mb, &error));
		QVERIFY(temp->unembed(&error));
		QCOMPARE(readFile(file("a.png")), original);
	}
	
	/// Accept (9)
	void worldFileTest()
	{
		double const expected[6] = { 1.0, 0.0, 0.0, -1.0, 500000.5, 4999999.5 };
		auto const check = [&](const char* name) {
			WorldFile written;
			if (!written.load(file(name)))
				return false;
			for (int i = 0; i < 6; ++i)
			{
				if (std::abs(written.parameters[i] - expected[i]) > 1e-9)
					return false;
			}
			return true;
		};

		// Image and world file missing: both are written
		makeEmbeddedMap(file("world.foom"));
		QVERIFY(QFile::remove(file("a.png")));
		QVERIFY(QFile::remove(file("a.pgw")));
		{
			Map map;
			auto* temp = openMap(map, file("world.foom"));
			QVERIFY(temp);
			QString error;
			QVERIFY2(temp->unembed(&error), qPrintable(error));
			QVERIFY(QFileInfo::exists(file("a.png")));
			QVERIFY(check("a.pgw"));
		}

		// A world file which is there stays as it is
		makeEmbeddedMap(file("world.foom"));
		QVERIFY(QFile::remove(file("a.png")));
		QVERIFY(writeText(file("a.pgw"), "2.0\n0.0\n0.0\n-2.0\n1.0\n2.0\n"));
		{
			Map map;
			auto* temp = openMap(map, file("world.foom"));
			QVERIFY(temp);
			QString error;
			QVERIFY2(temp->unembed(&error), qPrintable(error));
			QCOMPARE(readFile(file("a.pgw")), QByteArray("2.0\n0.0\n0.0\n-2.0\n1.0\n2.0\n"));
		}

		// A template without a transform writes no world file
		QVERIFY(writeImage(file("c.png"), Qt::blue));
		{
			Map map;
			map.addTemplate(-1, std::make_unique<TemplateImage>(file("c.png"), &map));
			auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
			QVERIFY(temp->loadTemplateFile());
			QString error;
			QVERIFY2(temp->embed(two_mb, &error), qPrintable(error));
			QVERIFY(QFile::remove(file("c.png")));
			QVERIFY2(temp->unembed(&error), qPrintable(error));
			QVERIFY(QFileInfo::exists(file("c.png")));
			QVERIFY(!QFileInfo::exists(file("c.pgw")));
		}

#ifdef MAPPER_USE_GDAL
		// The same for a GdalTemplate, whose transform GDAL read from the world file
		writeSource();
		{
			Map map;
			map.addTemplate(-1, std::make_unique<GdalTemplate>(file("a.png"), &map));
			auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
			QVERIFY(temp->loadTemplateFile());
			QString error;
			QVERIFY2(temp->embed(two_mb, &error), qPrintable(error));
			QVERIFY(QFile::remove(file("a.png")));
			QVERIFY(QFile::remove(file("a.pgw")));
			QVERIFY2(temp->unembed(&error), qPrintable(error));
			QVERIFY(check("a.pgw"));
		}
#endif
	}

	/// Accept (7)
	void omapTest()
	{
		makeEmbeddedMap(file("embed.omap"));
		QVERIFY(QFile::remove(file("a.png")));
		Map map;
		auto* temp = openMap(map, file("embed.omap"));
		QVERIFY(temp);
		QVERIFY(temp->isEmbedded());
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
	}
	
#ifdef MAPPER_USE_GDAL
	/// Accept (8)
	void gdalTest()
	{
		writeSource();
		{
			Map map;
			map.addTemplate(-1, std::make_unique<GdalTemplate>(file("a.png"), &map));
			auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
			QVERIFY(temp->loadTemplateFile());
			QString error;
			QVERIFY2(temp->embed(two_mb, &error), qPrintable(error));
			QVERIFY(saveMap(map, file("gdal.foom")));
		}
		QVERIFY(QFile::remove(file("a.png")));
		QVERIFY(QFile::remove(file("a.pgw")));
		Map map;
		auto* temp = openMap(map, file("gdal.foom"));
		QVERIFY(temp);
		QVERIFY(dynamic_cast<GdalTemplate*>(temp));
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QCOMPARE(QColor(temp->getImage().pixel(1, 1)), QColor(Qt::green));
	}
#endif
};


QTEST_MAIN(TemplateEmbedTest)
#include "template_embed_t.moc"  // IWYU pragma: keep
