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
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRadioButton>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "elevation/contours.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/elevation_wizard.h"
#include "elevation/landforms.h"
#include "elevation/point_cloud.h"
#include "elevation/raster_products.h"
#include "gui/main_window.h"
#include "gui/map/elevation_wizard_dialog.h"
#include "gui/map/map_editor.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

QString fixture(const char* name)
{
	return QDir(QString::fromUtf8(FOOM_FIXTURES_DIR)).absoluteFilePath(QLatin1String("contour/") + QLatin1String(name));
}

/// A map at 1:10 000 in a State Plane zone in metres whose origin lies in the middle of the grid.
void georeference(Map& map)
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"), { QStringLiteral("26954") });
	georef.setProjectedRefPoint(QPointF(966300.0, 557200.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	map.setGeoreferencing(georef);
}

QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

bool sameFile(const QString& a, const QString& b)
{
	auto const bytes = fileBytes(a);
	return !bytes.isEmpty() && bytes == fileBytes(b);
}

/// Two objects of the same symbol and the same coordinates.
bool sameObject(const Object* a, const Object* b)
{
	if (!a || !b || a->getType() != b->getType() || a->getSymbol()->getNumberAsString() != b->getSymbol()->getNumberAsString())
		return false;
	auto const& ca = a->getRawCoordinateVector();
	auto const& cb = b->getRawCoordinateVector();
	if (ca.size() != cb.size())
		return false;
	for (std::size_t i = 0; i < ca.size(); ++i)
	{
		if (ca[i] != cb[i])
			return false;
	}
	return true;
}

/// Ground on a plane rising eastwards, four points per square metre over 40 by 40 m,
/// with a square of trees 12 m high from (10, 10) to (22, 22).
std::vector<CloudPoint> cloudPoints()
{
	std::vector<CloudPoint> points;
	for (int i = 0; i < 80; ++i)
	{
		for (int j = 0; j < 80; ++j)
		{
			CloudPoint p;
			p.x = 1000.25 + 0.5 * i;
			p.y = 2000.25 + 0.5 * j;
			p.z = 300.0 + 0.1 * (p.x - 1000.0);
			p.intensity = quint16(10 * i);
			p.classification = 2;
			auto const in_square = p.x > 1010.0 && p.x < 1022.0 && p.y > 2010.0 && p.y < 2022.0;
			if (in_square)
			{
				p.return_number = 2;
				p.number_of_returns = 2;
			}
			points.push_back(p);
			if (in_square)
			{
				auto top = p;
				auto const r = std::hypot(p.x - 1016.0, p.y - 2016.0);
				top.z = p.z + 12.0 * (1.0 - 0.5 * (r / 6.0) * (r / 6.0));
				top.classification = 5;
				top.return_number = 1;
				points.push_back(top);
			}
		}
	}
	return points;
}

QAbstractButton* button(ElevationWizardDialog& wizard, const char* name)
{
	return wizard.findChild<QAbstractButton*>(QString::fromLatin1(name));
}

/// The ids of the pages which next() visits from the current one until it stops.
QList<int> visit(ElevationWizardDialog& wizard)
{
	QList<int> ids;
	for (int guard = 0; guard < 20; ++guard)
	{
		auto const before = wizard.currentId();
		wizard.next();
		if (wizard.currentId() == before)
			break;
		ids.push_back(wizard.currentId());
	}
	return ids;
}

/// A map editor in a main window, for the commands of the Elevation menu.
struct Editor
{
	MainWindow* window;
	MapEditorController* controller;

	explicit Editor(Map* map)
	{
		window = new MainWindow();
		controller = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(controller);
	}
	~Editor() { window->deleteLater(); }
};

}  // namespace



/**
 * @test The elevation wizard, the design notes, "Wizard".
 */
class ElevationWizardTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QString::fromLatin1("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
		QVERIFY2(QFileInfo::exists(fixture("moved/test.asc")), "the fixture contour/test.asc is missing");
	}

	// Accept (1).
	void gridPages()
	{
		using P = ElevationWizardDialog;
		ElevationWizardDialog wizard(QString{}, nullptr);
		wizard.restart();
		QCOMPARE(wizard.currentId(), int(P::SourcePage));
		QVERIFY(visit(wizard).isEmpty());  // no file

		wizard.setFiles({ fixture("moved/test.asc") });
		QCOMPARE(visit(wizard), (QList<int>{ P::SettingsPage, P::ProductsPage, P::ContoursPage, P::HillShadePage, P::SlopePage, P::OutputPage }));

		// The settings page reports the grid.
		wizard.restart();
		wizard.setFiles({ fixture("moved/test.asc") });
		wizard.next();
		QCOMPARE(wizard.currentId(), int(P::SettingsPage));
		auto const report = wizard.findChild<QLabel*>(QStringLiteral("report_label"))->text();
		QVERIFY2(report.contains(QStringLiteral("600 by 400 cells of 1 m")), qPrintable(report));
		QVERIFY2(report.contains(QStringLiteral("500.05")) && report.contains(QStringLiteral("540.36")), qPrintable(report));

		// The point-cloud products are not offered for grids.
		wizard.next();
		QCOMPARE(wizard.currentId(), int(P::ProductsPage));
		QVERIFY(!button(wizard, "vegetation_height_check")->isEnabled());
		QVERIFY(!button(wizard, "classification_check")->isEnabled());
		button(wizard, "hypsometric_check")->setChecked(true);
		button(wizard, "features_check")->setChecked(true);
		QCOMPARE(visit(wizard), (QList<int>{ P::ContoursPage, P::HillShadePage, P::SlopePage, P::HypsometricPage, P::FeaturesPage, P::OutputPage }));

		// No product: straight to the output.
		ElevationWizardDialog none(QString{}, nullptr);
		none.restart();
		none.setFiles({ fixture("moved/test.asc") });
		none.next();
		none.next();
		QCOMPARE(none.currentId(), int(P::ProductsPage));
		for (auto const* name : { "contours_check", "hillshade_check", "slope_check" })
			button(none, name)->setChecked(false);
		QCOMPARE(visit(none), (QList<int>{ P::OutputPage }));

		// A file which is not a grid does not pass the settings page.
		QTemporaryDir dir;
		auto const junk = dir.filePath(QStringLiteral("junk.asc"));
		{
			QFile file(junk);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("not a grid\n");
		}
		wizard.restart();
		wizard.setFiles({ junk });
		QCOMPARE(visit(wizard), (QList<int>{ P::SettingsPage }));
	}

	// Accept (3), for grids.
	void gridDefaults()
	{
		ElevationWizardDialog wizard(QString{}, nullptr);
		wizard.restart();
		wizard.setFiles({ fixture("moved/test.asc") });
		visit(wizard);
		QCOMPARE(wizard.currentId(), int(ElevationWizardDialog::OutputPage));
		auto job = wizard.job();
		QCOMPARE(job.source, ElevationWizardJob::Grid);
		QCOMPARE(job.files, QStringList{ fixture("moved/test.asc") });
		QVERIFY(job.contours && job.hill_shading && job.slope);
		QVERIFY(!job.hypsometric && !job.features);
		QVERIFY(!job.vegetation_height && !job.cloud.vegetation_map && !job.cloud.feature_map && !job.cloud.intensity_map && !job.cloud.classification_map);
		QVERIFY(job.attach);
		QVERIFY(!job.open_options.feet_to_metres && !job.open_options.millimetres_to_metres && !job.open_options.shift_below_sea_level);
		QCOMPARE(int(job.contour_sets.size()), 1);
		auto const& set = job.contour_sets.front();
		QVERIFY(!set.smoothed);
		QCOMPARE(set.interval, 1.0);
		QCOMPARE(set.main_interval, 5.0);
		QCOMPARE(set.index_interval, 25.0);
		QVERIFY(!set.depression_symbols);
		HillShadeParams const hill;
		QCOMPARE(job.hill_shade.method, hill.method);
		QCOMPARE(job.hill_shade.multi_directional, hill.multi_directional);
		QCOMPARE(job.hill_shade.azimuth_deg, hill.azimuth_deg);
		QCOMPARE(job.hill_shade.elevation_deg, hill.elevation_deg);
		QCOMPARE(job.hill_shade.exaggeration, hill.exaggeration);
		QCOMPARE(job.slope_params.binary, false);
		QCOMPARE(job.slope_params.threshold_deg, 45.0);
		QVERIFY(!job.landforms.cliffs);
		QCOMPARE(job.landforms.cliff_min_area, 3);
		QCOMPARE(job.landforms.cliff_min_length, 3);
		QVERIFY(job.landforms.knolls && job.landforms.depressions && !job.landforms.trees);
		QCOMPARE(job.landforms.min_height, 1.0);
		QCOMPARE(job.landforms.min_depth, 1.0);
		QCOMPARE(job.landforms.tree_min_height, 4.0);
		QCOMPARE(job.hypsometric_params.ramp, HypsometricParams::BrownYellowGreen);
		QCOMPARE(job.hypsometric_params.interval, 100.0);

		// The smoothed set.
		wizard.restart();
		wizard.setFiles({ fixture("moved/test.asc") });
		wizard.next();
		wizard.next();
		wizard.next();
		QCOMPARE(wizard.currentId(), int(ElevationWizardDialog::ContoursPage));
		wizard.findChild<QGroupBox*>(QStringLiteral("smoothed_group"))->setChecked(true);
		visit(wizard);
		job = wizard.job();
		QCOMPARE(int(job.contour_sets.size()), 2);
		auto const& smoothed = job.contour_sets.back();
		QVERIFY(smoothed.smoothed && smoothed.curves);
		QCOMPARE(smoothed.interval, 2.5);
		QCOMPARE(smoothed.main_interval, 5.0);
		QCOMPARE(smoothed.index_interval, 25.0);

		// The job of the defaults is the job's own default.
		auto const defaults = ElevationWizardJob::defaults(ElevationWizardJob::Grid);
		QVERIFY(defaults.contours && defaults.hill_shading && defaults.slope && !defaults.hypsometric && !defaults.features);
	}

	// Accept (2) and (3), for point clouds.
	void cloudPagesAndDefaults()
	{
		if (!PointCloud::supported())
			QSKIP("Built without LASzip");
		using P = ElevationWizardDialog;
		QTemporaryDir dir;
		auto const cloud = dir.filePath(QStringLiteral("plane.laz"));
		auto const points = cloudPoints();
		QString error;
		QVERIFY2(PointCloud::write(cloud, points, {}, 0, &error), qPrintable(error));

		ElevationWizardDialog wizard(QString{}, nullptr);
		wizard.restart();
		wizard.setSource(ElevationWizardJob::PointCloud);
		wizard.setFiles({ cloud });
		wizard.next();
		QCOMPARE(wizard.currentId(), int(P::SettingsPage));
		auto const report = wizard.findChild<QLabel*>(QStringLiteral("report_label"))->text();
		QVERIFY2(report.contains(QString::number(points.size()) + QLatin1String(" points")), qPrintable(report));
		wizard.next();
		QCOMPARE(wizard.currentId(), int(P::ProductsPage));
		QVERIFY(button(wizard, "vegetation_height_check")->isEnabled());
		QVERIFY(button(wizard, "vegetation_height_check")->isChecked());
		QVERIFY(button(wizard, "vegetation_map_check")->isChecked());
		QVERIFY(button(wizard, "intensity_check")->isChecked());
		QVERIFY(!button(wizard, "feature_map_check")->isChecked());
		QVERIFY(!button(wizard, "classification_check")->isChecked());
		QCOMPARE(visit(wizard), (QList<int>{ P::ContoursPage, P::HillShadePage, P::SlopePage, P::CloudProductsPage, P::OutputPage }));

		auto const job = wizard.job();
		QCOMPARE(job.source, ElevationWizardJob::PointCloud);
		QCOMPARE(job.gridding.cell, 1.0);
		QVERIFY(job.gridding.dtm && job.gridding.dsm);
		QVERIFY(job.vegetation_height && job.cloud.vegetation_map && job.cloud.intensity_map);
		QVERIFY(!job.cloud.feature_map && !job.cloud.classification_map && !job.features && !job.hypsometric);
		QVERIFY(!job.vegetation_height_options.greyscale);
		QCOMPARE(int(job.vegetation_height_options.classes.size()), int(defaultHeightClasses().size()));
		VegetationParams const vegetation;
		QCOMPARE(job.cloud.vegetation.undergrowth_from, vegetation.undergrowth_from);
		QCOMPARE(job.cloud.vegetation.undergrowth_to, vegetation.undergrowth_to);
		QCOMPARE(job.cloud.vegetation.undergrowth_kernel, vegetation.undergrowth_kernel);
		QCOMPARE(job.cloud.vegetation.undergrowth_thresholds[1], vegetation.undergrowth_thresholds[1]);
		QCOMPARE(job.cloud.vegetation.vegetation_from, vegetation.vegetation_from);
		QCOMPARE(job.cloud.vegetation.vegetation_to, vegetation.vegetation_to);
		QCOMPARE(job.cloud.vegetation.vegetation_kernel, vegetation.vegetation_kernel);
		QCOMPARE(job.cloud.vegetation.vegetation_thresholds[2], vegetation.vegetation_thresholds[2]);
		QVERIFY(job.cloud.vegetation.no_data_red);
		QCOMPARE(job.cloud.feature_from, 0.0);
		QCOMPARE(job.cloud.feature_to, 2.0);

		// Without the cloud's products, no page for them.
		wizard.restart();
		wizard.setSource(ElevationWizardJob::PointCloud);
		wizard.setFiles({ cloud });
		wizard.next();
		wizard.next();
		for (auto const* name : { "vegetation_height_check", "vegetation_map_check", "intensity_check" })
			button(wizard, name)->setChecked(false);
		QCOMPARE(visit(wizard), (QList<int>{ P::ContoursPage, P::HillShadePage, P::SlopePage, P::OutputPage }));
	}

	// Accept (4).
	void gridResult()
	{
		QTemporaryDir dir;
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("a")));
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("b")));
		auto const grid_a = dir.filePath(QStringLiteral("a/test.asc"));
		auto const grid_b = dir.filePath(QStringLiteral("b/test.asc"));
		QVERIFY(QFile::copy(fixture("moved/test.asc"), grid_a));
		QVERIFY(QFile::copy(fixture("moved/test.asc"), grid_b));

		// The wizard.
		Map map_a;
		georeference(map_a);
		auto job = ElevationWizardJob::defaults(ElevationWizardJob::Grid);
		job.files = QStringList{ grid_a };
		job.hypsometric = true;
		job.features = true;
		auto const undo_steps = map_a.undoManager().undoStepCount();
		auto const result = runElevationWizard(map_a, QString{}, job);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map_a.elevationSet().href(ElevationSet::Terrain), grid_a);
		QCOMPARE(map_a.getNumTemplates(), 3);
		QCOMPARE(map_a.undoManager().undoStepCount(), undo_steps + 1);
		QVERIFY(map_a.hasUnsavedChanges());
		QCOMPARE(result.files.size(), 3);

		// The single commands.
		auto* map_b = new Map;
		georeference(*map_b);
		Editor editor(map_b);
		map_b->elevationSet().setModel(ElevationSet::Terrain, ElevationModel::open(grid_b, {}), grid_b);
		auto const* model = map_b->elevationSet().model(ElevationSet::Terrain, map_b->getGeoreferencing().getProjectedCRSSpec());
		QVERIFY(model);
		QString error;
		auto const hill_path = dir.filePath(QStringLiteral("b/test_hillshade.tif"));
		auto const slope_path = dir.filePath(QStringLiteral("b/test_slope.tif"));
		QVERIFY2(writeGeoTiff(hillShade(*model, job.hill_shade), hill_path, &error), qPrintable(error));
		QVERIFY2(writeGeoTiff(slopeRaster(*model, job.slope_params), slope_path, &error), qPrintable(error));
		auto const hyps_path = editor.controller->writeHypsometricMap(job.hypsometric_params, false, &error);
		QVERIFY2(!hyps_path.isEmpty(), qPrintable(error));
		QVERIFY(sameFile(dir.filePath(QStringLiteral("a/test_hillshade.tif")), hill_path));
		QVERIFY(sameFile(dir.filePath(QStringLiteral("a/test_slope.tif")), slope_path));
		QVERIFY(sameFile(dir.filePath(QStringLiteral("a/test_hypsometric.tif")), hyps_path));

		auto contours = makeContourObjects(*map_b, *model, job.contour_sets.front(), contourSymbols(*map_b, false));
		QVERIFY(contours.error.isEmpty());
		QVERIFY(!contours.objects.empty());
		QCOMPARE(result.contours, int(contours.objects.size()));
		auto const* part_a = map_a.getCurrentPart();
		for (std::size_t i = 0; i < contours.objects.size(); ++i)
			QVERIFY2(sameObject(part_a->getObject(int(i)), contours.objects[i].get()), qPrintable(QString::number(i)));
		auto const features = editor.controller->extractLandforms(job.landforms);
		QCOMPARE(result.features, features);
		QCOMPARE(part_a->getNumObjects(), result.contours + result.features);

		// One undo takes everything away.
		QVERIFY(map_a.undoManager().undo());
		QCOMPARE(map_a.getNumObjects(), 0);

		// Without attaching: the files, no template.
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("c")));
		auto const grid_c = dir.filePath(QStringLiteral("c/test.asc"));
		QVERIFY(QFile::copy(fixture("moved/test.asc"), grid_c));
		Map map_c;
		georeference(map_c);
		job.files = QStringList{ grid_c };
		job.attach = false;
		job.features = false;
		auto const unattached = runElevationWizard(map_c, QString{}, job);
		QVERIFY2(unattached.ok, qPrintable(unattached.error));
		QCOMPARE(map_c.getNumTemplates(), 0);
		QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("c/test_hillshade.tif"))));
		QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("c/test_hypsometric.tif"))));
	}

	// Accept (5).
	void unitsAndMerging()
	{
		QTemporaryDir dir;
		auto const write = [&dir](const char* name, double x0, float z) {
			auto const path = dir.filePath(QString::fromLatin1(name));
			ElevationModel grid(11, 11, QPointF(x0, 11.0), 1.0, std::vector<float>(121, z));
			return grid.save(path) ? path : QString{};
		};
		auto const left = write("left.asc", 0.0, 100.0f);
		auto const right = write("right.asc", 11.0, 200.0f);
		QVERIFY(!left.isEmpty() && !right.isEmpty());

		Map map;
		auto job = ElevationWizardJob::defaults(ElevationWizardJob::Grid);
		job.contours = job.hill_shading = job.slope = false;
		job.files = QStringList{ left };
		job.open_options.feet_to_metres = true;
		auto result = runElevationWizard(map, QString{}, job);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(QFileInfo(map.elevationSet().href(ElevationSet::Terrain)).fileName(), QStringLiteral("left_terrain.tif"));
		auto const* terrain = map.elevationSet().model(ElevationSet::Terrain);
		QVERIFY(terrain);
		QVERIFY(std::abs(terrain->value(5, 5) - 30.48) < 1e-3);

		Map merged;
		job.open_options = {};
		job.files = QStringList{ left, right };
		result = runElevationWizard(merged, QString{}, job);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(QFileInfo(merged.elevationSet().href(ElevationSet::Terrain)).fileName(), QStringLiteral("left_terrain.tif"));
		terrain = merged.elevationSet().model(ElevationSet::Terrain);
		QVERIFY(terrain);
		QCOMPARE(terrain->width(), 22);
		QCOMPARE(terrain->extent(), QRectF(0.0, 0.0, 22.0, 11.0));
		QVERIFY(std::abs(terrain->at(QPointF(5.5, 5.5)) - 100.0) < 1e-4);
		QVERIFY(std::abs(terrain->at(QPointF(16.5, 5.5)) - 200.0) < 1e-4);
	}

	// Accept (6).
	void cloudResult()
	{
		if (!PointCloud::supported())
			QSKIP("Built without LASzip");
		QTemporaryDir dir;
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("a")));
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("b")));
		auto const cloud_a = dir.filePath(QStringLiteral("a/plane.laz"));
		auto const cloud_b = dir.filePath(QStringLiteral("b/plane.laz"));
		QString error;
		QVERIFY2(PointCloud::write(cloud_a, cloudPoints(), {}, 0, &error), qPrintable(error));
		QVERIFY(QFile::copy(cloud_a, cloud_b));

		Map map_a;
		auto job = ElevationWizardJob::defaults(ElevationWizardJob::PointCloud);
		job.files = QStringList{ cloud_a };
		job.features = true;
		job.landforms.trees = true;
		auto const result = runElevationWizard(map_a, QString{}, job);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(map_a.getNumTemplates(), 5);

		auto* map_b = new Map;
		Editor editor(map_b);
		auto products = job.cloud;
		products.vegetation_height = false;
		QVERIFY2(editor.controller->importPointClouds({ cloud_b }, job.gridding, products, &error), qPrintable(error));
		QVERIFY(!editor.controller->writeVegetationHeightMap(job.vegetation_height_options, true, &error).isEmpty());
		auto const* model = map_b->elevationSet().model(ElevationSet::Terrain);
		QVERIFY(model);
		auto const hill_path = productPath({}, map_b->elevationSet().href(ElevationSet::Terrain), QStringLiteral("hillshade"));
		auto const slope_path = productPath({}, map_b->elevationSet().href(ElevationSet::Terrain), QStringLiteral("slope"));
		QVERIFY(writeGeoTiff(hillShade(*model, job.hill_shade), hill_path, &error));
		QVERIFY(writeGeoTiff(slopeRaster(*model, job.slope_params), slope_path, &error));

		for (auto const* name : { "plane_DTM.tif", "plane_DSM.tif", "plane_vegetation.tif", "plane_intensity.tif",
		                          "plane_DTM_vegetation_height.tif", "plane_DTM_hillshade.tif", "plane_DTM_slope.tif" })
		{
			auto const a = dir.filePath(QLatin1String("a/") + QLatin1String(name));
			auto const b = dir.filePath(QLatin1String("b/") + QLatin1String(name));
			QVERIFY2(QFileInfo::exists(a), name);
			QVERIFY2(sameFile(a, b), name);
		}

		auto contours = makeContourObjects(*map_b, *model, job.contour_sets.front(), contourSymbols(*map_b, false));
		QCOMPARE(result.contours, int(contours.objects.size()));
		QVERIFY(result.contours > 0);
		auto const features = editor.controller->extractLandforms(job.landforms);
		QCOMPARE(result.features, features);
	}

	// Accept (7).
	void failure()
	{
		Map map;
		auto job = ElevationWizardJob::defaults(ElevationWizardJob::Grid);
		job.files = QStringList{ QStringLiteral("does-not-exist.asc") };
		auto const result = runElevationWizard(map, QString{}, job);
		QVERIFY(!result.ok);
		QVERIFY(!result.error.isEmpty());
		QVERIFY(map.elevationSet().isEmpty());
		QCOMPARE(map.getNumObjects(), 0);
		QCOMPARE(map.getNumTemplates(), 0);

		job.files.clear();
		QVERIFY(!runElevationWizard(map, QString{}, job).ok);
	}

	// Accept (8).
	void menu()
	{
		auto* map = new Map;
		Editor editor(map);
		auto* action = editor.controller->getAction("dem-wizard");
		QVERIFY(action);
		QMenu* elevation = nullptr;
		for (auto* menu_action : editor.window->menuBar()->actions())
		{
			if (menu_action->menu() && menu_action->menu()->actions().contains(editor.controller->getAction("dem-open")))
				elevation = menu_action->menu();
		}
		QVERIFY(elevation);
		QCOMPARE(elevation->actions().front(), action);
	}

	// Accept (9).
	void runner()
	{
		auto const executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));
		QTemporaryDir dir;
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("r")));
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("w")));
		auto const grid_r = dir.filePath(QStringLiteral("r/test.asc"));
		auto const grid_w = dir.filePath(QStringLiteral("w/test.asc"));
		QVERIFY(QFile::copy(fixture("moved/test.asc"), grid_r));
		QVERIFY(QFile::copy(fixture("moved/test.asc"), grid_w));
		auto const map_path = dir.filePath(QStringLiteral("r/map.foom"));
		{
			Map map;
			georeference(map);
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(map.exportToIODevice(file));
		}

		auto const run = [&](const QJsonArray& steps, QByteArray* out) {
			auto const job_path = dir.filePath(QStringLiteral("r/job.json"));
			QFile file(job_path);
			if (!file.open(QIODevice::WriteOnly))
				return -1;
			file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
			file.close();
			QProcess process;
			auto env = QProcessEnvironment::systemEnvironment();
			env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
			process.setProcessEnvironment(env);
			process.start(executable, { QStringLiteral("--run"), job_path });
			if (!process.waitForFinished(300000))
				return -1;
			if (out)
				*out = process.readAllStandardOutput();
			return process.exitCode();
		};
		auto const open = QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), QStringLiteral("map.foom") } };
		auto wizard = QJsonObject{
		    { QStringLiteral("step"), QStringLiteral("elevation-wizard") },
		    { QStringLiteral("source"), QStringLiteral("grid") },
		    { QStringLiteral("files"), QJsonArray{ QStringLiteral("test.asc") } },
		    { QStringLiteral("products"), QJsonArray{ QStringLiteral("contours"), QStringLiteral("hill-shading") } },
		};
		auto const save = QJsonObject{ { QStringLiteral("step"), QStringLiteral("save-as") }, { QStringLiteral("path"), QStringLiteral("out.foom") } };
		QByteArray out;
		QCOMPARE(run({ open, wizard, save }, &out), 0);
		auto const lines = out.split('\n');
		QVERIFY(lines.size() >= 2);
		auto const line = QJsonDocument::fromJson(lines[1]).object();
		QCOMPARE(line.value(QStringLiteral("step")).toString(), QStringLiteral("elevation-wizard"));
		QCOMPARE(line.value(QStringLiteral("files")).toArray().size(), 1);

		// The same job through the wizard's function.
		ElevationWizardJob job;
		QString error;
		auto params = wizard;
		params.remove(QStringLiteral("step"));  // the runner takes it out before the step's parameters are read
		QVERIFY2(elevationWizardJobFromJson(params, &job, &error), qPrintable(error));
		job.files = QStringList{ grid_w };
		Map expected;
		georeference(expected);
		auto const result = runElevationWizard(expected, dir.filePath(QStringLiteral("w/map.foom")), job);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(line.value(QStringLiteral("contours")).toInt(), result.contours);

		Map saved;
		QVERIFY(saved.loadFrom(dir.filePath(QStringLiteral("r/out.foom"))));
		QCOMPARE(saved.getNumObjects(), result.contours);
		QCOMPARE(QFileInfo(saved.elevationSet().href(ElevationSet::Terrain)).fileName(), QStringLiteral("test.asc"));
		QCOMPARE(saved.getNumTemplates(), 1);
		QVERIFY(sameFile(dir.filePath(QStringLiteral("r/map_hillshade.tif")), dir.filePath(QStringLiteral("w/map_hillshade.tif"))));

		// Malformed jobs run nothing; a missing grid fails the step.
		auto bad = wizard;
		bad.insert(QStringLiteral("products"), QJsonArray{ QStringLiteral("shadows") });
		QCOMPARE(run({ open, bad }, nullptr), 2);
		bad = wizard;
		bad.insert(QStringLiteral("contour_sets"), QJsonArray{ 1 });
		QCOMPARE(run({ open, bad }, nullptr), 2);
		bad = wizard;
		bad.remove(QStringLiteral("files"));
		QCOMPARE(run({ open, bad }, nullptr), 2);
		bad = wizard;
		bad.insert(QStringLiteral("files"), QJsonArray{ QStringLiteral("absent.asc") });
		QCOMPARE(run({ open, bad }, nullptr), 1);
	}
};



QTEST_MAIN(ElevationWizardTest)

#include "elevation_wizard_t.moc"  // IWYU pragma: keep
