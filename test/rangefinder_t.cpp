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
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QWidget>

#include "global.h"
#include "settings.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/rangefinder.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/rangefinder_widget.h"
#include "gui/widgets/symbol_widget.h"
#include "sensors/rangefinder_source.h"
#include "tools/rangefinder_tool.h"
#include "tools/tool.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

bool near(const MapCoordF& actual, double x, double y, double tolerance = 1e-6)
{
	return qAbs(actual.x() - x) <= tolerance && qAbs(actual.y() - y) <= tolerance;
}

QString shown(const MapCoordF& point)
{
	return QStringLiteral("(%1, %2)").arg(point.x(), 0, 'f', 6).arg(point.y(), 0, 'f', 6);
}

double distance(const MapCoordF& a, const MapCoordF& b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// Returns the sentence with its checksum and line end for the text between $ and *.
QByteArray sentence(const QByteArray& body)
{
	int checksum = 0;
	for (auto c : body)
		checksum ^= c;
	return QByteArray("$") + body + QByteArray("*") + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper() + QByteArray("\r\n");
}

RangefinderReading reading(double horizontal_distance, double azimuth)
{
	RangefinderReading result;
	result.horizontal_distance = horizontal_distance;
	result.azimuth = azimuth;
	return result;
}

}  // namespace



/**
 * @test Tests the rangefinder: readings, targets, traverses, the widget
 *       and the dock in the editor.
 *
 * the design notes, "Rangefinder dock: detail".
 */
class RangefinderTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void readingTest();
	void targetTest();
	void declinationTest();
	void traverseTest();
	void settingsTest();
	void widgetTest();
	void editorTest();
	void sentenceTest();
	void sourceTest();
	void deviceWidgetTest();
	void deviceEditorTest();
	
private:
	QTemporaryDir settings_dir;
};


void RangefinderTest::initTestCase()
{
	QVERIFY(settings_dir.isValid());
	// The settings of this test go to a file in a temporary folder.
	QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
	QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
	
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void RangefinderTest::readingTest()
{
	RangefinderReading empty;
	QVERIFY(!empty.isComplete());
	QVERIFY(qIsNaN(empty.horizontalDistance()));
	QVERIFY(qIsNaN(empty.heightDifference()));
	
	RangefinderReading slope;
	slope.slope_distance = 12.55;
	slope.inclination = 5.0;
	slope.azimuth = 45.0;
	QVERIFY(slope.isComplete());
	QVERIFY2(qAbs(slope.horizontalDistance() - 12.502) < 0.001, qPrintable(QString::number(slope.horizontalDistance(), 'f', 4)));
	QVERIFY2(qAbs(slope.heightDifference() - 1.094) < 0.001, qPrintable(QString::number(slope.heightDifference(), 'f', 4)));
	
	// A horizontal distance which is given counts.
	auto both = slope;
	both.horizontal_distance = 12.50;
	QCOMPARE(both.horizontalDistance(), 12.50);
	QVERIFY2(qAbs(both.heightDifference() - 1.094) < 0.001, qPrintable(QString::number(both.heightDifference(), 'f', 4)));
	
	auto horizontal = reading(12.50, 45.0);
	QVERIFY(horizontal.isComplete());
	QCOMPARE(horizontal.horizontalDistance(), 12.50);
	QVERIFY(qIsNaN(horizontal.heightDifference()));
	
	RangefinderReading slope_alone;
	slope_alone.slope_distance = 12.55;
	slope_alone.azimuth = 45.0;
	QVERIFY(!slope_alone.isComplete());
	
	RangefinderReading no_azimuth;
	no_azimuth.horizontal_distance = 12.50;
	QVERIFY(!no_azimuth.isComplete());
}


/// Accept (2)
void RangefinderTest::targetTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto const& georef = map.getGeoreferencing();
	
	RangefinderSurvey survey;
	QCOMPARE(survey.sight, RangefinderSurvey::ForwardSight);
	QCOMPARE(survey.traverse, RangefinderSurvey::LinearTraverse);
	QVERIFY(survey.azimuth_is_magnetic);
	survey.setReference(MapCoordF(0, 0));
	
	auto shot = reading(12.50, 45.0);
	shot.inclination = 5.0;
	MapCoordF target;
	QVERIFY(survey.target(shot, georef, &target));
	QVERIFY2(near(target, 0.88388, -0.88388, 1e-4), qPrintable(shown(target)));
	QVERIFY(qAbs(survey.heightDifference(shot) - 1.094) < 0.001);
	
	// Whether the azimuth is magnetic makes no difference without a declination.
	survey.azimuth_is_magnetic = false;
	QVERIFY(survey.target(shot, georef, &target));
	QVERIFY2(near(target, 0.88388, -0.88388, 1e-4), qPrintable(shown(target)));
	
	survey.sight = RangefinderSurvey::RearSight;
	QVERIFY(survey.target(shot, georef, &target));
	QVERIFY2(near(target, -0.88388, 0.88388, 1e-4), qPrintable(shown(target)));
	QVERIFY(qAbs(survey.heightDifference(shot) + 1.094) < 0.001);
	
	// A reference elsewhere.
	survey.sight = RangefinderSurvey::ForwardSight;
	survey.setReference(MapCoordF(10, 20));
	QVERIFY(survey.target(reading(100.0, 90.0), georef, &target));
	QVERIFY2(near(target, 20, 20), qPrintable(shown(target)));
	QVERIFY(survey.target(reading(100.0, 180.0), georef, &target));
	QVERIFY2(near(target, 10, 30), qPrintable(shown(target)));
}


