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

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "check/map_check.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/line_text.h"
#include "core/selection_queries.h"
#include "core/symbols/path_text_layout.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "gui/main_window.h"
#include "gui/map/lengthen_line_text_dialog.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/symbols/symbol_setting_dialog.h"
#include "gui/widgets/symbol_widget.h"
#include "tools/draw_line_text_tool.h"
#include "tools/draw_text_tool.h"
#include "tools/tool.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


/**
 * Tests the editing of text on a path: the symbol editor, the drawing tool
 * and lengthen-line-text.
 *
 * See the design notes, "Text on a path".
 */
class LineTextTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void symbolEditor();
	void symbolEditorPreview();
	void drawTool();
	void lengthenOp();
	void lengthenEditor();
	void lengthenDialog();
	void selectTooShort();

private:
	static TextSymbol* makeSymbol(Map& map, bool follows_path, int alignment);
	static TextObject* makePathText(const TextSymbol* symbol, const QString& text, const MapCoordVector& path);
};


namespace {

MapColor* addBlack(Map& map)
{
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setOpacity(1.0f);
	map.addColor(black, 0);
	return black;
}

bool near(const MapCoord& c, double x, double y, double tolerance)
{
	return std::abs(c.x() - x) < tolerance && std::abs(c.y() - y) < tolerance;
}

/// A map editor in a main window, as in tools_t.
struct Editor
{
	MainWindow* window;
	MapEditorController* editor;
	MapWidget* widget;

	explicit Editor(Map* map)
	{
		window = new MainWindow();
		editor = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(editor);
		widget = editor->getMainWidget();
	}
	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;
	~Editor()
	{
		window->deleteLater();
	}

	QPoint viewport(double x, double y) const
	{
		return widget->mapToViewport(MapCoord(x, y)).toPoint();
	}

	void move(double x, double y) const
	{
		auto const pos = viewport(x, y);
		QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(widget, &event);
	}

	void click(double x, double y) const
	{
		move(x, y);
		QTest::mouseClick(widget, Qt::LeftButton, {}, viewport(x, y));
	}
};

}  // namespace


void LineTextTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	static QTemporaryDir settings_dir;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
}

TextSymbol* LineTextTest::makeSymbol(Map& map, bool follows_path, int alignment)
{
	auto* symbol = new TextSymbol();
	symbol->setColor(map.getColor(0));
	symbol->setFontFamily(QStringLiteral("Arial"));
	symbol->setFontSize(qRound(12.0 / 72.0 * 25.4 * 1000.0));  // 12 pt in 1/1000 mm
	symbol->setFollowsPath(follows_path);
	symbol->setPathAlignment(alignment);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

TextObject* LineTextTest::makePathText(const TextSymbol* symbol, const QString& text, const MapCoordVector& path)
{
	auto* object = new TextObject(symbol);
	object->setText(text);
	object->setAnchorPath(path);
	return object;
}


void LineTextTest::symbolEditor()
{
	// the design notes, "Line text in the symbol editor", Accept.
	Map map;
	addBlack(map);
	auto* plain = makeSymbol(map, false, 0);
	{
		SymbolSettingDialog dialog(plain, &map, nullptr);
		auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("follows_path_check"));
		auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("path_alignment_combo"));
		QVERIFY(check);
		QVERIFY(combo);
		QCOMPARE(combo->count(), 12);
		QVERIFY(!check->isChecked());
		QVERIFY(!combo->isEnabled());
		QVERIFY(!dialog.isSymbolModified());

		check->click();
		QVERIFY(combo->isEnabled());
		QVERIFY(dialog.isSymbolModified());
		auto edited = dialog.getNewSymbol();
		QVERIFY(edited->getType() == Symbol::Text);
		QVERIFY(static_cast<const TextSymbol*>(edited.get())->followsPath());

		auto* preview = dialog.getPreviewMap();
		const TextObject* preview_text = nullptr;
		preview->getCurrentPart()->applyOnAllObjects([&preview_text](const Object* object) {
			if (object->getType() == Object::Text)
				preview_text = object->asText();
		});
		QVERIFY(preview_text);
		QVERIFY(preview_text->getRawCoordinateVector().size() > 1);

		auto const index = combo->findData(5);
		QVERIFY(index >= 0);
		combo->setCurrentIndex(index);
		edited = dialog.getNewSymbol();
		QCOMPARE(static_cast<const TextSymbol*>(edited.get())->getPathAlignment(), 5);
	}

	auto* along = makeSymbol(map, true, 10);
	{
		SymbolSettingDialog dialog(along, &map, nullptr);
		auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("follows_path_check"));
		auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("path_alignment_combo"));
		QVERIFY(check && combo);
		QVERIFY(check->isChecked());
		QVERIFY(combo->isEnabled());
		QCOMPARE(combo->currentData().toInt(), 10);
	}

	// The icon shows the icon text (the default one, "A").
	along->resetIcon();
	auto const icon = along->createIcon(map, 64);
	auto dark = 0;
	for (int y = 0; y < icon.height(); ++y)
	{
		for (int x = 0; x < icon.width(); ++x)
		{
			auto const pixel = icon.pixel(x, y);
			if (qAlpha(pixel) > 128 && qGray(pixel) < 128)
				++dark;
		}
	}
	QVERIFY2(dark >= 20, qPrintable(QString::number(dark)));

	// A single-anchored object of a follows-path symbol is drawn as ordinary text.
	map.setScaleDenominator(10000);
	auto* single = new TextObject(along);
	single->setAnchorPosition(MapCoord(0.0, 0.0));
	single->setText(QStringLiteral("Abc"));
	map.addObject(single);
	single->update();
	QVERIFY(single->getExtent().isValid());
	QVERIFY(single->getExtent().width() > 1.0);
}


