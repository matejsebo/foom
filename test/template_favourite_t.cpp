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
#include "core/map_view.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"

using namespace OpenOrienteering;


namespace {

bool saveMap(const Map& map, const MapView* view, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, view);
	return exporter && exporter->doExport();
}

bool loadMap(Map& map, MapView* view, const QString& path)
{
	XMLFileFormat format;
	auto importer = format.makeImporter(path, &map, view);
	return importer && importer->doImport();
}

bool writeImage(const QString& path)
{
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(QColor(Qt::green));
	return image.save(path, "PNG");
}

}  // namespace



/**
 * @test Tests favourite templates and the view of the favourites only.
 *
 * See the design notes, "Favourite templates".
 */
class TemplateFavouriteTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/** A map with the loaded and visible templates a (favourite) and b. */
	void fillMap(Map& map, MapView& view) const
	{
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("a.png"), &map));
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("b.png"), &map));
		for (int i = 0; i < 2; ++i)
		{
			QVERIFY(map.getTemplate(i)->loadTemplateFile());
			view.setTemplateVisibility(map.getTemplate(i), { 1.0f, true });
		}
		map.getTemplate(0)->setFavourite(true);
	}
	
private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QVERIFY(writeImage(file("a.png")));
		QVERIFY(writeImage(file("b.png")));
	}
	
	/// Accept (1)
	void flagTest()
	{
		Map map;
		MapView view(&map);
		fillMap(map, view);
		auto* a = map.getTemplate(0);
		auto* b = map.getTemplate(1);
		QVERIFY(a->isFavourite());
		QVERIFY(!b->isFavourite());
		QVERIFY(map.areTemplatesDirty());
		
		auto copy = std::unique_ptr<Template>(a->duplicate());
		QVERIFY(copy->isFavourite());
		
		QVERIFY(saveMap(map, &view, file("favourite.foom")));
		QFile saved(file("favourite.foom"));
		QVERIFY(saved.open(QIODevice::ReadOnly));
		QCOMPARE(saved.readAll().count("favourite=\"true\""), 1);
		
		Map reloaded;
		MapView reloaded_view(&reloaded);
		QVERIFY(loadMap(reloaded, &reloaded_view, file("favourite.foom")));
		QCOMPARE(reloaded.getNumTemplates(), 2);
		QVERIFY(reloaded.getTemplate(0)->isFavourite());
		QVERIFY(!reloaded.getTemplate(1)->isFavourite());
	}
	
	/// Accept (2)
	void viewTest()
	{
		Map map;
		MapView view(&map);
		fillMap(map, view);
		auto* a = map.getTemplate(0);
		auto* b = map.getTemplate(1);
		QVERIFY(view.isTemplateVisible(a));
		QVERIFY(view.isTemplateVisible(b));
		QVERIFY(!view.areFavouriteTemplatesOnly());
		
		auto signals_count = 0;
		auto last_feature = MapView::MultipleFeatures;
		connect(&view, &MapView::visibilityChanged, this, [&signals_count, &last_feature](MapView::VisibilityFeature feature) {
			++signals_count;
			last_feature = feature;
		});
		view.setFavouriteTemplatesOnly(true);
		QCOMPARE(signals_count, 1);
		QCOMPARE(last_feature, MapView::FavouriteTemplatesOnly);
		QVERIFY(view.areFavouriteTemplatesOnly());
		QVERIFY(view.isTemplateVisible(a));
		QVERIFY(!view.isTemplateVisible(b));
		// The template's own visibility is kept.
		QVERIFY(view.getTemplateVisibility(b).visible);
		view.setFavouriteTemplatesOnly(true);
		QCOMPARE(signals_count, 1);
		
		// A template becomes a favourite while the view shows the favourites only.
		b->setFavourite(true);
		QVERIFY(view.isTemplateVisible(b));
		b->setFavourite(false);
		
		QVERIFY(saveMap(map, &view, file("view.foom")));
		Map reloaded;
		MapView reloaded_view(&reloaded);
		QVERIFY(loadMap(reloaded, &reloaded_view, file("view.foom")));
		QVERIFY(reloaded_view.areFavouriteTemplatesOnly());
		
		view.setFavouriteTemplatesOnly(false);
		QCOMPARE(signals_count, 2);
		QVERIFY(view.isTemplateVisible(b));
		
		QVERIFY(saveMap(map, &view, file("plain.foom")));
		QFile plain(file("plain.foom"));
		QVERIFY(plain.open(QIODevice::ReadOnly));
		QVERIFY(!plain.readAll().contains("favourites_only"));
	}
};


QTEST_MAIN(TemplateFavouriteTest)
#include "template_favourite_t.moc"  // IWYU pragma: keep