/// Accept (3)
void RangefinderTest::declinationTest()
{
	Map map;
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setGeographicRefPoint(LatLon(50.0, -103.5));
	georef.setDeclination(3.0);
	map.setGeoreferencing(georef);
	auto const& map_georef = map.getGeoreferencing();
	QCOMPARE(map_georef.getState(), Georeferencing::Geospatial);
	QCOMPARE(map_georef.getDeclination(), 3.0);
	
	RangefinderSurvey survey;
	survey.setReference(MapCoordF(0, 0));
	MapCoordF target;
	
	// From true north.
	survey.azimuth_is_magnetic = false;
	QVERIFY(survey.target(reading(100.0, 0.0), map_georef, &target));
	auto ok = false;
	auto const reference_latlon = map_georef.toGeographicCoords(MapCoordF(0, 0), &ok);
	QVERIFY(ok);
	auto const target_latlon = map_georef.toGeographicCoords(target, &ok);
	QVERIFY(ok);
	// 100 m are 0.000899 degrees of latitude; 1 m to the side would be 0.000014 degrees of longitude.
	QVERIFY2(qAbs(target_latlon.longitude() - reference_latlon.longitude()) < 3e-6,
	         qPrintable(QString::number(target_latlon.longitude() - reference_latlon.longitude(), 'g', 6)));
	QVERIFY2(qAbs(target_latlon.latitude() - reference_latlon.latitude() - 0.000899) < 5e-6,
	         qPrintable(QString::number(target_latlon.latitude() - reference_latlon.latitude(), 'g', 6)));
	// The map's up direction is magnetic north: true north lies 3 degrees to its left.
	QVERIFY2(near(target, -10 * std::sin(qDegreesToRadians(3.0)), -10 * std::cos(qDegreesToRadians(3.0)), 1e-6), qPrintable(shown(target)));
	
	// From magnetic north.
	survey.azimuth_is_magnetic = true;
	QVERIFY(survey.target(reading(100.0, 0.0), map_georef, &target));
	QVERIFY2(near(target, 0, -10), qPrintable(shown(target)));
}


/// Accept (4)
void RangefinderTest::traverseTest()
{
	Map map;
	map.setScaleDenominator(10000);
	auto const& georef = map.getGeoreferencing();
	auto const d = 25.0;      // metres
	auto const d_mm = 2.5;    // on the map
	
	RangefinderSurvey survey;
	MapCoordF target;
	// Without a reference there is no target.
	QVERIFY(!survey.hasReference());
	QVERIFY(!survey.target(reading(d, 0.0), georef, &target));
	QVERIFY(!survey.take(reading(d, 0.0), georef, &target));
	QVERIFY(!survey.hasPreviousTarget());
	QVERIFY(!survey.usePreviousTargetAsReference());
	
	// Radial.
	survey.traverse = RangefinderSurvey::RadialTraverse;
	survey.setReference(MapCoordF(3, 4));
	std::vector<MapCoordF> targets;
	for (auto azimuth : { 0.0, 90.0, 180.0, 270.0 })
	{
		QVERIFY(survey.take(reading(d, azimuth), georef, &target));
		targets.push_back(target);
		QVERIFY(near(survey.reference(), 3, 4));
		QVERIFY(survey.hasPreviousTarget());
		QVERIFY(near(survey.previousTarget(), target.x(), target.y()));
	}
	for (std::size_t i = 0; i < targets.size(); ++i)
	{
		QVERIFY(qAbs(distance(targets[i], MapCoordF(3, 4)) - d_mm) < 1e-9);
		QVERIFY(qAbs(distance(targets[i], targets[(i + 1) % targets.size()]) - d_mm * std::sqrt(2.0)) < 1e-9);
	}
	QVERIFY2(near(targets[0], 3, 1.5), qPrintable(shown(targets[0])));
	QVERIFY2(near(targets[1], 5.5, 4), qPrintable(shown(targets[1])));
	// An incomplete reading is not taken.
	RangefinderReading incomplete;
	incomplete.azimuth = 10.0;
	QVERIFY(!survey.take(incomplete, georef, &target));
	QVERIFY(near(survey.previousTarget(), targets[3].x(), targets[3].y()));
	// The previous target as the reference.
	QVERIFY(survey.usePreviousTargetAsReference());
	QVERIFY(near(survey.reference(), targets[3].x(), targets[3].y()));
	
	// Linear.
	survey.traverse = RangefinderSurvey::LinearTraverse;
	survey.setReference(MapCoordF(3, 4));
	for (auto azimuth : { 0.0, 90.0, 180.0, 270.0 })
	{
		QVERIFY(survey.take(reading(d, azimuth), georef, &target));
		QVERIFY(near(survey.reference(), target.x(), target.y()));
	}
	QVERIFY2(near(survey.reference(), 3, 4), qPrintable(shown(survey.reference())));
	
	survey.clearReference();
	QVERIFY(!survey.hasReference());
	QVERIFY(!survey.target(reading(d, 0.0), georef, &target));
}