void LineTextTest::symbolEditorPreview()
{
	// the design notes, "Line text in the symbol editor", Accept:
	// the preview arc holds the whole text and the preview shows the whole arc.
	Map map;
	addBlack(map);
	auto const check_preview = [](SymbolSettingDialog& dialog) {
		const TextObject* preview_text = nullptr;
		dialog.getPreviewMap()->getCurrentPart()->applyOnAllObjects([&preview_text](const Object* object) {
			if (object->getType() == Object::Text)
				preview_text = object->asText();
		});
		QVERIFY(preview_text);
		QVERIFY(preview_text->getRawCoordinateVector().size() > 1);
		auto const* symbol = preview_text->getSymbol()->asText();
		QVERIFY(symbol->followsPath());
		QVERIFY(!symbol->layoutPathText(preview_text).overflow);

		// The preview pane is at least 300 pixels wide, centred on the origin.
		auto const* view = dialog.getPreviewController()->getMainWidget()->getMapView();
		auto const extent = preview_text->getExtent();
		QVERIFY(extent.isValid());
		auto const left = view->mapToView(extent.topLeft()).x();
		auto const right = view->mapToView(extent.bottomRight()).x();
		QVERIFY2(left >= -150.0 && right <= 150.0, qPrintable(QString::fromLatin1("%1 %2").arg(left).arg(right)));
	};

	for (auto const points : { 12.0, 40.0 })
	{
		auto* symbol = makeSymbol(map, true, 0);
		symbol->setFontSize(qRound(points / 72.0 * 25.4 * 1000.0));
		SymbolSettingDialog dialog(symbol, &map, nullptr);
		check_preview(dialog);
		if (QTest::currentTestFailed())
			return;
	}

	auto* plain = makeSymbol(map, false, 0);
	plain->setFontSize(qRound(40.0 / 72.0 * 25.4 * 1000.0));
	SymbolSettingDialog dialog(plain, &map, nullptr);
	auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("follows_path_check"));
	QVERIFY(check);
	check->click();
	check_preview(dialog);
}


