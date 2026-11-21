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


#include <memory>

#include <QtTest>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLatin1Char>
#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "templates/template_relink.h"

using namespace OpenOrienteering;


namespace {

bool saveMap(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

bool loadMap(Map& map, const QString& path, QString* warnings = nullptr)
{
	XMLFileFormat format;
	auto importer = format.makeImporter(path, &map, nullptr);
	if (!importer || !importer->doImport())
		return false;
	if (warnings)
	{
		for (auto const& warning : importer->warnings())
			*warnings += warning + QLatin1Char(' ');
	}
	return true;
}

bool writeImage(const QString& path)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(QColor(Qt::green));
	return image.save(path, "PNG");
}

int indexOf(const Map& map, const QString& file_name)
{
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		if (map.getTemplate(i)->getTemplateFilename() == file_name)
			return i;
	}
	return -1;
}

}  // namespace



/**
 * @test Tests relinking the templates of a map by folder.
 *
 * See the design notes, "Relink by folder".
 */
class TemplateRelinkTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString map_path;
	
	QString file(const QString& name) const
	{
		return QDir(dir.path()).absoluteFilePath(name);
	}
	
	/** Opens the map with the templates of the acceptance criteria, all but c missing. */
	void openMap(Map& map, QString* warnings = nullptr) const
	{
		QVERIFY(loadMap(map, map_path, warnings));
		QCOMPARE(map.getNumTemplates(), 4);
	}
	
private slots:
	void initTestCase();
	void relinkTest();
	void prefixTest();
	void savedTest();
	void startFolderTest();
};


void TemplateRelinkTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY(dir.isValid());
	
	// The files of a and b were moved from old/ to new/; other.png is lost; c is elsewhere.
	QVERIFY(writeImage(file(QStringLiteral("new/maps/a.png"))));
	QVERIFY(writeImage(file(QStringLiteral("new/maps/sub/b.png"))));
	QVERIFY(writeImage(file(QStringLiteral("elsewhere/c.png"))));
	map_path = file(QStringLiteral("work/event.foom"));
	QDir().mkpath(file(QStringLiteral("work")));
	
	Map map;
	for (auto const* name : { "old/maps/a.png", "old/maps/sub/b.png", "old/other.png", "elsewhere/c.png" })
		map.addTemplate(-1, std::make_unique<TemplateImage>(file(QString::fromLatin1(name)), &map));
	QVERIFY(saveMap(map, map_path));
}


void TemplateRelinkTest::relinkTest()
{
	Map map;
	QString warnings;
	openMap(map, &warnings);
	QVERIFY2(warnings.contains(QLatin1String("Relink by folder")), qPrintable(warnings));
	QCOMPARE(missingTemplateCount(map), 3);
	QCOMPARE(QDir::cleanPath(missingTemplatesFolder(map)), QDir::cleanPath(file(QStringLiteral("old"))));
	
	auto const report = relinkTemplates(map, file(QStringLiteral("old")), file(QStringLiteral("new")));
	QCOMPARE(report.relinked.size(), 2);
	QCOMPARE(report.not_found, QStringList{ QDir::cleanPath(file(QStringLiteral("old/other.png"))) });
	QVERIFY(report.failed.isEmpty());
	QVERIFY(map.areTemplatesDirty());
	
	auto const* a = map.getTemplate(indexOf(map, QStringLiteral("a.png")));
	QCOMPARE(QDir::cleanPath(a->getTemplatePath()), QDir::cleanPath(file(QStringLiteral("new/maps/a.png"))));
	QCOMPARE(a->getTemplateState(), Template::Loaded);
	auto const* b = map.getTemplate(indexOf(map, QStringLiteral("b.png")));
	QCOMPARE(QDir::cleanPath(b->getTemplatePath()), QDir::cleanPath(file(QStringLiteral("new/maps/sub/b.png"))));
	QCOMPARE(b->getTemplateState(), Template::Loaded);
	auto const* other = map.getTemplate(indexOf(map, QStringLiteral("other.png")));
	QCOMPARE(QDir::cleanPath(other->getTemplatePath()), QDir::cleanPath(file(QStringLiteral("old/other.png"))));
	QCOMPARE(other->getTemplateState(), Template::Invalid);
	auto const* c = map.getTemplate(indexOf(map, QStringLiteral("c.png")));
	QCOMPARE(QDir::cleanPath(c->getTemplatePath()), QDir::cleanPath(file(QStringLiteral("elsewhere/c.png"))));
	
	QCOMPARE(missingTemplateCount(map), 1);
}


void TemplateRelinkTest::prefixTest()
{
	// A trailing separator, native separators and, on Windows, another letter case give the same.
	{
		Map map;
		openMap(map);
		QString old_folder = QDir::toNativeSeparators(file(QStringLiteral("old"))) + QDir::separator();
#ifdef Q_OS_WIN
		old_folder = old_folder.toUpper();
#endif
		QCOMPARE(relinkTemplates(map, old_folder, file(QStringLiteral("new/"))).relinked.size(), 2);
	}
	// A prefix which ends inside a folder name relinks nothing.
	{
		Map map;
		openMap(map);
		auto const report = relinkTemplates(map, file(QStringLiteral("old/ma")), file(QStringLiteral("new")));
		QVERIFY(report.relinked.isEmpty());
		QVERIFY(!map.areTemplatesDirty());
	}
	// One folder deeper on both sides.
	{
		Map map;
		openMap(map);
		auto const report = relinkTemplates(map, file(QStringLiteral("old/maps/sub")), file(QStringLiteral("new/maps/sub")));
		QCOMPARE(report.relinked.size(), 1);
		QCOMPARE(map.getTemplate(indexOf(map, QStringLiteral("b.png")))->getTemplateState(), Template::Loaded);
		QCOMPARE(map.getTemplate(indexOf(map, QStringLiteral("a.png")))->getTemplateState(), Template::Invalid);
	}
}


void TemplateRelinkTest::savedTest()
{
	{
		Map map;
		openMap(map);
		QCOMPARE(relinkTemplates(map, file(QStringLiteral("old")), file(QStringLiteral("new"))).relinked.size(), 2);
		QVERIFY(saveMap(map, map_path));
	}
	Map map;
	QString warnings;
	openMap(map, &warnings);
	QVERIFY2(!warnings.contains(QLatin1String("Relink by folder")), qPrintable(warnings));
	QCOMPARE(missingTemplateCount(map), 1);
	QCOMPARE(QDir::cleanPath(map.getTemplate(indexOf(map, QStringLiteral("a.png")))->getTemplatePath()),
	         QDir::cleanPath(file(QStringLiteral("new/maps/a.png"))));
}


/// Accept (5)
void TemplateRelinkTest::startFolderTest()
{
	QCOMPARE(QDir::cleanPath(relinkStartFolder(file(QStringLiteral("old/maps")), map_path)), QDir::cleanPath(dir.path()));
	QCOMPARE(QDir::cleanPath(relinkStartFolder(file(QStringLiteral("new/maps/gone")), map_path)), QDir::cleanPath(file(QStringLiteral("new/maps"))));
	QCOMPARE(QDir::cleanPath(relinkStartFolder(QString(), map_path)), QDir::cleanPath(file(QStringLiteral("work"))));
}


QTEST_MAIN(TemplateRelinkTest)
#include "template_relink_t.moc"  // IWYU pragma: keep