/// Accept (5)
void RangefinderTest::settingsTest()
{
	auto const path = settings_dir.filePath(QStringLiteral("rangefinder.ini"));
	{
		QSettings file(path, QSettings::IniFormat);
		auto const defaults = RangefinderSettings::load(file);
		QCOMPARE(defaults.sight, RangefinderSurvey::ForwardSight);
		QCOMPARE(defaults.traverse, RangefinderSurvey::LinearTraverse);
		QVERIFY(defaults.azimuth_is_magnetic);
		QVERIFY(!defaults.slope_distance);
		
		RangefinderSettings settings;
		settings.sight = RangefinderSurvey::RearSight;
		settings.traverse = RangefinderSurvey::RadialTraverse;
		settings.azimuth_is_magnetic = false;
		settings.slope_distance = true;
		settings.save(file);
	}
	{
		QSettings file(path, QSettings::IniFormat);
		auto const settings = RangefinderSettings::load(file);
		QCOMPARE(settings.sight, RangefinderSurvey::RearSight);
		QCOMPARE(settings.traverse, RangefinderSurvey::RadialTraverse);
		QVERIFY(!settings.azimuth_is_magnetic);
		QVERIFY(settings.slope_distance);
		
		file.setValue(QStringLiteral("Rangefinder/sight"), QStringLiteral("sideways"));
		QCOMPARE(RangefinderSettings::load(file).sight, RangefinderSurvey::ForwardSight);
	}
}


/// Accept (6)
void RangefinderTest::widgetTest()
{
	RangefinderSettings{}.save();
	Map map;
	map.setScaleDenominator(10000);
	
	RangefinderWidget widget(&map, false);
	auto* distance_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-distance"));
	auto* kind_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-distance-kind"));
	auto* inclination_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-inclination"));
	auto* azimuth_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-azimuth"));
	auto* magnetic_check = widget.findChild<QCheckBox*>(QStringLiteral("rangefinder-magnetic"));
	auto* sight_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-sight"));
	auto* traverse_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-traverse"));
	auto* reference_label = widget.findChild<QLabel*>(QStringLiteral("rangefinder-reference"));
	auto* target_label = widget.findChild<QLabel*>(QStringLiteral("rangefinder-target"));
	auto* height_label = widget.findChild<QLabel*>(QStringLiteral("rangefinder-height"));
	auto* add_button = widget.findChild<QAbstractButton*>(QStringLiteral("rangefinder-add"));
	auto* previous_button = widget.findChild<QAbstractButton*>(QStringLiteral("rangefinder-reference-previous"));
	QVERIFY(distance_box && kind_box && inclination_box && azimuth_box && magnetic_check && sight_box && traverse_box);
	QVERIFY(reference_label && target_label && height_label && add_button && previous_button);
	QVERIFY(!widget.findChild<QAbstractButton*>(QStringLiteral("rangefinder-close")));
	
	// The defaults.
	QVERIFY(magnetic_check->isChecked());
	QCOMPARE(sight_box->currentData().toInt(), int(RangefinderSurvey::ForwardSight));
	QCOMPARE(traverse_box->currentData().toInt(), int(RangefinderSurvey::LinearTraverse));
	QCOMPARE(kind_box->currentData().toBool(), false);
	QVERIFY(!widget.survey().hasReference());
	QVERIFY(!add_button->isEnabled());
	QVERIFY(!previous_button->isEnabled());
	
	auto changes = 0;
	connect(&widget, &RangefinderWidget::changed, this, [&changes]() { ++changes; });
	
	distance_box->setValue(12.50);
	azimuth_box->setValue(45.0);
	QVERIFY(!add_button->isEnabled());
	MapCoordF target;
	QVERIFY(!widget.previewTarget(&target));
	
	widget.setReference(MapCoordF(0, 0));
	QVERIFY(changes > 0);
	QVERIFY(widget.survey().hasReference());
	QVERIFY(add_button->isEnabled());
	QVERIFY(widget.previewTarget(&target));
	QVERIFY2(near(target, 0.88388, -0.88388, 1e-4), qPrintable(shown(target)));
	QCOMPARE(target_label->text(), QStringLiteral("8.84 m east, 8.84 m north"));
	QVERIFY(reference_label->text().contains(QStringLiteral("0.00")));
	
	// A distance of nothing is no reading.
	distance_box->setValue(0.0);
	QVERIFY(!add_button->isEnabled());
	QVERIFY(!widget.previewTarget(&target));
	
	// The slope distance with its inclination.
	kind_box->setCurrentIndex(kind_box->findData(true));
	distance_box->setValue(12.55);
	inclination_box->setValue(5.0);
	QVERIFY(add_button->isEnabled());
	QCOMPARE(height_label->text(), QStringLiteral("+1.09 m"));
	QCOMPARE(target_label->text(), QStringLiteral("8.84 m east, 8.84 m north"));
	QVERIFY(qAbs(widget.reading().slope_distance - 12.55) < 1e-9);
	QVERIFY(qIsNaN(widget.reading().horizontal_distance));
	
	// The rear sight.
	sight_box->setCurrentIndex(sight_box->findData(int(RangefinderSurvey::RearSight)));
	QCOMPARE(widget.survey().sight, RangefinderSurvey::RearSight);
	QCOMPARE(height_label->text(), QStringLiteral("-1.09 m"));
	QCOMPARE(target_label->text(), QStringLiteral("8.84 m west, 8.84 m south"));
	sight_box->setCurrentIndex(sight_box->findData(int(RangefinderSurvey::ForwardSight)));
	
	// The choices are stored.
	QCOMPARE(RangefinderSettings::load().slope_distance, true);
	
	// Add, refused: the reading is not taken.
	auto asked = 0;
	auto accept = false;
	auto asked_for = MapCoordF{};
	widget.setAddFunction([&](const MapCoordF& position) {
		++asked;
		asked_for = position;
		return accept;
	});
	add_button->click();
	QCOMPARE(asked, 1);
	QVERIFY2(near(asked_for, 0.884, -0.884, 1e-3), qPrintable(shown(asked_for)));
	QVERIFY(near(widget.survey().reference(), 0, 0));
	QVERIFY(!widget.survey().hasPreviousTarget());
	
	// Add, accepted: in a linear traverse the target is the reference now.
	accept = true;
	add_button->click();
	QCOMPARE(asked, 2);
	QVERIFY2(near(widget.survey().reference(), 0.884, -0.884, 1e-3), qPrintable(shown(widget.survey().reference())));
	QVERIFY(widget.survey().hasPreviousTarget());
	
	// In a radial traverse the reference stays, and the previous target can be taken.
	traverse_box->setCurrentIndex(traverse_box->findData(int(RangefinderSurvey::RadialTraverse)));
	widget.setReference(MapCoordF(0, 0));
	add_button->click();
	QCOMPARE(asked, 3);
	QVERIFY(near(widget.survey().reference(), 0, 0));
	QVERIFY(previous_button->isEnabled());
	previous_button->click();
	QVERIFY2(near(widget.survey().reference(), 0.884, -0.884, 1e-3), qPrintable(shown(widget.survey().reference())));
	
	// The mobile form has a button to close it.
	RangefinderWidget sheet(&map, true);
	auto* close_button = sheet.findChild<QAbstractButton*>(QStringLiteral("rangefinder-close"));
	QVERIFY(close_button);
	auto closed = 0;
	connect(&sheet, &RangefinderWidget::closeClicked, this, [&closed]() { ++closed; });
	close_button->click();
	QCOMPARE(closed, 1);
	// It starts with the stored choices.
	QCOMPARE(sheet.survey().traverse, RangefinderSurvey::RadialTraverse);
	
	RangefinderSettings{}.save();
}


