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
#include <vector>

#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/background_extents.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/map/course_canvases_dialog.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "templates/template_map.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

bool saveMap(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

bool near(const QRectF& a, const QRectF& b)
{
	return std::abs(a.left() - b.left()) < 0.01 && std::abs(a.top() - b.top()) < 0.01
	       && std::abs(a.width() - b.width()) < 0.01 && std::abs(a.height() - b.height()) < 0.01;
}

QString rectText(const QRectF& r)
{
	return QStringLiteral("%1 %2 %3 %4").arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height());
}

Georeferencing utm13(unsigned int scale, const QPointF& projected_ref)
{
	Georeferencing georef;
	georef.setScaleDenominator(int(scale));
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(projected_ref, false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	return georef;
}

Extent extentOf(const QString& name, const QRectF& rect)
{
	Extent extent;
	extent.name = name;
	extent.rect_mm = rect;
	return extent;
}

}  // namespace



/**
 * @test Tests loading the named extents of background maps.
 *
 * See the design notes, "Canvases: load extent from background map".
 */
class BackgroundExtentsTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/** A course map at 1:10,000 with the local background map north.foom, moved by (5, -3) mm. */
	void fillLocalMap(Map& map) const
	{
		map.setScaleDenominator(10000);
		auto temp = std::make_unique<TemplateMap>(file("north.foom"), &map);
		temp->setTemplateX(5000);
		temp->setTemplateY(-3000);
		map.addTemplate(-1, std::move(temp));
	}
	
private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
		{
			Map north;
			north.setScaleDenominator(10000);
			north.setExtents({ extentOf(QStringLiteral("North"), QRectF(10, 20, 200, 150)) });
			QVERIFY(saveMap(north, file("north.foom")));
		}
		{
			Map south;
			south.setGeoreferencing(utm13(15000, QPointF(500000, 5000000)));
			south.setExtents({ extentOf(QStringLiteral("South"), QRectF(0, 0, 100, 100)) });
			QVERIFY(saveMap(south, file("south.foom")));
		}
		{
			Map empty;
			empty.setScaleDenominator(10000);
			QVERIFY(saveMap(empty, file("empty.foom")));
		}
	}
	
	/// Accept (1)
	void localTest()
	{
		Map map;
		fillLocalMap(map);
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("picture.png"), &map));
		map.addTemplate(-1, std::make_unique<TemplateMap>(file("empty.foom"), &map));
		map.addTemplate(-1, std::make_unique<TemplateMap>(file("missing.foom"), &map));
		auto const found = backgroundMapExtents(map);
		QCOMPARE(found.size(), std::size_t(1));
		QCOMPARE(found.front().extent.name, QStringLiteral("North"));
		QCOMPARE(found.front().background, QStringLiteral("north.foom"));
		QCOMPARE(backgroundExtentLabel(found.front()), QStringLiteral("North (Background map: north.foom)"));
		QVERIFY2(near(found.front().extent.rect_mm, QRectF(15, 17, 200, 150)), qPrintable(rectText(found.front().extent.rect_mm)));
	}
	
	/// Accept (2)
	void georeferencedTest()
	{
		Map map;
		map.setGeoreferencing(utm13(10000, QPointF(499000, 5001000)));
		map.addTemplate(-1, std::make_unique<TemplateMap>(file("south.foom"), &map));
		auto* temp = map.getTemplate(0);
		QVERIFY(temp->loadTemplateFile());
		temp->setTemplateState(Template::Loaded);
		QVERIFY(temp->trySetTemplateGeoreferenced(true, nullptr));
		QVERIFY(temp->isTemplateGeoreferenced());
		auto const found = backgroundMapExtents(map);
		QCOMPARE(found.size(), std::size_t(1));
		QVERIFY2(near(found.front().extent.rect_mm, QRectF(100, 100, 150, 150)), qPrintable(rectText(found.front().extent.rect_mm)));
	}
	
	/// Accept (3)
	void loadTest()
	{
		Map map;
		fillLocalMap(map);
		map.setExtents({ extentOf(QStringLiteral("Whole"), QRectF(0, 0, 10, 10)) });
		auto const found = backgroundMapExtents(map);
		QCOMPARE(found.size(), std::size_t(1));
		auto const undo_steps = map.undoManager().undoStepCount();
		QVERIFY(loadExtentFromBackgroundMap(map, found.front()));
		QCOMPARE(map.extents().size(), std::size_t(2));
		QCOMPARE(map.extents().back().name, QStringLiteral("North"));
		QVERIFY(near(map.extents().back().rect_mm, QRectF(15, 17, 200, 150)));
		QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 1);
		// Again: the extent of that name is replaced, not doubled.
		QVERIFY(loadExtentFromBackgroundMap(map, found.front()));
		QCOMPARE(map.extents().size(), std::size_t(2));
		QVERIFY(map.undoManager().undo());
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.extents().size(), std::size_t(1));
	}
	
	/// Accept (4)
	void dialogTest()
	{
		Map map;
		fillLocalMap(map);
		CourseCanvasesDialog dialog(&map);
		auto* load_box = dialog.findChild<QComboBox*>(QStringLiteral("canvas-load-extent"));
		auto* extent_box = dialog.findChild<QComboBox*>(QStringLiteral("canvas-extent"));
		QVERIFY(load_box && extent_box);
		QCOMPARE(load_box->count(), 2);
		QCOMPARE(load_box->itemText(1), QStringLiteral("North (Background map: north.foom)"));
		QVERIFY(dialog.addCanvas(QStringLiteral("Canvas 1")));
		load_box->setCurrentIndex(1);
		QCOMPARE(extent_box->currentText(), QStringLiteral("North"));
		QCOMPARE(dialog.canvases().canvases.front().extent, QStringLiteral("North"));
		QCOMPARE(load_box->currentIndex(), 0);
		QCOMPARE(dialog.loadedExtents().size(), std::size_t(1));
		QVERIFY(near(dialog.loadedExtents().front().rect_mm, QRectF(15, 17, 200, 150)));
		// The map is not changed before the dialog is accepted.
		QVERIFY(map.extents().empty());
	}
};


QTEST_MAIN(BackgroundExtentsTest)
#include "background_extents_t.moc"  // IWYU pragma: keep