void LineTextTest::drawTool()
{
	// the design notes, "Drawing line text", Accept.
	auto* map = new Map();
	addBlack(*map);
	auto* plain = makeSymbol(*map, false, 0);
	auto* along = makeSymbol(*map, true, 0);
	Editor editor(map);
	auto* symbols = editor.editor->getSymbolWidget();

	symbols->selectSingleSymbol(along);
	auto* action = editor.editor->getAction("drawtext");
	QVERIFY(action);
	QVERIFY(action->isEnabled());
	action->trigger();
	QVERIFY(qobject_cast<DrawLineTextTool*>(editor.editor->getTool()));

	auto* part = map->getCurrentPart();
	auto const objects_before = part->getNumObjects();
	auto const* status = editor.window->statusBar()->findChild<QLabel*>();
	QVERIFY(status);
	// A letter typed while the path is drawn is no shortcut (seen on 2026-11-22: M opened Measure).
	auto const overridden = [&editor](int key, Qt::KeyboardModifiers modifiers, const QString& text) {
		QKeyEvent event(QEvent::ShortcutOverride, key, modifiers, text);
		event.ignore();
		QApplication::sendEvent(editor.widget, &event);
		return event.isAccepted();
	};
	QVERIFY(!overridden(Qt::Key_M, Qt::NoModifier, QStringLiteral("m")));
	editor.click(10.0, 10.0);
	QVERIFY(overridden(Qt::Key_M, Qt::NoModifier, QStringLiteral("m")));
	QVERIFY(overridden(Qt::Key_M, Qt::ShiftModifier, QStringLiteral("M")));
	QVERIFY(!overridden(Qt::Key_Z, Qt::ControlModifier, QString()));
	// Return finishes the path open, and the hint says so (screen check 2026-11-21).
	QVERIFY2(status->text().contains(QLatin1String("Finish the path")), qPrintable(status->text()));
	QVERIFY2(!status->text().contains(QLatin1String("Close the path")), qPrintable(status->text()));
	editor.click(30.0, 10.0);
	editor.click(50.0, 10.0);
	QTest::keyClick(editor.widget, Qt::Key_Return);
	QCOMPARE(part->getNumObjects(), objects_before);
	QTest::keyClicks(editor.widget, QStringLiteral("Abc"));
	QTest::keyClick(editor.widget, Qt::Key_Return);
	QCOMPARE(part->getNumObjects(), objects_before + 1);
	auto const* object = part->getObject(part->getNumObjects() - 1);
	QVERIFY(object->getType() == Object::Text);
	QCOMPARE(object->getSymbol(), static_cast<const Symbol*>(along));
	QCOMPARE(object->asText()->getText(), QStringLiteral("Abc"));
	auto const& coords = object->getRawCoordinateVector();
	QCOMPARE(coords.size(), MapCoordVector::size_type(3));
	QVERIFY(near(coords[0], 10.0, 10.0, 0.2));
	QVERIFY(near(coords[1], 30.0, 10.0, 0.2));
	QVERIFY(near(coords[2], 50.0, 10.0, 0.2));
	QCOMPARE(map->getNumSelectedObjects(), 1);
	QVERIFY(map->isObjectSelected(object));
	QVERIFY(map->undoManager().undo());
	QCOMPARE(part->getNumObjects(), objects_before);

	// No text, no object; Ctrl+Escape cancels.
	editor.click(10.0, 20.0);
	editor.click(30.0, 20.0);
	QTest::keyClick(editor.widget, Qt::Key_Return);
	QTest::keyClick(editor.widget, Qt::Key_Escape);
	QCOMPARE(part->getNumObjects(), objects_before);
	editor.click(10.0, 30.0);
	editor.click(30.0, 30.0);
	QTest::keyClick(editor.widget, Qt::Key_Return);
	QTest::keyClicks(editor.widget, QStringLiteral("Xyz"));
	QTest::keyClick(editor.widget, Qt::Key_Escape, Qt::ControlModifier);
	QCOMPARE(part->getNumObjects(), objects_before);
	QVERIFY(qobject_cast<DrawLineTextTool*>(editor.editor->getTool()));

	// The tool follows the symbol.
	symbols->selectSingleSymbol(plain);
	QVERIFY(qobject_cast<DrawTextTool*>(editor.editor->getTool()));
	symbols->selectSingleSymbol(along);
	QVERIFY(qobject_cast<DrawLineTextTool*>(editor.editor->getTool()));

	symbols->selectSingleSymbol(plain);
	map->clearObjectSelection(true);
	editor.editor->setEditTool();
	symbols->selectSingleSymbol(along);
	QVERIFY(qobject_cast<DrawLineTextTool*>(editor.editor->getTool()));
	editor.editor->setTool(nullptr);
}


