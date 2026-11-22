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

#include <QtTest>
#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/guides.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/map/view_aids.h"
#include "tools/tool.h"
#include "tools/tool_helpers.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

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

/** A map editor in a shown window of a known size. */
struct Editor
{
	MainWindow* window;
	MapEditorController* editor;
	MapWidget* map_widget;
	ViewAids* aids;

	explicit Editor(Map* map)
	{
		window = new MainWindow();
		editor = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(editor);
		map_widget = editor->getMainWidget();
		aids = editor->viewAids();
		window->setAttribute(Qt::WA_DontShowOnScreen);  // shown for its geometry, but never on the screen
		window->resize(900, 700);
		window->show();
		QCoreApplication::processEvents();
		map_widget->getMapView()->setCenter(MapCoord(0, 0));
		map_widget->getMapView()->setZoom(1.0);
	}

	~Editor()
	{
		window->deleteLater();
	}

	/** The pixel of a map position in the map widget. */
	QPoint pixel(double x_mm, double y_mm) const
	{
		return map_widget->mapToViewport(QPointF(x_mm, y_mm)).toPoint();
	}

	QMenu* viewMenu() const
	{
		for (auto* action : window->menuBar()->actions())
		{
			if (action->menu() && action->menu()->actions().contains(editor->getAction("view-back")))
				return action->menu();
		}
		return nullptr;
	}
};

bool menuContains(const QMenu* menu, const QAction* action)
{
	if (!menu || !action)
		return false;
	for (auto* entry : menu->actions())
	{
		if (entry == action || (entry->menu() && menuContains(entry->menu(), action)))
			return true;
	}
	return false;
}

}  // namespace


/**
 * Tests the view aids: rulers and guides, find selected, move to coordinates.
 *
 * See the design notes.
 */
class ViewAidsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("FOOM-Tests"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
	}


	// ### Rulers and guides ###

	void rulerStep()
	{
		// Labelled ticks at 1, 2 or 5 times a power of ten, at least 60 pixels apart.
		QCOMPARE(OpenOrienteering::rulerStep(60.0), 1.0);
		QCOMPARE(OpenOrienteering::rulerStep(59.0), 2.0);
		QCOMPARE(OpenOrienteering::rulerStep(30.0), 2.0);
		QCOMPARE(OpenOrienteering::rulerStep(29.0), 5.0);
		QCOMPARE(OpenOrienteering::rulerStep(12.0), 5.0);
		QCOMPARE(OpenOrienteering::rulerStep(11.0), 10.0);
		QCOMPARE(OpenOrienteering::rulerStep(600.0), 0.1);
		QCOMPARE(OpenOrienteering::rulerStep(0.06), 1000.0);
	}

	void rulerTicks()
	{
		// 10 pixels per unit: labels every 10 units (100 pixels), minor ticks every unit (10 pixels).
		auto const ticks = OpenOrienteering::rulerTicks(-5.0, 0.1, 200);
		QVERIFY(!ticks.empty());
		QCOMPARE(ticks.front().value, -5.0);
		QCOMPARE(ticks.front().pixel, 0.0);
		QCOMPARE(ticks.back().value, 15.0);
		QCOMPARE(ticks.back().pixel, 200.0);
		int labelled = 0;
		for (auto const& tick : ticks)
		{
			if (tick.labelled)
			{
				++labelled;
				QVERIFY(qFuzzyIsNull(std::fmod(tick.value, 10.0)));
			}
		}
		QCOMPARE(labelled, 2);  // 0 and 10
		QCOMPARE(ticks.size(), std::size_t(21));  // every unit from -5 to 15

		// A ruler whose values fall along it (y up): ticks still run along the pixels.
		auto const down = OpenOrienteering::rulerTicks(10.0, -0.1, 100);
		QCOMPARE(down.front().value, 10.0);
		QCOMPARE(down.front().pixel, 0.0);
		QCOMPARE(down.back().value, 0.0);
		QCOMPARE(down.back().pixel, 100.0);
	}

	void guidesXml()
	{
		Guides guides;
		guides.list = { { Qt::Horizontal, 12000 }, { Qt::Vertical, -3500 } };
		guides.locked = true;
		auto const xml = guidesToXml(guides);
		QVERIFY(xml.contains("orientation=\"vertical\""));
		QVERIFY(xml.contains("position=\"-3500\""));
		QCOMPARE(guidesFromXml(xml), guides);
		QVERIFY(Guides().isDefault());
		QVERIFY(!guides.isDefault());
	}

	void guidesFileRoundTrip()
	{
		QTemporaryDir dir;
		Map map;
		Guides guides;
		guides.list = { { Qt::Horizontal, 12000 }, { Qt::Vertical, -3500 } };
		guides.visible = false;
		map.setGuides(guides);
		auto const path = dir.filePath(QStringLiteral("guides.foom"));
		QVERIFY(saveMap(map, path));
		Map loaded;
		QVERIFY(loadMap(loaded, path));
		QCOMPARE(loaded.guides(), guides);

		// A map without guides writes no element, and reads the defaults.
		Map plain;
		auto const plain_path = dir.filePath(QStringLiteral("plain.foom"));
		QVERIFY(saveMap(plain, plain_path));
		QFile file(plain_path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(!file.readAll().contains("<guides"));
		Map plain_loaded;
		QVERIFY(loadMap(plain_loaded, plain_path));
		QVERIFY(plain_loaded.guides().isDefault());
	}

	void snapToGuides()
	{
		std::vector<Guide> guides = { { Qt::Horizontal, 10000 }, { Qt::Vertical, 20000 } };
		MapCoordF out;
		// Near the horizontal guide only: onto the guide, x kept.
		QVERIFY(OpenOrienteering::snapToGuides(guides, MapCoordF(5, 10.4), 0.5, out));
		QCOMPARE(out, MapCoordF(5, 10));
		// Near both: the crossing.
		QVERIFY(OpenOrienteering::snapToGuides(guides, MapCoordF(19.7, 10.3), 0.5, out));
		QCOMPARE(out, MapCoordF(20, 10));
		// Near none: false, out unchanged.
		out = MapCoordF(-1, -1);
		QVERIFY(!OpenOrienteering::snapToGuides(guides, MapCoordF(15, 15), 0.5, out));
		QCOMPARE(out, MapCoordF(-1, -1));

		QCOMPARE(guideAt(guides, MapCoordF(19.8, 50), 0.5), 1);
		QCOMPARE(guideAt(guides, MapCoordF(0, 0), 0.5), -1);
	}

	void snappingHelperUsesGuides()
	{
		auto* map_ptr = new Map();
		auto& map = *map_ptr;
		Editor editor(map_ptr);
		Guides guides;
		guides.list = { { Qt::Vertical, 20000 } };
		map.setGuides(guides);
		auto* tool = editor.editor->getTool();
		QVERIFY(tool);
		SnappingToolHelper helper(tool, SnappingToolHelper::ObjectCorners);

		// Within a pixel of the guide at zoom 1: onto the guide.
		auto const near = editor.map_widget->viewportToMapF(editor.pixel(20, 5) + QPoint(2, 0));
		SnappingToolHelperSnapInfo info;
		auto const snapped = helper.snapToObject(near, editor.map_widget, &info);
		QCOMPARE(info.type, SnappingToolHelper::GuideLines);
		QCOMPARE(snapped.nativeX(), 20000);

		// Hidden guides are not snapped onto, nor is anything without a filter.
		guides.visible = false;
		map.setGuides(guides);
		helper.snapToObject(near, editor.map_widget, &info);
		QCOMPARE(info.type, SnappingToolHelper::NoSnapping);
		guides.visible = true;
		map.setGuides(guides);
		helper.setFilter(SnappingToolHelper::NoSnapping);
		helper.snapToObject(near, editor.map_widget, &info);
		QCOMPARE(info.type, SnappingToolHelper::NoSnapping);
	}

	void rulersAndGuidesInEditor()
	{
		auto* map_ptr = new Map();
		auto& map = *map_ptr;
		Editor editor(map_ptr);
		auto* aids = editor.aids;
		QVERIFY(aids);
		for (auto const* id : { "view-rulers", "guides-show", "guides-lock", "guides-clear" })
		{
			QVERIFY2(editor.editor->getAction(id), id);
			QVERIFY2(menuContains(editor.viewMenu(), editor.editor->getAction(id)), id);
		}

		// The rulers are hidden at first and shown by the action, which is remembered.
		auto* top = aids->ruler(Qt::Horizontal);
		auto* left = aids->ruler(Qt::Vertical);
		QVERIFY(top && left);
		QVERIFY(!top->isVisible());
		editor.editor->getAction("view-rulers")->trigger();
		QVERIFY(top->isVisible() && left->isVisible());
		QVERIFY(QSettings().value(QStringLiteral("ViewAids/RulersVisible")).toBool());
		QCOMPARE(top->height(), RulerWidget::thickness);

		// The top ruler shows map x from the origin in mm, the left one y upwards.
		QVERIFY(qFuzzyIsNull(aids->rulerValue(Qt::Horizontal, MapCoordF(0, 0))));
		QCOMPARE(aids->rulerValue(Qt::Horizontal, MapCoordF(12, 0)), 12.0);
		QCOMPARE(aids->rulerValue(Qt::Vertical, MapCoordF(0, 12)), -12.0);
		aids->setRulerOrigin(MapCoordF(10, 10));
		QCOMPARE(aids->rulerValue(Qt::Horizontal, MapCoordF(12, 0)), 2.0);
		QCOMPARE(aids->rulerValue(Qt::Vertical, MapCoordF(0, 0)), 10.0);
		map.setScaleDenominator(10000);
		aids->setRulerUnit(ViewAids::GroundMetres);
		QCOMPARE(aids->rulerValue(Qt::Horizontal, MapCoordF(12, 0)), 20.0);  // 2 mm at 1:10000
		aids->setRulerUnit(ViewAids::PaperMillimetres);
		aids->resetRulerOrigin();

		// The ticks of the shown ruler: the labelled tick at 0 lies at the pixel of x 0.
		auto const ticks = top->ticks();
		QVERIFY(!ticks.empty());
		auto const zero_pixel = editor.map_widget->mapTo(editor.window, editor.pixel(0, 0)).x() - top->mapTo(editor.window, QPoint(0, 0)).x();
		bool found = false;
		for (auto const& tick : ticks)
		{
			if (tick.labelled && qFuzzyIsNull(tick.value))
			{
				QVERIFY(std::abs(tick.pixel - zero_pixel) <= 1.0);
				found = true;
			}
		}
		QVERIFY(found);

		// Drag from the top ruler into the map: a horizontal guide where it is dropped.
		map.setHasUnsavedChanges(false);
		aids->beginGuideDrag(Qt::Horizontal);
		aids->updateGuideDrag(editor.pixel(0, 5));
		aids->finishGuideDrag(editor.pixel(0, 5));
		QCOMPARE(map.guides().list.size(), std::size_t(1));
		QCOMPARE(map.guides().list.front().orientation, Qt::Horizontal);
		QVERIFY(std::abs(map.guides().list.front().positionMM() - 5.0) < 0.3);
		QVERIFY(map.hasUnsavedChanges());
		QVERIFY(editor.editor->getAction("guides-clear")->isEnabled());

		// A drag which ends back on the ruler adds nothing.
		aids->beginGuideDrag(Qt::Vertical);
		aids->finishGuideDrag(QPoint(-5, 100));
		QCOMPARE(map.guides().list.size(), std::size_t(1));

		// One undo step per guide.
		map.undoManager().undo();
		QVERIFY(map.guides().list.empty());
		map.undoManager().redo();
		QCOMPARE(map.guides().list.size(), std::size_t(1));

		// With the edit tool, dragging the guide moves it; dragging it onto the ruler removes it.
		QCOMPARE(editor.editor->getTool()->toolType(), MapEditorTool::EditPoint);
		auto const on_guide = editor.pixel(30, map.guides().list.front().positionMM());
		QTest::mousePress(editor.map_widget, Qt::LeftButton, {}, on_guide);
		QVERIFY(aids->isDraggingGuide());
		QTest::mouseRelease(editor.map_widget, Qt::LeftButton, {}, editor.pixel(30, 15));
		QVERIFY(!aids->isDraggingGuide());
		QCOMPARE(map.guides().list.size(), std::size_t(1));
		QVERIFY2(std::abs(map.guides().list.front().positionMM() - 15.0) < 0.3, qPrintable(QString::number(map.guides().list.front().positionMM())));
		aids->beginGuideDrag(Qt::Horizontal, 0);
		aids->finishGuideDrag(QPoint(100, -5));
		QVERIFY(map.guides().list.empty());
		map.undoManager().undo();
		QCOMPARE(map.guides().list.size(), std::size_t(1));

		// Locked guides neither move nor are cleared, nor are new ones added.
		editor.editor->getAction("guides-lock")->trigger();
		QVERIFY(map.guides().locked);
		QVERIFY(!editor.editor->getAction("guides-clear")->isEnabled());
		aids->clearGuides();
		aids->beginGuideDrag(Qt::Vertical);
		aids->finishGuideDrag(editor.pixel(0, 0));
		QCOMPARE(map.guides().list.size(), std::size_t(1));
		editor.editor->getAction("guides-lock")->trigger();
		QVERIFY(!map.guides().locked);

		// Hiding is remembered with the map; clearing is one undo step.
		editor.editor->getAction("guides-show")->trigger();
		QVERIFY(!map.guides().visible);
		editor.editor->getAction("guides-show")->trigger();
		QVERIFY(map.guides().visible);
		editor.editor->getAction("guides-clear")->trigger();
		QVERIFY(map.guides().list.empty());
		map.undoManager().undo();
		QCOMPARE(map.guides().list.size(), std::size_t(1));

		editor.editor->getAction("view-rulers")->trigger();
		QVERIFY(!top->isVisible());
	}

private:
	QTemporaryDir settings_dir;
};


QTEST_MAIN(ViewAidsTest)
#include "view_aids_t.moc"