/// Accept (7)
void RangefinderTest::editorTest()
{
	RangefinderSettings{}.save();
	
	auto* map = new Map();
	map->setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map->addColor(black, 0);
	auto* line = new LineSymbol();
	line->setLineWidth(0.5);
	line->setColor(black);
	map->addSymbol(line, 0);
	auto* point = new PointSymbol();
	point->setInnerRadius(300);
	point->setInnerColor(black);
	map->addSymbol(point, 1);
	auto* text = new TextSymbol();
	map->addSymbol(text, 2);
	
	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	editor->getSymbolWidget()->selectSingleSymbol(point);
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	
	auto* rangefinder_act = editor->getAction("rangefinder");
	QVERIFY(rangefinder_act);
	QVERIFY(rangefinder_act->isCheckable());
	QVERIFY(!window->findChild<RangefinderWidget*>());
	
	rangefinder_act->trigger();
	QVERIFY(rangefinder_act->isChecked());
	auto* widget = window->findChild<RangefinderWidget*>();
	QVERIFY(widget);
	QVERIFY(!widget->isHidden());
	rangefinder_act->trigger();
	QVERIFY(!rangefinder_act->isChecked());
	QVERIFY(widget->parentWidget()->isHidden());
	rangefinder_act->trigger();
	QVERIFY(rangefinder_act->isChecked());
	QCOMPARE(window->findChildren<RangefinderWidget*>().size(), 1);
	
	auto* distance_box = widget->findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-distance"));
	auto* azimuth_box = widget->findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-azimuth"));
	auto* traverse_box = widget->findChild<QComboBox*>(QStringLiteral("rangefinder-traverse"));
	auto* add_button = widget->findChild<QAbstractButton*>(QStringLiteral("rangefinder-add"));
	auto* click_button = widget->findChild<QAbstractButton*>(QStringLiteral("rangefinder-reference-click"));
	auto* gps_button = widget->findChild<QAbstractButton*>(QStringLiteral("rangefinder-reference-gps"));
	QVERIFY(distance_box && azimuth_box && traverse_box && add_button && click_button && gps_button);
	auto const message = [window]() {
		return window->statusBar()->currentMessage();
	};
	auto const* part = map->getCurrentPart();
	
	// A point object at the target.
	editor->rangefinderSetReference(MapCoordF(0, 0));
	QVERIFY(widget->survey().hasReference());
	distance_box->setValue(12.50);
	azimuth_box->setValue(45.0);
	QVERIFY(add_button->isEnabled());
	add_button->click();
	QCOMPARE(part->getNumObjects(), 1);
	QCOMPARE(part->getObject(0)->getSymbol(), static_cast<const Symbol*>(point));
	{
		auto const position = MapCoordF(part->getObject(0)->getRawCoordinateVector().front());
		QVERIFY2(near(position, 0.884, -0.884, 0.001), qPrintable(shown(position)));
	}
	// Linear traverse: the reference went on.
	QVERIFY2(near(widget->survey().reference(), 0.88388, -0.88388, 1e-4), qPrintable(shown(widget->survey().reference())));
	QVERIFY(map->undoManager().canUndo());
	map->undoManager().undo();
	QCOMPARE(part->getNumObjects(), 0);
	
	// A text symbol cannot be drawn by a reading.
	editor->getSymbolWidget()->selectSingleSymbol(text);
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	editor->rangefinderSetReference(MapCoordF(0, 0));
	add_button->click();
	QCOMPARE(part->getNumObjects(), 0);
	QVERIFY(near(widget->survey().reference(), 0, 0));
	QVERIFY(!message().isEmpty());
	
	// A square from a radial traverse, with a line symbol: the path tool is chosen.
	editor->getSymbolWidget()->selectSingleSymbol(line);
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!editor->getTool() || !editor->getTool()->acceptsNumericVertices());
	traverse_box->setCurrentIndex(traverse_box->findData(int(RangefinderSurvey::RadialTraverse)));
	editor->rangefinderSetReference(MapCoordF(0, 0));
	distance_box->setValue(10.0);
	for (auto azimuth : { 0.0, 90.0, 180.0, 270.0 })
	{
		azimuth_box->setValue(azimuth);
		add_button->click();
		QVERIFY(editor->getTool() && editor->getTool()->acceptsNumericVertices());
		QVERIFY(near(widget->survey().reference(), 0, 0));
	}
	QCOMPARE(part->getNumObjects(), 0);
	{
		// Return closes the path and finishes it.
		QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		editor->getTool()->keyPressEvent(&finish);
	}
	QCOMPARE(part->getNumObjects(), 1);
	{
		auto const* object = part->getObject(0);
		QCOMPARE(object->getSymbol(), static_cast<const Symbol*>(line));
		auto const& coords = object->getRawCoordinateVector();
		QString vertices;
		for (auto const& coord : coords)
			vertices += shown(MapCoordF(coord)) + QLatin1Char(' ');
		QVERIFY2(coords.size() == 5, qPrintable(vertices));
		QVERIFY2(near(MapCoordF(coords[0]), 0, -1, 0.001), qPrintable(vertices));
		QVERIFY2(near(MapCoordF(coords[1]), 1, 0, 0.001), qPrintable(vertices));
		QVERIFY2(near(MapCoordF(coords[2]), 0, 1, 0.001), qPrintable(vertices));
		QVERIFY2(near(MapCoordF(coords[3]), -1, 0, 0.001), qPrintable(vertices));
		QVERIFY2(near(MapCoordF(coords[4]), 0, -1, 0.001), qPrintable(vertices));
	}
	// Return in a field is Add, and Finish path ends the path at its last vertex.
	auto* finish_button = widget->findChild<QAbstractButton*>(QStringLiteral("rangefinder-finish"));
	QVERIFY(finish_button);
	finish_button->click();  // nothing is drawn: nothing happens
	QCOMPARE(part->getNumObjects(), 1);
	editor->rangefinderSetReference(MapCoordF(10, 10));
	azimuth_box->setValue(90.0);
	QTest::keyClick(distance_box, Qt::Key_Return);
	QVERIFY(editor->getTool()->editingInProgress());
	azimuth_box->setValue(180.0);
	QTest::keyClick(azimuth_box, Qt::Key_Return);
	QCOMPARE(part->getNumObjects(), 1);
	finish_button->click();
	QCOMPARE(part->getNumObjects(), 2);
	QVERIFY(!editor->getTool()->editingInProgress());
	{
		auto const& coords = part->getObject(1)->getRawCoordinateVector();
		QCOMPARE(int(coords.size()), 2);
		QVERIFY2(near(MapCoordF(coords[0]), 11, 10, 0.001), qPrintable(shown(MapCoordF(coords[0]))));
		QVERIFY2(near(MapCoordF(coords[1]), 10, 11, 0.001), qPrintable(shown(MapCoordF(coords[1]))));
	}
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	
	// The reference by a click on the map.
	QVERIFY(click_button->isCheckable());
	click_button->click();
	QVERIFY(qobject_cast<RangefinderReferenceTool*>(editor->getTool()));
	QVERIFY(click_button->isChecked());
	{
		auto* tool = editor->getTool();
		auto* map_widget = editor->getMainWidget();
		auto const position = MapCoordF(5, 5);
		auto const pixel = map_widget->mapToViewport(position).toPoint();
		QMouseEvent press(QEvent::MouseButtonPress, pixel, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		tool->mousePressEvent(&press, position, map_widget);
		QMouseEvent release(QEvent::MouseButtonRelease, pixel, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		tool->mouseReleaseEvent(&release, position, map_widget);
	}
	QVERIFY(!qobject_cast<RangefinderReferenceTool*>(editor->getTool()));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(!click_button->isChecked());
	QVERIFY2(near(widget->survey().reference(), 5, 5), qPrintable(shown(widget->survey().reference())));
	
	// The reference from the GPS position, of which there is none.
	window->statusBar()->clearMessage();
	gps_button->click();
	QVERIFY(near(widget->survey().reference(), 5, 5));
	QCOMPARE(message(), QStringLiteral("There is no GPS position which can be used."));
	
	rangefinder_act->trigger();
	QVERIFY(!rangefinder_act->isChecked());
	window->deleteLater();
}


/// Accept (8)
void RangefinderTest::sentenceTest()
{
	auto const body = QByteArray("PLTIT,HV,12.50,M,45.0,D,5.0,D,12.55,M");

	RangefinderSentenceParser parser;
	QCOMPARE(parser.readingCount(), 0);
	QCOMPARE(parser.otherCount(), 0);
	QCOMPARE(parser.feed(sentence(body)), RangefinderSentenceParser::Reading);
	QCOMPARE(parser.readingCount(), 1);
	{
		auto const& shot = parser.reading();
		QCOMPARE(shot.horizontal_distance, 12.50);
		QCOMPARE(shot.azimuth, 45.0);
		QCOMPARE(shot.inclination, 5.0);
		QCOMPARE(shot.slope_distance, 12.55);

		Map map;
		map.setScaleDenominator(10000);
		RangefinderSurvey survey;
		survey.setReference(MapCoordF(0, 0));
		MapCoordF target;
		QVERIFY(survey.target(shot, map.getGeoreferencing(), &target));
		QVERIFY2(near(target, 0.884, -0.884, 0.001), qPrintable(shown(target)));
	}

	// A wrong checksum.
	QCOMPARE(parser.feed(QByteArray("$") + body + QByteArray("*00\r\n")), RangefinderSentenceParser::Other);
	QCOMPARE(parser.readingCount(), 1);
	QCOMPARE(parser.otherCount(), 1);
	// No checksum at all, and no line end.
	QCOMPARE(parser.feed(QByteArray("$") + body), RangefinderSentenceParser::Reading);
	QCOMPARE(parser.readingCount(), 2);
	// White space around the line.
	QByteArray const padded = QByteArray("  ") + sentence(body) + QByteArray(" \n");
	QCOMPARE(parser.feed(padded), RangefinderSentenceParser::Reading);
	QCOMPARE(parser.readingCount(), 3);

	// Feet and yards.
	QCOMPARE(parser.feed(sentence("PLTIT,HV,12.50,F,45.0,D,5.0,D,12.55,Y")), RangefinderSentenceParser::Reading);
	QVERIFY2(qAbs(parser.reading().horizontal_distance - 3.81) < 1e-9, qPrintable(QString::number(parser.reading().horizontal_distance, 'f', 6)));
	QVERIFY2(qAbs(parser.reading().slope_distance - 12.55 * 0.9144) < 1e-9, qPrintable(QString::number(parser.reading().slope_distance, 'f', 6)));
	QCOMPARE(parser.feed(sentence("PLTIT,HV,12.50,Y,45.0,D,5.0,D,12.55,M")), RangefinderSentenceParser::Reading);
	QVERIFY(qAbs(parser.reading().horizontal_distance - 11.43) < 1e-9);
	// A unit which is not known.
	QCOMPARE(parser.feed(sentence("PLTIT,HV,12.50,X,45.0,D,5.0,D,12.55,M")), RangefinderSentenceParser::Reading);
	QVERIFY(qIsNaN(parser.reading().horizontal_distance));
	QCOMPARE(parser.reading().slope_distance, 12.55);

	// No compass: the azimuth is empty.
	QCOMPARE(parser.feed(sentence("PLTIT,HV,12.50,M,,D,5.0,D,12.55,M")), RangefinderSentenceParser::Reading);
	QVERIFY(qIsNaN(parser.reading().azimuth));
	QCOMPARE(parser.reading().horizontal_distance, 12.50);
	QCOMPARE(parser.reading().inclination, 5.0);
	QVERIFY(!parser.reading().isComplete());

	// What is not a reading.
	auto const readings = parser.readingCount();
	auto const others = parser.otherCount();
	QCOMPARE(parser.feed(sentence("PLTIT,HT,3.20,M")), RangefinderSentenceParser::Other);
	QCOMPARE(parser.feed(sentence("GPGGA,101500.00,5000.00000,N,10500.00000,W,2,09,0.9,362.4,M,47.0,M,,")), RangefinderSentenceParser::Other);
	QCOMPARE(parser.feed(QByteArray("hello\r\n")), RangefinderSentenceParser::Other);
	QCOMPARE(parser.feed(sentence("PLTIT,HV,12.50,M")), RangefinderSentenceParser::Other);
	QCOMPARE(parser.feed(QByteArray("\r\n")), RangefinderSentenceParser::NoLine);
	QCOMPARE(parser.feed(QByteArray()), RangefinderSentenceParser::NoLine);
	QCOMPARE(parser.readingCount(), readings);
	QCOMPARE(parser.otherCount(), others + 4);
	// The last reading is still there.
	QCOMPARE(parser.reading().horizontal_distance, 12.50);
}


/// Accept (9)
void RangefinderTest::sourceTest()
{
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	auto const address = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());

	RangefinderSource source;
	QVERIFY(!source.isOpen());
	std::vector<RangefinderReading> readings;
	connect(&source, &RangefinderSource::readingReceived, this, [&readings](const RangefinderReading& shot) {
		readings.push_back(shot);
	});
	QSignalSpy states(&source, &RangefinderSource::stateChanged);

	QVERIFY(source.open(address, 4800));
	QTRY_VERIFY(server.hasPendingConnections());
	auto* client = server.nextPendingConnection();
	QVERIFY(client);
	QTRY_VERIFY(source.isOpen());
	QVERIFY(states.count() > 0);
	QVERIFY(source.errorString().isEmpty());

	QByteArray const lines = sentence("PLTIT,HV,12.50,M,45.0,D,5.0,D,12.55,M")
	                         + sentence("GPGGA,101500.00,5000.00000,N,10500.00000,W,2,09,0.9,362.4,M,47.0,M,,")
	                         + QByteArray("$PLTIT,HV,12.50,M,45.0,D,5.0,D,12.55,M*00\r\n");
	client->write(lines);
	client->flush();
	QTRY_COMPARE(source.otherCount(), 2);
	QCOMPARE(source.readingCount(), 1);
	QCOMPARE(int(readings.size()), 1);
	QCOMPARE(readings.front().horizontal_distance, 12.50);
	QCOMPARE(readings.front().azimuth, 45.0);

	// A line in two pieces.
	QByteArray const line = sentence("PLTIT,HV,20.00,M,90.0,D,0.0,D,20.00,M");
	client->write(line.left(14));
	client->flush();
	QTest::qWait(100);
	QCOMPARE(int(readings.size()), 1);
	client->write(line.mid(14));
	client->flush();
	QTRY_COMPARE(int(readings.size()), 2);
	QCOMPARE(readings.back().horizontal_distance, 20.0);
	QCOMPARE(readings.back().azimuth, 90.0);
	QCOMPARE(source.otherCount(), 2);

	// The other side closes.
	states.clear();
	client->disconnectFromHost();
	QTRY_VERIFY(!source.isOpen());
	QVERIFY(states.count() > 0);
	QVERIFY(!source.errorString().isEmpty());

	// A line given by hand counts as one which was read.
	source.processLine(line);
	QCOMPARE(int(readings.size()), 3);
	QCOMPARE(source.readingCount(), 3);

	// Nothing listens.
	server.close();
	RangefinderSource lonely;
	lonely.open(address, 4800);
	QTRY_VERIFY(!lonely.errorString().isEmpty());
	QVERIFY(!lonely.isOpen());
	lonely.close();

	// A text which is neither a serial port nor a network port.
	RangefinderSource nameless;
	QVERIFY(!nameless.open(QString{}, 4800));
	QVERIFY(!nameless.isOpen());

	// The settings of the device.
	auto const path = settings_dir.filePath(QStringLiteral("rangefinder-device.ini"));
	{
		QSettings file(path, QSettings::IniFormat);
		auto const defaults = RangefinderSettings::load(file);
		QVERIFY(defaults.port.isEmpty());
		QCOMPARE(defaults.baud, 4800);
		QVERIFY(defaults.draw_automatically);

		RangefinderSettings settings;
		settings.port = QStringLiteral("COM7");
		settings.baud = 38400;
		settings.draw_automatically = false;
		settings.save(file);
	}
	{
		QSettings file(path, QSettings::IniFormat);
		auto const settings = RangefinderSettings::load(file);
		QCOMPARE(settings.port, QStringLiteral("COM7"));
		QCOMPARE(settings.baud, 38400);
		QVERIFY(!settings.draw_automatically);
		file.setValue(QStringLiteral("Rangefinder/baud"), -5);
		QCOMPARE(RangefinderSettings::load(file).baud, 4800);
	}
}