void LineTextTest::lengthenOp()
{
	// the design notes, "Lengthen line text", Accept.
	Map map;
	addBlack(map);
	auto const text = QStringLiteral("Path text 123");
	auto const straight = MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) };
	auto const lengthen = [&map, &text](int alignment, const MapCoordVector& path, const ops::LengthenLineTextParams& params) {
		auto* symbol = makeSymbol(map, true, alignment);
		auto* object = makePathText(symbol, text, path);
		map.addObject(object);
		return ops::lengthenLineText(map, { object }, params);
	};

	ops::LengthenLineTextParams by_distance;
	by_distance.distance = 5.0;
	struct Case { int alignment; double x0; double x1; };
	for (auto const& c : { Case{0, 0.0, 25.0}, Case{2, -5.0, 20.0}, Case{1, -2.5, 22.5}, Case{3, -2.5, 22.5}, Case{8, 0.0, 25.0} })
	{
		auto result = lengthen(c.alignment, straight, by_distance);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(result.out.size(), std::size_t(1));
		QCOMPARE(result.consumed.size(), std::size_t(1));
		auto const* out = result.out.front().get();
		QVERIFY(out->getType() == Object::Text);
		QCOMPARE(out->uuid(), result.consumed.front()->uuid());
		QCOMPARE(out->asText()->getText(), text);
		auto const& coords = out->getRawCoordinateVector();
		QCOMPARE(coords.size(), MapCoordVector::size_type(2));
		QVERIFY2(near(coords[0], c.x0, 0.0, 0.001), qPrintable(QString::number(c.alignment)));
		QVERIFY2(near(coords[1], c.x1, 0.0, 0.001), qPrintable(QString::number(c.alignment)));
	}

	ops::LengthenLineTextParams by_percent;
	by_percent.by_percent = true;
	by_percent.percent = 50.0;
	{
		auto result = lengthen(0, straight, by_percent);
		QCOMPARE(result.out.size(), std::size_t(1));
		auto const& coords = result.out.front()->getRawCoordinateVector();
		QVERIFY(near(coords[1], 30.0, 0.0, 0.001));
	}

	// A curve keeps its coordinates and gains a straight segment.
	{
		auto c0 = MapCoord(0.0, 0.0);
		c0.setCurveStart(true);
		auto const curve = MapCoordVector{ c0, MapCoord(5.0, 5.0), MapCoord(15.0, 5.0), MapCoord(20.0, 0.0) };
		auto result = lengthen(0, curve, by_distance);
		QCOMPARE(result.out.size(), std::size_t(1));
		auto const& coords = result.out.front()->getRawCoordinateVector();
		QCOMPARE(coords.size(), MapCoordVector::size_type(5));
		for (int i = 0; i < 4; ++i)
			QCOMPARE(coords[std::size_t(i)], curve[std::size_t(i)]);
		QVERIFY(coords[0].isCurveStart());
		QVERIFY(!coords[3].isCurveStart());
		QVERIFY(near(coords[4], 23.536, -3.536, 0.001));
	}

	// Overflow goes away.
	{
		auto* symbol = makeSymbol(map, true, 0);
		auto* object = makePathText(symbol, text, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
		map.addObject(object);
		QVERIFY(symbol->layoutPathText(object).overflow);
		ops::LengthenLineTextParams params;
		params.distance = 60.0;
		auto result = ops::lengthenLineText(map, { object }, params);
		QCOMPARE(result.out.size(), std::size_t(1));
		QVERIFY(!symbol->layoutPathText(result.out.front()->asText()).overflow);
	}

	// Errors and closed paths.
	{
		auto* point_symbol = new PointSymbol();
		map.addSymbol(point_symbol, map.getNumSymbols());
		auto* point = new PointObject(point_symbol);
		map.addObject(point);
		auto result = ops::lengthenLineText(map, { point }, by_distance);
		QVERIFY(!result.error.isEmpty());

		ops::LengthenLineTextParams zero;
		zero.distance = 0.0;
		result = lengthen(0, straight, zero);
		QVERIFY(!result.error.isEmpty());
		QVERIFY(result.out.empty());

		auto end = MapCoord(0.0, 0.0);
		end.setClosePoint(true);
		auto const closed = MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(20.0, 0.0), MapCoord(20.0, 20.0), end };
		result = lengthen(0, closed, by_distance);
		QVERIFY(result.error.isEmpty());
		QVERIFY(result.out.empty());
		QVERIFY(!result.warning.isEmpty());
	}
}


