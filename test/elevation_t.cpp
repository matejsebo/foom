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


#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// A grid of width by height cells of the given size, left edge x0 and top edge y_top,
/// filled from the function of the cell centre.
std::unique_ptr<ElevationModel> makeGrid(int width, int height, double x0, double y_top, double cell, const std::function<double(double, double)>& f)
{
	std::vector<float> values(std::size_t(width) * std::size_t(height));
	for (int r = 0; r < height; ++r)
		for (int c = 0; c < width; ++c)
			values[std::size_t(r) * std::size_t(width) + std::size_t(c)] = float(f(x0 + (c + 0.5) * cell, y_top - (r + 0.5) * cell));
	return std::make_unique<ElevationModel>(width, height, QPointF(x0, y_top), cell, std::move(values));
}

Georeferencing utm32Georeferencing()
{
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	return georef;
}

}  // namespace


/**
 * @test Tests the elevation module: grids, contours and raster products.
 * See the design notes.
 */
class ElevationTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QString::fromLatin1("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
	}

	void gridBasics()
	{
		auto grid = makeGrid(11, 11, 0.0, 11.0, 1.0, [](double x, double y) { return 100.0 + 0.1 * x + 0.2 * y; });
		QCOMPARE(grid->width(), 11);
		QCOMPARE(grid->height(), 11);
		QCOMPARE(grid->extent(), QRectF(0.0, 0.0, 11.0, 11.0));
		QCOMPARE(grid->cellSize(), 1.0);
		QVERIFY(qAbs(grid->at(QPointF(5.5, 5.5)) - 101.65) < 1e-5);
		QVERIFY(qAbs(grid->at(QPointF(5.0, 5.0)) - 101.5) < 1e-5);
		QVERIFY(std::isnan(grid->at(QPointF(20.0, 5.0))));
		QVERIFY(std::isnan(grid->at(QPointF(5.0, -1.0))));
		auto const g = grid->gradient(QPointF(5.0, 5.0));
		QVERIFY(qAbs(g.x() - 0.1) < 1e-5);
		QVERIFY(qAbs(g.y() - 0.2) < 1e-5);

		auto const info = grid->info();
		QVERIFY(qAbs(info.minimum - grid->at(QPointF(0.5, 0.5))) < 1e-6);
		QVERIFY(qAbs(info.maximum - grid->at(QPointF(10.5, 10.5))) < 1e-6);
		QCOMPARE(info.void_count, qint64(0));
		grid->setValue(3, 4, std::numeric_limits<float>::quiet_NaN());
		QCOMPARE(grid->info().void_count, qint64(1));
		QVERIFY(std::isnan(grid->at(grid->cellCenter(3, 4))));
	}

	void gridFiles()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto grid = makeGrid(11, 11, 0.0, 11.0, 1.0, [](double x, double y) { return 100.0 + 0.1 * x + 0.2 * y; });
		grid->setValue(3, 4, std::numeric_limits<float>::quiet_NaN());
		grid->setCrsSpec(QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));

		// GeoTIFF
		auto const tif = QDir(dir.path()).absoluteFilePath(QStringLiteral("grid.tif"));
		QVERIFY(grid->save(tif));
		auto reopened = ElevationModel::open(tif);
		QVERIFY2(reopened, qPrintable(ElevationModel::lastError()));
		QCOMPARE(reopened->width(), 11);
		QCOMPARE(reopened->height(), 11);
		QCOMPARE(reopened->extent(), grid->extent());
		QCOMPARE(reopened->cellSize(), 1.0);
		for (int r = 0; r < 11; ++r)
			for (int c = 0; c < 11; ++c)
			{
				if (c == 3 && r == 4)
					QVERIFY(std::isnan(reopened->value(c, r)));
				else
					QVERIFY(qAbs(reopened->value(c, r) - grid->value(c, r)) < 1e-6);
			}
		QVERIFY(std::isnan(reopened->at(reopened->cellCenter(3, 4))));
		QVERIFY(!reopened->crsSpec().isEmpty());
		auto const metadata = ElevationModel::geoTiffMetadata(tif);
		QVERIFY2(metadata.contains(QLatin1String("Float32")), qPrintable(metadata));
		QVERIFY2(metadata.contains(QLatin1String("DEFLATE")), qPrintable(metadata));
		QVERIFY2(metadata.contains(QLatin1String("TILED")), qPrintable(metadata));

		// Options
		ElevationModel::OpenOptions options;
		options.feet_to_metres = true;
		auto feet = ElevationModel::open(tif, options);
		QVERIFY(feet);
		QVERIFY(qAbs(feet->value(0, 0) - 0.3048 * grid->value(0, 0)) < 1e-4);

		auto below = makeGrid(11, 11, 0.0, 11.0, 1.0, [](double x, double) { return -5.0 + 0.5 * x; });
		auto const below_tif = QDir(dir.path()).absoluteFilePath(QStringLiteral("below.tif"));
		QVERIFY(below->save(below_tif));
		options = {};
		options.shift_below_sea_level = true;
		auto shifted = ElevationModel::open(below_tif, options);
		QVERIFY(shifted);
		QVERIFY(qAbs(shifted->info().minimum) < 1e-5);
		QVERIFY(qAbs(shifted->value(10, 0) - (below->value(10, 0) + 4.75)) < 1e-4);

		// ESRI ASCII grid, XYZ and OCD grid
		grid->setValue(3, 4, 101.0f);
		struct { const char* name; double tolerance; } const formats[] = {
		    { "grid.asc", 0.001 }, { "grid.xyz", 0.001 }, { "grid.ocdDem", 0.005 } };
		for (auto const& format : formats)
		{
			auto const path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8(format.name));
			QVERIFY2(grid->save(path), format.name);
			auto again = ElevationModel::open(path);
			QVERIFY2(again, qPrintable(QString::fromUtf8(format.name) + QLatin1String(": ") + ElevationModel::lastError()));
			QCOMPARE(again->width(), 11);
			QCOMPARE(again->height(), 11);
			QVERIFY2(qAbs(again->extent().left() - 0.0) < 1e-6 && qAbs(again->extent().top() - 0.0) < 1e-6
			         && qAbs(again->extent().width() - 11.0) < 1e-6 && qAbs(again->extent().height() - 11.0) < 1e-6, format.name);
			for (int r = 0; r < 11; ++r)
				for (int c = 0; c < 11; ++c)
					QVERIFY2(qAbs(again->value(c, r) - grid->value(c, r)) <= format.tolerance, format.name);
		}
	}

	void utilities()
	{
		auto a = makeGrid(11, 11, 0.0, 11.0, 1.0, [](double, double) { return 1.0; });
		auto b = makeGrid(11, 11, 5.0, 11.0, 1.0, [](double, double) { return 2.0; });

		auto merged = ElevationModel::merge({ a.get(), b.get() });
		QVERIFY(merged);
		QCOMPARE(merged->extent(), QRectF(0.0, 0.0, 16.0, 11.0));
		QCOMPARE(merged->info().void_count, qint64(0));
		QCOMPARE(merged->at(QPointF(2.0, 5.0)), 1.0);
		QCOMPARE(merged->at(QPointF(8.0, 5.0)), 2.0);
		QCOMPARE(merged->at(QPointF(14.0, 5.0)), 2.0);

		auto difference = ElevationModel::difference(*b, *a);
		QVERIFY(difference);
		QCOMPARE(difference->extent(), QRectF(5.0, 0.0, 6.0, 11.0));
		for (int r = 0; r < difference->height(); ++r)
			for (int c = 0; c < difference->width(); ++c)
				QCOMPARE(difference->value(c, r), 1.0f);

		auto cropped = a->cropped(QRectF(2.0, 2.0, 3.0, 3.0));
		QVERIFY(cropped);
		QCOMPARE(cropped->width(), 3);
		QCOMPARE(cropped->height(), 3);
		QCOMPARE(cropped->extent(), QRectF(2.0, 2.0, 3.0, 3.0));
		QCOMPARE(cropped->value(1, 1), 1.0f);

		auto smoothed = a->smoothed(1);
		for (int r = 0; r < 11; ++r)
			for (int c = 0; c < 11; ++c)
				QCOMPARE(smoothed->value(c, r), 1.0f);

		QCOMPARE(a->info().minimum, 1.0);
		QCOMPARE(a->info().maximum, 1.0);
	}

	void xmlRoundTrip()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const* format = FileFormats.findFormat("XML");
		QVERIFY(format);
		auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("map.xmap"));

		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			QVERIFY(map.elevationSet().isEmpty());
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QVERIFY(!file.readAll().contains("<elevation"));
		}

		auto grid = makeGrid(11, 11, 500000.0, 5000011.0, 1.0, [](double, double) { return 100.0; });
		auto const grid_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("dtm.tif"));
		QVERIFY(grid->save(grid_path));
		auto const other_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("sub/dsm.tif"));
		QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
		QVERIFY(grid->save(other_path));
		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			map.elevationSet().setHref(ElevationSet::Terrain, grid_path);
			map.elevationSet().setHref(ElevationSet::Surface, other_path);
			QVERIFY(!map.elevationSet().isEmpty());
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			auto const text = QString::fromUtf8(file.readAll());
			QVERIFY2(text.contains(QLatin1String("<elevation href=\"dtm.tif\" kind=\"terrain\"/>")), qPrintable(text.mid(text.indexOf(QLatin1String("<elevation")), 120)));
			QVERIFY(text.contains(QLatin1String("<elevation href=\"sub/dsm.tif\" kind=\"surface\"/>")));
		}
		{
			Map map;
			auto importer = format->makeImporter(map_path, &map, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(importer->warnings().empty());
			QCOMPARE(map.elevationSet().href(ElevationSet::Terrain), QFileInfo(grid_path).absoluteFilePath());
			QCOMPARE(map.elevationSet().href(ElevationSet::Surface), QFileInfo(other_path).absoluteFilePath());
			QVERIFY(map.elevationSet().href(ElevationSet::Canopy).isEmpty());
			auto const* model = map.elevationSet().model(ElevationSet::Terrain, map.getGeoreferencing().getProjectedCRSSpec());
			QVERIFY2(model, qPrintable(ElevationModel::lastError()));
			QCOMPARE(model->width(), 11);
			QCOMPARE(model->at(QPointF(500005.0, 5000005.0)), 100.0);
			map.clear();
			QVERIFY(map.elevationSet().isEmpty());
		}
	}
};


QTEST_MAIN(ElevationTest)


#include "elevation_t.moc"  // IWYU pragma: keep