/// Accept (10)
void RangefinderTest::deviceWidgetTest()
{
	RangefinderSettings{}.save();
	Map map;
	map.setScaleDenominator(10000);

	RangefinderWidget widget(&map, false);
	auto* distance_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-distance"));
	auto* kind_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-distance-kind"));
	auto* inclination_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-inclination"));
	auto* azimuth_box = widget.findChild<QDoubleSpinBox*>(QStringLiteral("rangefinder-azimuth"));
	auto* add_button = widget.findChild<QAbstractButton*>(QStringLiteral("rangefinder-add"));
	auto* port_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-port"));
	auto* baud_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-baud"));
	auto* connect_button = widget.findChild<QAbstractButton*>(QStringLiteral("rangefinder-connect"));
	auto* state_label = widget.findChild<QLabel*>(QStringLiteral("rangefinder-state"));
	auto* auto_check = widget.findChild<QCheckBox*>(QStringLiteral("rangefinder-auto"));
	auto* counts_label = widget.findChild<QLabel*>(QStringLiteral("rangefinder-counts"));
	QVERIFY(distance_box && kind_box && inclination_box && azimuth_box && add_button);
	QVERIFY(port_box && baud_box && connect_button && state_label && auto_check && counts_label);
	QVERIFY(widget.source());

	// The defaults.
	QVERIFY(port_box->isEditable());
	QVERIFY(port_box->currentText().isEmpty());
	QCOMPARE(baud_box->currentData().toInt(), 4800);
	QVERIFY(connect_button->isCheckable());
	QVERIFY(!connect_button->isChecked());
	QVERIFY(auto_check->isChecked());
	QCOMPARE(counts_label->text(), QStringLiteral("Readings: 0, other lines: 0"));

	auto asked = 0;
	auto asked_for = MapCoordF{};
	widget.setAddFunction([&](const MapCoordF& position) {
		++asked;
		asked_for = position;
		return true;
	});
	widget.setReference(MapCoordF(0, 0));
	// Radial, so that the reference stays for the readings of this test.
	auto* traverse_box = widget.findChild<QComboBox*>(QStringLiteral("rangefinder-traverse"));
	QVERIFY(traverse_box);
	traverse_box->setCurrentIndex(traverse_box->findData(int(RangefinderSurvey::RadialTraverse)));

	// With Draw automatically the reading is added at once.
	widget.source()->processLine(sentence("PLTIT,HV,12.50,M,45.0,D,5.0,D,12.55,M"));
	QCOMPARE(distance_box->value(), 12.50);
	QCOMPARE(kind_box->currentData().toBool(), false);
	QCOMPARE(inclination_box->value(), 5.0);
	QCOMPARE(azimuth_box->value(), 45.0);
	QCOMPARE(asked, 1);
	QVERIFY2(near(asked_for, 0.884, -0.884, 0.001), qPrintable(shown(asked_for)));
	QCOMPARE(counts_label->text(), QStringLiteral("Readings: 1, other lines: 0"));

	// Without, it waits in the fields.
	auto_check->setChecked(false);
	QVERIFY(!RangefinderSettings::load().draw_automatically);
	widget.source()->processLine(sentence("PLTIT,HV,30.00,M,180.0,D,0.0,D,30.00,M"));
	widget.source()->processLine(sentence("GPGGA,101500.00,5000.00000,N,10500.00000,W,2,09,0.9,362.4,M,47.0,M,,"));
	QCOMPARE(asked, 1);
	QCOMPARE(distance_box->value(), 30.0);
	QCOMPARE(azimuth_box->value(), 180.0);
	QCOMPARE(counts_label->text(), QStringLiteral("Readings: 2, other lines: 1"));
	add_button->click();
	QCOMPARE(asked, 2);
	QVERIFY2(near(asked_for, 0, 3, 0.001), qPrintable(shown(asked_for)));

	// A rangefinder without compass: the typed azimuth stays.
	auto_check->setChecked(true);
	azimuth_box->setValue(90.0);
	widget.source()->processLine(sentence("PLTIT,HV,10.00,M,,D,0.0,D,10.00,M"));
	QCOMPARE(azimuth_box->value(), 90.0);
	QCOMPARE(asked, 3);
	QVERIFY2(near(asked_for, 1, 0, 0.001), qPrintable(shown(asked_for)));

	// Slope distance and inclination only.
	widget.source()->processLine(sentence("PLTIT,HV,,M,0.0,D,60.0,D,20.00,M"));
	QCOMPARE(kind_box->currentData().toBool(), true);
	QCOMPARE(distance_box->value(), 20.0);
	QCOMPARE(inclination_box->value(), 60.0);
	QCOMPARE(asked, 4);
	QVERIFY2(near(asked_for, 0, -1, 0.001), qPrintable(shown(asked_for)));

	// A reading with no distance at all changes nothing and adds nothing.
	widget.source()->processLine(sentence("PLTIT,HV,,M,10.0,D,,D,,M"));
	QCOMPARE(asked, 4);
	QCOMPARE(distance_box->value(), 20.0);

	// Connecting to a port on which nothing listens: the switch goes off again, and the state says why.
	quint16 free_port = 0;
	{
		QTcpServer probe;
		QVERIFY(probe.listen(QHostAddress::LocalHost));
		free_port = probe.serverPort();
	}
	port_box->setEditText(QStringLiteral("127.0.0.1:%1").arg(free_port));
	connect_button->click();
	QTRY_VERIFY(!connect_button->isChecked());
	QVERIFY(!state_label->text().isEmpty());
	QCOMPARE(RangefinderSettings::load().port, QStringLiteral("127.0.0.1:%1").arg(free_port));

	// Connecting to one which is there.
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	port_box->setEditText(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
	connect_button->click();
	QTRY_VERIFY(server.hasPendingConnections());
	auto* client = server.nextPendingConnection();
	QTRY_VERIFY(widget.source()->isOpen());
	QVERIFY(connect_button->isChecked());
	QCOMPARE(connect_button->text(), QStringLiteral("Disconnect"));
	QCOMPARE(state_label->text(), QStringLiteral("Connected"));
	client->write(sentence("PLTIT,HV,40.00,M,270.0,D,0.0,D,40.00,M"));
	client->flush();
	QTRY_COMPARE(asked, 5);
	QVERIFY2(near(asked_for, -4, 0, 0.001), qPrintable(shown(asked_for)));
	connect_button->click();
	QVERIFY(!connect_button->isChecked());
	QVERIFY(!widget.source()->isOpen());
	QCOMPARE(connect_button->text(), QStringLiteral("Connect"));
	QCOMPARE(state_label->text(), QStringLiteral("Not connected"));

	RangefinderSettings{}.save();
}


/// Accept (11)
void RangefinderTest::deviceEditorTest()
{
	RangefinderSettings{}.save();

	auto* map = new Map();
	map->setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map->addColor(black, 0);
	auto* point = new PointSymbol();
	point->setInnerRadius(300);
	point->setInnerColor(black);
	map->addSymbol(point, 0);

	auto* window = new MainWindow();
	auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
	window->setController(editor);
	editor->getSymbolWidget()->selectSingleSymbol(point);
	editor->setEditTool();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

	auto* rangefinder_act = editor->getAction("rangefinder");
	QVERIFY(rangefinder_act);
	rangefinder_act->trigger();
	auto* widget = window->findChild<RangefinderWidget*>();
	QVERIFY(widget);
	editor->rangefinderSetReference(MapCoordF(0, 0));

	auto const* part = map->getCurrentPart();
	QCOMPARE(part->getNumObjects(), 0);
	widget->source()->processLine(sentence("PLTIT,HV,12.50,M,45.0,D,5.0,D,12.55,M"));
	QCOMPARE(part->getNumObjects(), 1);
	{
		auto const position = MapCoordF(part->getObject(0)->getRawCoordinateVector().front());
		QVERIFY2(near(position, 0.884, -0.884, 0.001), qPrintable(shown(position)));
	}

	rangefinder_act->trigger();
	window->deleteLater();
}


QTEST_MAIN(RangefinderTest)
#include "rangefinder_t.moc"  // IWYU pragma: keep