void LineTextTest::lengthenEditor()
{
	// the design notes, "Lengthen line text", Accept (editor).
	auto* map = new Map();
	addBlack(*map);
	auto* along = makeSymbol(*map, true, 0);
	auto* object = makePathText(along, QStringLiteral("Abc"), { MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) });
	map->addObject(object);
	auto const uuid = object->uuid();
	Editor editor(map);

	auto* action = editor.editor->getAction("lengthen-line-text");
	QVERIFY(action);
	map->clearObjectSelection(true);
	QVERIFY(!action->isEnabled());
	map->addObjectToSelection(object, true);
	QVERIFY(action->isEnabled());

	auto const undo_steps = map->undoManager().undoStepCount();
	ops::LengthenLineTextParams params;
	params.distance = 5.0;
	editor.editor->lengthenLineText(params);
	QCOMPARE(map->undoManager().undoStepCount(), undo_steps + 1);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	auto const* lengthened = map->getFirstSelectedObject();
	QCOMPARE(lengthened->uuid(), uuid);
	QVERIFY(near(lengthened->getRawCoordinateVector().back(), 25.0, 0.0, 0.001));

	QVERIFY(map->undoManager().undo());
	auto const* part = map->getCurrentPart();
	const Object* restored = nullptr;
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		if (part->getObject(i)->uuid() == uuid)
			restored = part->getObject(i);
	}
	QVERIFY(restored);
	QVERIFY(near(restored->getRawCoordinateVector().back(), 20.0, 0.0, 0.001));
	editor.editor->setTool(nullptr);
}


void LineTextTest::lengthenDialog()
{
	// The title is not cut off (screen check 2026-11-21): room for the title
	// and the title bar buttons.
	LengthenLineTextDialog dialog(ops::LengthenLineTextParams{}, nullptr);
	dialog.adjustSize();
	QFontMetrics const metrics(dialog.font());
	auto const needed = metrics.boundingRect(dialog.windowTitle()).width() + 10 * metrics.height();
	QVERIFY2(dialog.width() >= needed, qPrintable(QString::fromLatin1("%1 < %2").arg(dialog.width()).arg(needed)));
}


void LineTextTest::selectTooShort()
{
	// the design notes, "Selecting line text whose line is too short", Accept.
	auto* map = new Map();
	addBlack(*map);
	auto* along = makeSymbol(*map, true, 0);
	auto* hidden = makeSymbol(*map, true, 0);
	hidden->setHidden(true);
	auto* plain = makeSymbol(*map, false, 0);
	auto const text = QStringLiteral("Path text 123");
	auto* too_short = makePathText(along, text, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map->addObject(too_short);
	auto* long_enough = makePathText(along, text, { MapCoord(0.0, 20.0), MapCoord(80.0, 20.0) });
	map->addObject(long_enough);
	map->addObject(makePathText(hidden, text, { MapCoord(0.0, 40.0), MapCoord(10.0, 40.0) }));
	auto* ordinary = new TextObject(plain);
	ordinary->setText(text);
	ordinary->setAnchorPosition(MapCoord(0.0, 60.0));
	map->addObject(ordinary);
	auto* empty = makePathText(along, QString{}, { MapCoord(0.0, 80.0), MapCoord(1.0, 80.0) });
	map->addObject(empty);
	// A line text which once followed a path but has a single anchor now.
	auto* single = new TextObject(along);
	single->setText(text);
	single->setAnchorPosition(MapCoord(0.0, 100.0));
	map->addObject(single);

	auto const found = findLineTextTooShort(*map->getCurrentPart());
	QCOMPARE(found.size(), std::size_t(1));
	QCOMPARE(found.front(), static_cast<Object*>(too_short));
	// The map check reports the same line text, and not the one with a single anchor.
	auto overflowing = 0;
	for (auto const& issue : checkMap(*map))
	{
		if (issue.rule == rule::text_path_overflow)
		{
			++overflowing;
			QVERIFY(!issue.objects.contains(single->uuid()));
		}
	}
	QCOMPARE(overflowing, 2);  // the too short one and the one of the hidden symbol

	// In the editor: the action, and the selection it makes.
	Editor editor(map);
	QVERIFY(editor.editor->getAction("select-line-text-too-short"));
	map->clearObjectSelection(true);
	QCOMPARE(editor.editor->selectLineTextTooShort(), 1);
	QCOMPARE(map->getNumSelectedObjects(), 1);
	QVERIFY(map->isObjectSelected(too_short));

	// Lengthened far enough, it is no longer found.
	ops::LengthenLineTextParams params;
	params.distance = 60.0;
	editor.editor->lengthenLineText(params);
	QCOMPARE(editor.editor->selectLineTextTooShort(), 0);
	QCOMPARE(map->getNumSelectedObjects(), 0);
}

QTEST_MAIN(LineTextTest)
#include "line_text_t.moc"  // IWYU pragma: keep
