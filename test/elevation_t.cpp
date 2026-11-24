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


#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QImage>
#include <QPainter>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogr_srs_api.h>

#include "global.h"
#include "core/foreign_data.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/area_symbol.h"
#include "elevation/cliffs.h"
#include "elevation/contours.h"
#include "elevation/elevation_model.h"
#include "elevation/embedded_grid.h"
#include "elevation/ocd_course_dem.h"
#include "elevation/grid_frame.h"
#include "elevation/elevation_set.h"
#include "elevation/landforms.h"
#include "elevation/profile.h"
#include "elevation/raster_products.h"
#include "elevation/trees.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "templates/template.h"
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

std::unique_ptr<ElevationModel> cone(double x0 = 0.0, double y0 = 0.0, bool inverted = false)
{
	auto const cx = x0 + 50.5, cy = y0 + 50.5;
	return makeGrid(101, 101, x0, y0 + 101.0, 1.0, [cx, cy, inverted](double x, double y) {
		auto const r = std::hypot(x - cx, y - cy);
		return inverted ? 40.0 + 0.5 * r : 60.0 - 0.5 * r;
	});
}

double signedArea(const std::vector<QPointF>& points)
{
	double a = 0.0;
	for (std::size_t i = 0, n = points.size(); i < n; ++i)
	{
		auto const& p = points[i];
		auto const& q = points[(i + 1) % n];
		a += p.x() * q.y() - q.x() * p.y();
	}
	return 0.5 * a;
}

double length(const std::vector<QPointF>& points, bool closed)
{
	double l = 0.0;
	for (std::size_t i = 1; i < points.size(); ++i)
		l += std::hypot(points[i].x() - points[i-1].x(), points[i].y() - points[i-1].y());
	if (closed && points.size() > 1)
		l += std::hypot(points.front().x() - points.back().x(), points.front().y() - points.back().y());
	return l;
}

/// Steps, steep ridges and blocks on flat ground at 400 m.
std::unique_ptr<ElevationModel> cliffsGrid()
{
	auto const x0 = 966000.0;
	auto const top = 557230.0;
	return makeGrid(330, 230, x0, top, 1.0, [x0, top](double px, double py) {
		auto const x = px - x0;
		auto const y = top - py;
		auto z = 400.0;
		if (20 <= y && y < 60)
		{
			double const heights[] = { 3.0, 3.25, 3.5, 3.75, 4.0, 4.5 };
			for (int i = 0; i < 6; ++i)
			{
				auto const s = 20.0 + 50 * i;
				if (s <= x && x < s + 20)
					z += heights[i];
				else if (s + 20 <= x && x < s + 45)
					z += heights[i] * (1 - (x - s - 20) / 25);
			}
		}
		if (90 <= y && y < 130)
		{
			double const angles[] = { 40, 43, 44, 46, 47, 50 };
			for (int i = 0; i < 6; ++i)
			{
				auto const s = 20.0 + 50 * i;
				auto const rise = 10 * std::tan(qDegreesToRadians(angles[i]));
				if (s <= x && x < s + 10)
					z += (x - s) * rise / 10;
				else if (s + 10 <= x && x < s + 20)
					z += rise;
				else if (s + 20 <= x && x < s + 30)
					z += rise * (1 - (x - s - 20) / 10);
			}
		}
		int const lengths[] = { 1, 2, 3, 4, 6 };
		for (int i = 0; i < 5; ++i)
		{
			auto const s = 30.0 + 60 * i;
			if (s <= x && x < s + 10 && 180 <= y && y < 180 + lengths[i])
				z += 8.0;
		}
		// The heights of the .asc file, in millimetres.
		return std::round(z * 1000.0) / 1000.0;
	});
}

ContourParams standardParams()
{
	ContourParams params;
	params.interval = 1.0;
	params.main_interval = 5.0;
	params.index_interval = 25.0;
	return params;
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

QString tagValue(const Object* object, const char* key)
{
	auto const it = object->tags().find(QString::fromLatin1(key));
	return it == object->tags().end() ? QString{} : it->value;
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

	void contourCone()
	{
		auto grid = cone();
		auto lines = traceContours(*grid, standardParams());
		std::map<int, int> count;
		for (auto const& line : lines)
			++count[qRound(line.level)];
		for (int level = 35; level <= 59; ++level)
		{
			QVERIFY2(count[level] == 1, qPrintable(QString::number(level) + QLatin1String(": ") + QString::number(count[level])));
			auto const it = std::find_if(lines.begin(), lines.end(), [level](const ContourLine& l) { return qRound(l.level) == level; });
			QVERIFY2(it->closed, qPrintable(QString::number(level)));
			QVERIFY(!it->depression);
			QVERIFY(it->points.size() >= 4);
			auto const radius = 2.0 * (60.0 - level);
			for (auto const& p : it->points)
				QVERIFY2(qAbs(std::hypot(p.x() - 50.5, p.y() - 50.5) - radius) < 0.5, qPrintable(QString::number(level)));
			QVERIFY(signedArea(it->points) > 0.0);
			QCOMPARE(it->tag, QString::number(level));
		}
		auto classOf = [&lines](int level) {
			return std::find_if(lines.begin(), lines.end(), [level](const ContourLine& l) { return qRound(l.level) == level; })->cls;
		};
		QCOMPARE(classOf(50), ContourLine::Index);
		QCOMPARE(classOf(55), ContourLine::Main);
		QCOMPARE(classOf(59), ContourLine::Smallest);
		// Levels which reach the border are open.
		for (auto const& line : lines)
			if (line.level < 34.5)
				QVERIFY(!line.closed);
	}

	void contourInvertedCone()
	{
		auto grid = cone(0.0, 0.0, true);
		auto lines = traceContours(*grid, standardParams());
		std::map<int, int> count;
		for (auto const& line : lines)
			++count[qRound(line.level)];
		for (int level = 41; level <= 65; ++level)
		{
			QCOMPARE(count[level], 1);
			auto const it = std::find_if(lines.begin(), lines.end(), [level](const ContourLine& l) { return qRound(l.level) == level; });
			QVERIFY(it->closed);
			QVERIFY(it->depression);
			QVERIFY(signedArea(it->points) < 0.0);
		}
	}

	void contourPlane()
	{
		auto grid = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double x, double) { return 100.0 + 0.1 * x; });
		auto lines = traceContours(*grid, standardParams());
		std::map<int, int> count;
		for (auto const& line : lines)
			++count[qRound(line.level)];
		for (int level = 101; level <= 110; ++level)
		{
			QCOMPARE(count[level], 1);
			auto const it = std::find_if(lines.begin(), lines.end(), [level](const ContourLine& l) { return qRound(l.level) == level; });
			QVERIFY(!it->closed);
			QVERIFY(it->points.size() >= 2);
			for (auto const& p : it->points)
				QVERIFY2(qAbs(p.x() - 10.0 * (level - 100)) < 1e-6, qPrintable(QString::number(p.x())));
			QVERIFY(it->points.front().y() > it->points.back().y());
		}
	}

	void contourTiles()
	{
		auto grid = cone();
		auto whole = traceContours(*grid, standardParams());
		auto params = standardParams();
		params.tile_size = 40;
		auto tiled = traceContours(*grid, params);
		std::map<int, std::pair<int, double>> whole_stats, tiled_stats;
		for (auto const& line : whole)
		{
			auto& s = whole_stats[qRound(line.level)];
			++s.first;
			s.second += length(line.points, line.closed);
		}
		for (auto const& line : tiled)
		{
			auto& s = tiled_stats[qRound(line.level)];
			++s.first;
			s.second += length(line.points, line.closed);
		}
		QCOMPARE(tiled_stats.size(), whole_stats.size());
		for (auto const& item : whole_stats)
		{
			QVERIFY2(tiled_stats[item.first].first == item.second.first, qPrintable(QString::number(item.first) + QLatin1String(": ") + QString::number(tiled_stats[item.first].first) + QLatin1String(" vs ") + QString::number(item.second.first)));
			QVERIFY2(qAbs(tiled_stats[item.first].second - item.second.second) < 1e-6, qPrintable(QString::number(item.first)));
		}
	}

	/// The brown of the contour symbols goes to the top of the colour table.
	/// The objects which are there already must keep their colours.
	void contourSymbolsKeepColours()
	{
		Map map;
		map.setScaleDenominator(10000);
		auto* green = new MapColor(QStringLiteral("Green"), 0);
		green->setCmyk(MapColorCmyk(1.0f, 0.0f, 1.0f, 0.0f));
		green->setRgbFromCmyk();
		map.addColor(green, 0);
		auto* line = new LineSymbol();
		line->setLineWidth(2.0);
		line->setColor(green);
		map.addSymbol(line, 0);
		map.addObject(new PathObject(line, { MapCoord(5, 5), MapCoord(35, 5) }));
		
		// The area from (0, 0) to (40, 10) mm at 5 pixels per mm.
		auto const render = [&map]() {
			QImage image(200, 50, QImage::Format_RGB32);
			image.fill(QColor(Qt::white));
			QPainter painter(&image);
			painter.scale(5, 5);
			RenderConfig config = { map, QRectF(0, 0, 40, 10), 5, RenderConfig::NoOptions, 1.0 };
			map.draw(&painter, config);
			return image;
		};
		auto const before = render().pixelColor(100, 25);
		QVERIFY(before.green() > 200 && before.red() < 60);
		
		contourSymbols(map, false);
		QCOMPARE(map.getNumColors(), 2);
		QVERIFY(map.getColor(0) != green);
		QCOMPARE(render().pixelColor(100, 25), before);
	}
	
	void contourObjects()
	{
		Map map;
		map.setGeoreferencing(utm32Georeferencing());
		map.setScaleDenominator(10000);
		auto grid = cone(500000.0, 5000000.0);

		auto const symbols = contourSymbols(map, false);
		QVERIFY(symbols.hill[0] && symbols.hill[1] && symbols.hill[2]);
		QVERIFY(symbols.depression[0] && symbols.depression[1] && symbols.depression[2]);
		QCOMPARE(map.getNumSymbols(), 6);
		QCOMPARE(symbols.hill[0]->getNumberComponent(0), 10050);
		QCOMPARE(symbols.hill[2]->getNumberComponent(0), 10052);
		QCOMPARE(symbols.depression[1]->getNumberComponent(1), 1);
		auto const again = contourSymbols(map, false);
		QCOMPARE(map.getNumSymbols(), 6);
		QCOMPARE(again.hill[1], symbols.hill[1]);

		auto result = makeContourObjects(map, *grid, standardParams(), symbols);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		auto const num_objects = int(result.objects.size());
		QVERIFY(num_objects >= 25);
		int closed_lines = 0;
		for (auto const& object : result.objects)
		{
			QCOMPARE(object->getType(), Object::Path);
			QVERIFY(!tagValue(object.get(), "ele").isEmpty());
			auto const* path = object->asPath();
			if (path->parts().front().isClosed())
			{
				++closed_lines;
				auto const level = tagValue(object.get(), "ele").toDouble();
				auto const radius_mm = 2.0 * (60.0 - level) / 10.0;
				QVERIFY2(level >= 35.0 && level <= 59.0, qPrintable(QString::number(level)));
				for (auto const& coord : path->getRawCoordinateVector())
					QVERIFY2(qAbs(std::hypot(coord.x() - 5.05, coord.y() + 5.05) - radius_mm) < 0.05, qPrintable(QString::number(level)));
				if (qRound(level) == 50)
					QCOMPARE(object->getSymbol(), symbols.hill[2]);
				else if (qRound(level) == 55)
					QCOMPARE(object->getSymbol(), symbols.hill[1]);
				else if (qRound(level) == 59)
					QCOMPARE(object->getSymbol(), symbols.hill[0]);
			}
		}
		QCOMPARE(closed_lines, 25);

		QCOMPARE(map.getNumObjects(), 0);
		QVERIFY(addContours(map, std::move(result.objects)));
		QCOMPARE(map.getNumObjects(), num_objects);
		QVERIFY(map.undoManager().canUndo());
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumObjects(), 0);
		QVERIFY(map.undoManager().redo());
		QCOMPARE(map.getNumObjects(), num_objects);

		// The smoothed set with curves.
		auto params = standardParams();
		params.smoothed = true;
		params.curves = true;
		auto const smoothed_symbols = contourSymbols(map, true);
		QCOMPARE(map.getNumSymbols(), 12);
		QCOMPARE(smoothed_symbols.hill[0]->getNumberComponent(0), 10060);
		auto smoothed = makeContourObjects(map, *grid, params, smoothed_symbols);
		QVERIFY2(smoothed.error.isEmpty(), qPrintable(smoothed.error));
		int curved = 0;
		for (auto const& object : smoothed.objects)
		{
			auto const* path = object->asPath();
			auto const level = tagValue(object.get(), "ele").toDouble();
			bool has_curve = false;
			for (auto const& coord : path->getRawCoordinateVector())
				has_curve = has_curve || coord.isCurveStart();
			if (has_curve)
				++curved;
			if (path->parts().front().isClosed())
			{
				auto const radius_mm = 2.0 * (60.0 - level) / 10.0;
				for (auto const& coord : path->getRawCoordinateVector())
					if (!coord.isCurveStart() && &coord != &path->getRawCoordinateVector().back())
						QVERIFY2(qAbs(std::hypot(coord.x() - 5.05, coord.y() + 5.05) - radius_mm) < 0.05, qPrintable(QString::number(level)));
			}
		}
		QVERIFY(curved > 0);
	}

	void hillShadeTest()
	{
		auto inclined = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 + 0.1 * y; });  // rises to the north: faces south
		auto flat = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double) { return 100.0; });

		HillShadeParams params;
		params.gradient = GradientMethod::Horn;  // The lattice of the cells; Corners: cornerGradientTest().
		params.method = HillShadeParams::Oblique;
		params.azimuth_deg = 315.0;
		params.elevation_deg = 45.0;
		params.exaggeration = 4.0;
		struct { HillShadeParams::Method method; bool multi; int expected; } const cases[] = {
		    { HillShadeParams::Oblique, false, 120 },
		    { HillShadeParams::Oblique, true, 167 },
		    { HillShadeParams::SlopeShading, false, 193 },
		    { HillShadeParams::Combined, false, 91 } };
		for (auto const& c : cases)
		{
			params.method = c.method;
			params.multi_directional = c.multi;
			auto product = OpenOrienteering::hillShade(*inclined, params);
			QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
			QCOMPARE(product.image.size(), QSize(101, 101));
			QCOMPARE(product.image.format(), QImage::Format_Grayscale8);
			QCOMPARE(product.extent, inclined->extent());
			for (int r = 1; r < 100; ++r)
				for (int col = 1; col < 100; ++col)
					QVERIFY2(qAbs(int(product.image.constScanLine(r)[col]) - c.expected) <= 1,
					         qPrintable(QString::number(c.method) + QLatin1Char(' ') + QString::number(product.image.constScanLine(r)[col])));
		}
		params.method = HillShadeParams::Oblique;
		params.multi_directional = false;
		auto flat_product = OpenOrienteering::hillShade(*flat, params);
		QVERIFY(qAbs(int(flat_product.image.constScanLine(50)[50]) - 180) <= 1);
		params.method = HillShadeParams::SlopeShading;
		QCOMPARE(int(OpenOrienteering::hillShade(*flat, params).image.constScanLine(50)[50]), 255);
	}

	void slopeRasterTest()
	{
		auto inclined = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 - 0.1 * y; });
		auto steep = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 + 1.7320508 * y; });
		auto flat = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double) { return 100.0; });

		SlopeParams params;
		params.gradient = GradientMethod::Horn;
		params.binary = false;
		params.threshold_deg = 45.0;
		auto product = OpenOrienteering::slopeRaster(*inclined, params);
		QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
		QCOMPARE(product.image.size(), QSize(101, 101));
		for (int r = 1; r < 100; ++r)
			for (int col = 1; col < 100; ++col)
				QVERIFY2(qAbs(int(product.image.constScanLine(r)[col]) - 229) <= 1, qPrintable(QString::number(product.image.constScanLine(r)[col])));
		// At and above the threshold the raster is dark grey, not black (2026-11-23).
		QCOMPARE(SlopeParams().darkest, 0.2);
		QCOMPARE(int(OpenOrienteering::slopeRaster(*steep, params).image.constScanLine(50)[50]), 51);
		params.darkest = 0.0;
		QCOMPARE(int(OpenOrienteering::slopeRaster(*steep, params).image.constScanLine(50)[50]), 0);
		QVERIFY(qAbs(int(OpenOrienteering::slopeRaster(*inclined, params).image.constScanLine(50)[50]) - 223) <= 1);
		params.darkest = 0.2;
		QCOMPARE(int(OpenOrienteering::slopeRaster(*flat, params).image.constScanLine(50)[50]), 255);
		params.binary = true;
		QCOMPARE(int(OpenOrienteering::slopeRaster(*inclined, params).image.constScanLine(50)[50]), 255);
		QCOMPARE(int(OpenOrienteering::slopeRaster(*steep, params).image.constScanLine(50)[50]), 0);
	}

	void hypsometricTest()
	{
		// the design notes, "Hypsometric map", Accept (1) to (3).
		auto const rising = makeGrid(101, 3, 0.0, 3.0, 1.0, [](double x, double) { return 100.0 + (x - 0.5); });
		auto const near = [](QRgb a, QRgb b) {
			return std::abs(qRed(a) - qRed(b)) <= 1 && std::abs(qGreen(a) - qGreen(b)) <= 1 && std::abs(qBlue(a) - qBlue(b)) <= 1;
		};
		auto const pixel = [](const RasterProduct& p, int col) { return p.image.pixel(col, 1); };
		HypsometricParams params;
		QCOMPARE(params.ramp, HypsometricParams::BrownYellowGreen);
		auto product = hypsometricRaster(*rising, params);
		QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
		QCOMPARE(product.image.format(), QImage::Format_RGB32);
		QCOMPARE(product.image.size(), QSize(101, 3));
		QCOMPARE(product.extent, rising->extent());
		QVERIFY(near(pixel(product, 0), qRgb(0, 160, 0)));
		QVERIFY(near(pixel(product, 50), qRgb(255, 255, 0)));
		QVERIFY(near(pixel(product, 100), qRgb(153, 102, 51)));
		QVERIFY2(near(pixel(product, 25), qRgb(128, 208, 0)), qPrintable(QString::number(pixel(product, 25), 16)));
	
		params.ramp = HypsometricParams::Greyscale;
		product = hypsometricRaster(*rising, params);
		QVERIFY(near(pixel(product, 0), qRgb(0, 0, 0)));
		QVERIFY(near(pixel(product, 50), qRgb(128, 128, 128)));
		QVERIFY(near(pixel(product, 100), qRgb(255, 255, 255)));
		params.ramp = HypsometricParams::RedGreenBlue;
		product = hypsometricRaster(*rising, params);
		QVERIFY(near(pixel(product, 0), qRgb(0, 0, 255)));
		QVERIFY(near(pixel(product, 50), qRgb(0, 160, 0)));
		QVERIFY(near(pixel(product, 100), qRgb(255, 0, 0)));
	
		params.ramp = HypsometricParams::GreyscaleInterval;
		params.interval = 100.0;
		product = hypsometricRaster(*rising, params);
		QVERIFY(near(pixel(product, 50), qRgb(128, 128, 128)));
		QCOMPARE(pixel(product, 0), qRgb(0, 0, 0));
		QCOMPARE(pixel(product, 100), qRgb(0, 0, 0));
		QVERIFY(qRed(pixel(product, 99)) >= 250);
	
		auto zero = makeGrid(3, 1, 0.0, 1.0, 1.0, [](double x, double) { return x < 1.0 ? 0.0 : 50.0; });
		zero->setValue(2, 0, std::numeric_limits<float>::quiet_NaN());
		params.ramp = HypsometricParams::BrownYellowGreen;
		params.white_at_zero = true;
		product = hypsometricRaster(*zero, params);
		QCOMPARE(product.image.pixel(0, 0), qRgb(255, 255, 255));
		QVERIFY(product.image.pixel(1, 0) != qRgb(255, 255, 255));
		QCOMPARE(product.image.pixel(2, 0), qRgb(0, 0, 0));
		params.white_at_zero = false;
		QVERIFY(hypsometricRaster(*zero, params).image.pixel(0, 0) != qRgb(255, 255, 255));
	}

	void profileTest()
	{
		// the design notes, "Profile", Accept (1) to (4).
		auto const plane = makeGrid(200, 50, 0.0, 50.0, 1.0, [](double x, double) { return 100.0 + 0.1 * x; });
		std::vector<QPointF> const line = { QPointF(20.5, 25.0), QPointF(120.5, 25.0) };
		auto profile = elevationProfile(*plane, line);
		QCOMPARE(int(profile.samples.size()), 101);
		for (std::size_t k = 0; k < profile.samples.size(); ++k)
		{
			auto const& s = profile.samples[k];
			QVERIFY(std::abs(s.distance - double(k)) < 1e-9);
			QVERIFY(std::abs(s.height - (100.0 + 0.1 * s.projected.x())) < 0.001);
		}
		QVERIFY(std::abs(profile.length - 100.0) < 1e-9);
		QVERIFY(std::abs(profile.start - 102.05) < 0.001);
		QVERIFY(std::abs(profile.end - 112.05) < 0.001);
		QCOMPARE(profile.minimum, profile.start);
		QCOMPARE(profile.maximum, profile.end);
		QVERIFY2(profile.ascent > 9.0 && profile.ascent <= 10.0 + 1e-6, qPrintable(QString::number(profile.ascent)));
		QCOMPARE(profile.descent, 0.0);
		QCOMPARE(int(elevationProfile(*plane, line, 2.0).samples.size()), 51);
	
		auto const ripples = [](double amplitude) {
			return makeGrid(200, 50, 0.0, 50.0, 1.0, [amplitude](double x, double) { return 100.0 + amplitude * std::sin(x / 3.0); });
		};
		auto const small = elevationProfile(*ripples(0.4), line);
		QCOMPARE(small.ascent, 0.0);
		QCOMPARE(small.descent, 0.0);
		auto const large = elevationProfile(*ripples(2.0), line);
		QVERIFY(large.ascent > 0.0 && large.descent > 0.0);
	
		auto holes = makeGrid(200, 50, 0.0, 50.0, 1.0, [](double x, double) { return 100.0 + 0.1 * x; });
		for (int row = 0; row < 50; ++row)
		{
			holes->setValue(70, row, std::numeric_limits<float>::quiet_NaN());
			holes->setValue(71, row, std::numeric_limits<float>::quiet_NaN());
		}
		auto const gappy = elevationProfile(*holes, line);
		auto const voids = std::count_if(gappy.samples.begin(), gappy.samples.end(), [](const ProfileSample& s) { return std::isnan(s.height); });
		QVERIFY(voids >= 1);
		QVERIFY(std::abs(gappy.start - 102.05) < 0.001);
		QVERIFY(std::abs(gappy.end - 112.05) < 0.001);
	
		QTemporaryDir dir;
		auto const csv = dir.filePath(QStringLiteral("profile.csv"));
		QVERIFY(writeProfileCsv(gappy, csv));
		QFile file(csv);
		QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
		auto const lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), QString::SkipEmptyParts);
		QCOMPARE(lines.front(), QStringLiteral("distance;easting;northing;height"));
		QCOMPARE(lines.size(), int(gappy.samples.size()) + 1);
		QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString& l) { return l.endsWith(QLatin1Char(';')); }));
		QCOMPARE(lines.at(1), QStringLiteral("0.00;20.50;25.00;102.05"));
	}

	void productTemplate()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Map map;
		map.setGeoreferencing(utm32Georeferencing());
		auto grid = makeGrid(101, 101, 500000.0, 5000101.0, 1.0, [](double, double y) { return 100.0 - 0.1 * (y - 5000000.0); });
		grid->setCrsSpec(map.getGeoreferencing().getProjectedCRSSpec());

		HillShadeParams params;
		params.gradient = GradientMethod::Horn;  // The grid's extent.
		auto product = hillShade(*grid, params);
		QVERIFY(product.error.isEmpty());
		QCOMPARE(product.crs_spec, grid->crsSpec());
		auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("hillshade.tif"));
		QString error;
		QVERIFY2(writeGeoTiff(product, path, &error), qPrintable(error));
		auto reopened = ElevationModel::open(path);
		QVERIFY2(reopened, qPrintable(ElevationModel::lastError()));
		QCOMPARE(reopened->width(), 101);
		QCOMPARE(reopened->extent(), grid->extent());
		QVERIFY(qAbs(reopened->value(50, 50) - product.image.constScanLine(50)[50]) < 1e-6);
		QVERIFY(!reopened->crsSpec().isEmpty());

		QCOMPARE(map.getNumTemplates(), 0);
		auto* temp = attachProductTemplate(map, path, &error);
		QVERIFY2(temp, qPrintable(error));
		QCOMPARE(map.getNumTemplates(), 1);
		QCOMPARE(map.getTemplate(0), temp);
		QCOMPARE(temp->getTemplateState(), Template::Loaded);
		QVERIFY(temp->isTemplateGeoreferenced());
		auto const& georef = map.getGeoreferencing();
		auto const top_left = georef.toMapCoordF(QPointF(500000.0, 5000101.0));
		auto const bottom_right = georef.toMapCoordF(QPointF(500101.0, 5000000.0));
		auto const bbox = temp->calculateTemplateBoundingBox();
		QVERIFY2(qAbs(bbox.left() - top_left.x()) < 0.01, qPrintable(QString::number(bbox.left())));
		QVERIFY2(qAbs(bbox.top() - top_left.y()) < 0.01, qPrintable(QString::number(bbox.top())));
		QVERIFY2(qAbs(bbox.right() - bottom_right.x()) < 0.01, qPrintable(QString::number(bbox.right())));
		QVERIFY2(qAbs(bbox.bottom() - bottom_right.y()) < 0.01, qPrintable(QString::number(bbox.bottom())));
	}

	/// the design notes, "The embedded grid for course setting", Accept (1) and (2).
	void embeddedGridCodec()
	{
		// Heights from 400 to 4000 m, one void cell, a system.
		auto grid = makeGrid(60, 40, 950000.0, 500040.0, 1.0, [](double x, double y) { return 400.0 + 1.5 * (x - 950000.0) * (y - 500000.0); });
		grid->setValue(7, 3, std::numeric_limits<float>::quiet_NaN());
		grid->setCrsSpec(QStringLiteral("EPSG:26954"));
		auto const text = encodeEmbeddedGrid(*grid);
		QVERIFY(!text.isEmpty());
		auto const base64 = QByteArray("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=");
		for (auto c : text)
			QVERIFY(base64.contains(c));
		QString error;
		auto const decoded = decodeEmbeddedGrid(text, &error);
		QVERIFY2(decoded, qPrintable(error));
		QCOMPARE(decoded->width(), 60);
		QCOMPARE(decoded->height(), 40);
		QCOMPARE(decoded->extent(), grid->extent());
		QCOMPARE(decoded->cellSize(), 1.0);
		QCOMPARE(decoded->crsSpec(), QStringLiteral("EPSG:26954"));
		auto worst = 0.0;
		for (int r = 0; r < 40; ++r)
		{
			for (int c = 0; c < 60; ++c)
			{
				if (c == 7 && r == 3)
				{
					QVERIFY(std::isnan(decoded->value(c, r)));
					continue;
				}
				worst = std::max(worst, std::abs(double(decoded->value(c, r)) - double(grid->value(c, r))));
			}
		}
		QVERIFY2(worst <= 0.0501, qPrintable(QString::number(worst)));

		// Smaller than the grid as float32, and a smooth grid far smaller.
		QVERIFY(text.size() < 60 * 40 * 4);
		auto const smooth = makeGrid(200, 200, 0.0, 200.0, 1.0, [](double x, double y) { return 500.0 + 0.1 * x + 0.05 * y; });
		QVERIFY(encodeEmbeddedGrid(*smooth).size() < 200 * 200 / 4);

		// Accept (2): damaged text.
		QVERIFY(!decodeEmbeddedGrid(QByteArray("not a grid"), &error));
		QVERIFY(!error.isEmpty());
		auto truncated = text.left(text.size() / 2);
		QVERIFY(!decodeEmbeddedGrid(truncated, &error));
	}

	/// the design notes, "The embedded grid for course setting", Accept (3) and (4).
	void embeddedGridXml()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const* format = FileFormats.findFormat("XML");
		QVERIFY(format);
		auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("map.xmap"));
		auto grid = makeGrid(11, 11, 500000.0, 5000011.0, 1.0, [](double x, double) { return 100.0 + 0.1 * (x - 500000.0); });
		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			map.elevationSet().setEmbeddedGrid(std::make_unique<ElevationModel>(*grid));
			QVERIFY(map.elevationSet().embeddedGrid());
			QVERIFY(map.elevationSet().isEmpty());  // no grid file
			// A copy of the set keeps it.
			ElevationSet copy(map.elevationSet());
			QVERIFY(copy.embeddedGrid());
			QCOMPARE(copy.embeddedGrid()->width(), 11);
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			auto const text = file.readAll();
			QVERIFY(text.contains("<embedded_elevation>"));
			QVERIFY(!text.contains("<elevation "));
		}
		{
			Map map;
			auto importer = format->makeImporter(map_path, &map, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(importer->warnings().empty());
			auto const* embedded = map.elevationSet().embeddedGrid();
			QVERIFY(embedded);
			QCOMPARE(embedded->extent(), grid->extent());
			QVERIFY(std::abs(embedded->at(QPointF(500005.5, 5000005.5)) - grid->at(QPointF(500005.5, 5000005.5))) <= 0.0501);
			map.clear();
			QVERIFY(!map.elevationSet().embeddedGrid());
		}
	}

	/// "The embedded DEM in OCD files", Accept (5) to (7).
	void ocdCourseDemInOcdFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const* format = FileFormats.findFormat("OCD2018");
		QVERIFY(format);
		auto const map_path = QDir(dir.path()).absoluteFilePath(QStringLiteral("map.ocd"));
		auto grid = makeGrid(40, 30, 500000.0, 5000060.0, 2.0, [](double x, double y) { return 300.0 + 0.2 * (x - 500000.0) + 0.05 * (y - 5000000.0); });
		grid->setValue(4, 9, std::numeric_limits<float>::quiet_NaN());
		auto const same = [&grid](const ElevationModel* embedded) {
			if (!embedded || embedded->extent() != grid->extent() || embedded->width() != 40 || !std::isnan(embedded->value(4, 9)))
				return false;
			for (int row = 0; row < 30; ++row)
				for (int col = 0; col < 40; ++col)
					if (!(row == 9 && col == 4) && !(std::abs(embedded->value(col, row) - grid->value(col, row)) <= 0.0051f))
						return false;
			return true;
		};

		// (5) Written and read back.
		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			map.elevationSet().setEmbeddedGrid(std::make_unique<ElevationModel>(*grid));
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			auto const bytes = file.readAll();
			// The block is the end of the file, and the string says where it is.
			auto const block = encodeOcdCourseDem(*grid);
			QVERIFY(bytes.endsWith(block));
			QByteArray const place = QByteArray("\tP") + QByteArray::number(bytes.size() - block.size()) + "\tS" + QByteArray::number(block.size()) + "\te1";
			QVERIFY2(bytes.contains(place) || bytes.contains(QByteArray("\tP0") + place.mid(2)) || bytes.contains(QByteArray("\tP00") + place.mid(2)), place.constData());
		}
		{
			Map map;
			auto importer = format->makeImporter(map_path, &map, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(same(map.elevationSet().embeddedGrid()));
			QVERIFY(map.elevationSet().isEmpty());
			// The string which only carried the block is not kept a second time.
			for (auto const& string : map.foreignData().strings)
				QVERIFY(string.type != ocd_dem_string_type);

			// (6) With the string of a loaded DEM kept from an import: the name stays, the place is renewed.
			auto foreign = map.foreignData();
			foreign.format = QStringLiteral("OCD");
			foreign.version = 2018;
			ForeignString string;
			string.type = ocd_dem_string_type;
			string.data = QByteArray("C:\\maps\\hillside.ocdDem\tl1\tP12\tS34\te1\0", 37);
			foreign.strings.push_back(string);
			map.setForeignData(foreign);
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
		}
		{
			Map map;
			auto importer = format->makeImporter(map_path, &map, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(same(map.elevationSet().embeddedGrid()));
			auto count = 0;
			for (auto const& string : map.foreignData().strings)
			{
				if (string.type != ocd_dem_string_type)
					continue;
				++count;
				QCOMPARE(string.data, QByteArray("C:\\maps\\hillside.ocdDem\tl1\0", 27));
			}
			QCOMPARE(count, 1);

			// (7) Without the grid the string goes back as it is; version 12 cannot hold the grid.
			map.elevationSet().setEmbeddedGrid(nullptr);
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QFile file(map_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			auto const bytes = file.readAll();
			QVERIFY(bytes.contains(QByteArray("C:\\maps\\hillside.ocdDem\tl1\0", 27)));
			QVERIFY(!bytes.contains("\te1"));
		}
		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			map.elevationSet().setEmbeddedGrid(std::make_unique<ElevationModel>(*grid));
			auto const* v12 = FileFormats.findFormat("OCD12");
			QVERIFY(v12);
			auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("v12.ocd"));
			auto exporter = v12->makeExporter(path, &map, nullptr);
			QVERIFY(exporter->doExport());
			QVERIFY(!exporter->warnings().empty());
			Map read;
			auto importer = v12->makeImporter(path, &read, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(!read.elevationSet().embeddedGrid());
		}
		// A place outside the file is a warning and no grid.
		{
			Map map;
			map.setGeoreferencing(utm32Georeferencing());
			ForeignData foreign;
			foreign.format = QStringLiteral("OCD");
			foreign.version = 2018;
			ForeignString string;
			string.type = ocd_dem_string_type;
			string.data = QByteArray("\tP99999999\tS540\te1\tl1\0", 23);
			foreign.strings.push_back(string);
			map.setForeignData(foreign);
			auto exporter = format->makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter->doExport());
			Map read;
			auto importer = format->makeImporter(map_path, &read, nullptr);
			QVERIFY(importer->doImport());
			QVERIFY(!read.elevationSet().embeddedGrid());
			QVERIFY(!importer->warnings().empty());
		}
	}

	/// the design notes, "Levels in the range", Accept (1) to (3).
	void contourLevelCountTest()
	{
		ContourParams params;
		QCOMPARE(contourLevelCount(500.05, 540.359, params), 40);
		params.interval = 2.5;
		QCOMPARE(contourLevelCount(500.05, 540.359, params), 16);
		params.interval = 1.0;
		params.minimum = 510.0;
		params.maximum = 520.0;
		QCOMPARE(contourLevelCount(500.05, 540.359, params), 11);
		params.minimum = 500.0;
		params.maximum = std::numeric_limits<double>::quiet_NaN();
		QCOMPARE(contourLevelCount(500.0, 510.0, params), 10);   // no level at the minimum itself
		params.minimum = 530.0;
		params.maximum = 520.0;
		QCOMPARE(contourLevelCount(500.05, 540.359, params), 0);
		QCOMPARE(contourLevelCount(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), ContourParams()), 0);

		// The count is the number of levels the tracer gives lines for on a plane.
		auto const plane = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double x, double) { return 100.0 + 0.1 * x; });
		auto const info = plane->info();
		for (auto const& range : { std::make_pair(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()), std::make_pair(103.0, 107.0) })
		{
			ContourParams p;
			p.minimum = range.first;
			p.maximum = range.second;
			std::set<QString> levels;
			for (auto const& line : traceContours(*plane, p))
				levels.insert(line.tag);
			QCOMPARE(int(levels.size()), contourLevelCount(info.minimum, info.maximum, p));
			if (!std::isnan(range.first))
			{
				QVERIFY(levels.count(QStringLiteral("103")));
				QVERIFY(levels.count(QStringLiteral("107")));
			}
		}
	}

	/// the design notes, "Tiled export", Accept.
	void tiledExport()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto grid = makeGrid(25, 12, 1000.0, 1024.0, 2.0, [](double x, double y) { return 300.0 + 0.01 * x + 0.003 * y; });
		grid->setValue(21, 11, std::numeric_limits<float>::quiet_NaN());
		auto const path = QDir(dir.path()).absoluteFilePath(QStringLiteral("dtm.asc"));

		// Accept (1): the names and the number.
		auto const tiles = grid->saveAsciiTiles(path, 11);
		QCOMPARE(tiles.size(), 6);
		QStringList expected;
		for (auto const* name : { "dtm_0_0.asc", "dtm_0_1.asc", "dtm_0_2.asc", "dtm_1_0.asc", "dtm_1_1.asc", "dtm_1_2.asc" })
			expected.push_back(QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name)));
		QCOMPARE(tiles, expected);
		QVERIFY(!QFileInfo::exists(path));

		// Accept (2): the tiles, sharing their edge cells.
		int const c0[] = { 0, 10, 20 };
		int const w[] = { 11, 11, 5 };
		int const r0[] = { 0, 10 };
		int const h[] = { 11, 2 };
		for (int tr = 0; tr < 2; ++tr)
		{
			for (int tc = 0; tc < 3; ++tc)
			{
				auto const tile = ElevationModel::open(tiles[tr * 3 + tc]);
				QVERIFY2(tile, qPrintable(ElevationModel::lastError()));
				QCOMPARE(tile->width(), w[tc]);
				QCOMPARE(tile->height(), h[tr]);
				QCOMPARE(tile->cellSize(), 2.0);
				QCOMPARE(tile->extent(), QRectF(1000.0 + 2.0 * c0[tc], 1024.0 - 2.0 * (r0[tr] + h[tr]), 2.0 * w[tc], 2.0 * h[tr]));
				for (int r = 0; r < h[tr]; ++r)
				{
					for (int c = 0; c < w[tc]; ++c)
					{
						auto const original = grid->value(c0[tc] + c, r0[tr] + r);
						if (std::isnan(original))
							QVERIFY(std::isnan(tile->value(c, r)));
						else
							QVERIFY(std::abs(tile->value(c, r) - original) < 0.001);
					}
				}
			}
		}

		// Accept (3): one tile when the grid fits; nothing for fewer than two points.
		QCOMPARE(grid->saveAsciiTiles(QDir(dir.path()).absoluteFilePath(QStringLiteral("one.asc"))).size(), 1);
		QVERIFY(grid->saveAsciiTiles(QDir(dir.path()).absoluteFilePath(QStringLiteral("none.asc")), 1).isEmpty());
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
	
	/// "Knolls and small depressions", Accept (2)
	void landformsNone()
	{
		auto const plane = makeGrid(41, 41, 0.0, 41.0, 1.0, [](double x, double y) { return 100.0 + 0.1 * x + 0.05 * y; });
		QVERIFY(findLandforms(*plane, LandformParams{}).empty());
		// A cone of 3 m at the edge: the edge is met before the ground has dropped by the minimum.
		auto const edge = makeGrid(41, 41, 0.0, 41.0, 1.0, [](double x, double y) {
			auto const d = std::hypot(x - 1.5, y - 20.5);
			return 100.0 + (d < 6.0 ? 3.0 * (1.0 - d / 6.0) : 0.0);
		});
		QVERIFY(findLandforms(*edge, LandformParams{}).empty());
		// The same cone in the middle: its whole height. With a void cell on its side it counts with
		// the drop down to that cell, and not at all when the void is beside the summit.
		auto middle = makeGrid(41, 41, 0.0, 41.0, 1.0, [](double x, double y) {
			auto const d = std::hypot(x - 20.5, y - 20.5);
			return 100.0 + (d < 6.0 ? 3.0 * (1.0 - d / 6.0) : 0.0);
		});
		auto found = findLandforms(*middle, LandformParams{});
		QCOMPARE(int(found.size()), 1);
		QVERIFY2(found.front().size > 2.4 && found.front().size <= 3.0, qPrintable(QString::number(found.front().size)));
		middle->setValue(24, 20, std::numeric_limits<float>::quiet_NaN());
		found = findLandforms(*middle, LandformParams{});
		QCOMPARE(int(found.size()), 1);
		QVERIFY2(found.front().size > 1.0 && found.front().size < 2.2, qPrintable(QString::number(found.front().size)));
		middle->setValue(21, 20, std::numeric_limits<float>::quiet_NaN());
		QVERIFY(findLandforms(*middle, LandformParams{}).empty());
		// A hill wider than the area limit counts with the drop within the limit only.
		auto const hill = makeGrid(201, 201, 0.0, 201.0, 1.0, [](double x, double y) {
			return 100.0 + 5.0 * std::max(0.0, 1.0 - std::hypot(x - 100.5, y - 100.5) / 250.0);
		});
		QVERIFY(findLandforms(*hill, LandformParams{}).empty());
		LandformParams wide;
		wide.max_area = 30000.0;
		QCOMPARE(int(findLandforms(*hill, wide).size()), 1);
	}

	/// "Knolls and small depressions", Accept (3)
	void landformObjects()
	{
		Map map;
		map.setGeoreferencing(utm32Georeferencing());
		map.setScaleDenominator(10000);
		auto const symbols = landformSymbols(map);
		QVERIFY(symbols.knoll && symbols.depression);
		QCOMPARE(symbols.knoll->getNumberComponent(0), 109);
		QCOMPARE(symbols.depression->getNumberComponent(0), 111);
		auto const count = map.getNumSymbols();
		auto const again = landformSymbols(map);
		QCOMPARE(map.getNumSymbols(), count);
		QCOMPARE(again.knoll, symbols.knoll);
		
		std::vector<Landform> landforms(2);
		landforms[0].kind = Landform::Knoll;
		landforms[0].projected = QPointF(500010.0, 4999990.0);
		landforms[0].size = 1.234;
		landforms[1].kind = Landform::Depression;
		landforms[1].projected = QPointF(500020.0, 5000000.0);
		landforms[1].size = 2.0;
		auto const objects = makeLandformObjects(map, landforms, symbols);
		QCOMPARE(int(objects.size()), 2);
		QCOMPARE(objects[0]->getType(), Object::Point);
		QCOMPARE(objects[0]->getSymbol(), static_cast<const Symbol*>(symbols.knoll));
		QCOMPARE(objects[1]->getSymbol(), static_cast<const Symbol*>(symbols.depression));
		auto const expected = map.getGeoreferencing().toMapCoordF(landforms[0].projected);
		auto const position = objects[0]->asPoint()->getCoordF();
		QVERIFY(std::abs(position.x() - expected.x()) < 0.001 && std::abs(position.y() - expected.y()) < 0.001);
		QCOMPARE(tagValue(objects[0].get(), "feature"), QStringLiteral("knoll"));
		QCOMPARE(tagValue(objects[0].get(), "size"), QStringLiteral("1.23"));
		QCOMPARE(tagValue(objects[1].get(), "feature"), QStringLiteral("depression"));
	}

	void cliffFilterAndThresholds()
	{
		// the design notes, "Cliffs from the slope gradient", Accept (1) and (2).
		auto const grid = cliffsGrid();
		auto const filtered = gaussianFiltered(*grid);
		// The 3.0 m step east of x = 20, row y = 40; the block of 8 m one cell long at x = 30..40, y = 180.
		QVERIFY2(std::abs(filtered->value(19, 40) - 400.82) < 0.005, qPrintable(QString::number(filtered->value(19, 40))));
		QVERIFY2(std::abs(filtered->value(20, 40) - 402.18) < 0.005, qPrintable(QString::number(filtered->value(20, 40))));
		QVERIFY(std::abs(filtered->value(35, 180) - 403.61) < 0.005);
		QVERIFY(std::abs(filtered->value(35, 179) - 402.19) < 0.005);
	
		QVERIFY(std::abs(steepGradient(30) - 0.7125) < 0.002);
		QVERIFY(std::abs(steepGradient(40) - 1.1115) < 0.002);
		QVERIFY(std::abs(steepGradient(45) - 1.413) < 0.002);
		for (int degrees = 2; degrees <= 89; ++degrees)
		{
			// Rising up to the measured range, never falling beyond it.
			QVERIFY(degrees > 45 ? steepGradient(degrees) >= steepGradient(degrees - 1) : steepGradient(degrees) > steepGradient(degrees - 1));
			QVERIFY(cliffGradient(degrees) > steepGradient(degrees));
		}
		// A region of steep corners is a cliff from its steepest corner: the 3.0 m step
		// stays out at 40 degrees and the 3.25 m step comes in; at 45 the 4.0 and 4.5 m steps.
		QVERIFY(cliffGradient(40) > 1.36 && cliffGradient(40) <= 1.47);
		QVERIFY(cliffGradient(45) > 1.82 && cliffGradient(45) <= 2.04);
		QVERIFY(cliffGradient(30) <= 1.36);
	}

	void cliffAreasOfFixture()
	{
		// the design notes, "Cliffs from the slope gradient", Accept (4).
		auto const grid = cliffsGrid();
		auto const x0 = 966000.0;
		auto const top = 557230.0;
		auto const covers = [x0, top](const std::vector<CliffArea>& areas, double x, double y) {
			QPointF const p(x0 + x, top - y);
			for (auto const& area : areas)
			{
				auto inside = false;
				for (auto const& ring : area.rings)
				{
					for (std::size_t k = 0, n = ring.size(); k < n; ++k)
					{
						auto const& a = ring[k];
						auto const& b = ring[(k + 1) % n];
						if ((a.y() > p.y()) != (b.y() > p.y()) && p.x() < a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y()))
							inside = !inside;
					}
				}
				if (inside)
					return true;
			}
			return false;
		};
		CliffParams params;
		auto const areas = findCliffs(*grid, params);
		QVERIFY(!areas.empty());
		// The 4.5 m step: its west edge at x = 270 and its north and south edges at y = 20 and 60.
		QVERIFY(covers(areas, 270.0, 40.0));
		QVERIFY(covers(areas, 280.0, 20.0));
		QVERIFY(covers(areas, 280.0, 60.0));
		// The ridges' ends at y = 90 and 130, the blocks' edges.
		for (int i = 0; i < 6; ++i)
		{
			QVERIFY(covers(areas, 35.0 + 50 * i, 90.0));
			QVERIFY(covers(areas, 35.0 + 50 * i, 130.0));
			QVERIFY(!covers(areas, 25.0 + 50 * i, 110.0));
		}
		for (int i = 0; i < 5; ++i)
			QVERIFY(covers(areas, 35.0 + 60 * i, 180.0));
		for (int i = 0; i < 5; ++i)
			QVERIFY2(!covers(areas, 20.0 + 50 * i, 40.0), qPrintable(QString::number(i)));
	
		for (auto const& area : areas)
		{
			double sum = 0.0;
			for (auto const& ring : area.rings)
			{
				for (std::size_t k = 0, n = ring.size(); k < n; ++k)
					sum += ring[k].x() * ring[(k + 1) % n].y() - ring[(k + 1) % n].x() * ring[k].y();
			}
			QVERIFY2(std::abs(std::abs(sum) / 2 - area.pixels) < 1e-6, qPrintable(QStringLiteral("%1 %2").arg(std::abs(sum) / 2).arg(area.pixels)));
		}
	
		params.threshold = 30;
		auto const lower = findCliffs(*grid, params);
		QVERIFY(covers(lower, 25.0, 110.0));
		QVERIFY(covers(lower, 20.0, 40.0));
	}

	void cliffMinimums()
	{
		// the design notes, "Cliffs from the slope gradient", Accept (5).
		CliffRaster raster;
		raster.width = 10;
		raster.height = 10;
		raster.first = QPointF(100.0, 200.0);
		raster.gradient.assign(100, 0.0f);
		raster.state.assign(100, CliffRaster::Flat);
		raster.state[44] = CliffRaster::Cliff;
		CliffParams params;
		QVERIFY(cliffAreas(raster, params).empty());
		params.min_area = 1;
		params.min_length = 1;
		auto const one = cliffAreas(raster, params);
		QCOMPARE(int(one.size()), 1);
		QCOMPARE(one.front().pixels, 1);
		QCOMPARE(int(one.front().rings.size()), 1);
		QCOMPARE(int(one.front().rings.front().size()), 4);
		raster.state[45] = CliffRaster::Cliff;
		params.min_length = 3;
		QVERIFY(cliffAreas(raster, params).empty());
		params.min_length = 2;
		QCOMPARE(int(cliffAreas(raster, params).size()), 1);
	}

	void cliffObjects()
	{
		// the design notes, "Cliffs from the slope gradient", Accept (6).
		Map map;
		map.setGeoreferencing(utm32Georeferencing());
		map.setScaleDenominator(10000);
		auto const* symbol = cliffSymbol(map);
		QVERIFY(symbol);
		QCOMPARE(symbol->getNumberComponent(0), 10080);
		QCOMPARE(symbol->getType(), Symbol::Area);
		auto const count = map.getNumSymbols();
		QCOMPARE(cliffSymbol(map), symbol);
		QCOMPARE(map.getNumSymbols(), count);
	
		CliffArea area;
		area.rings = { { QPointF(500000.0, 5000000.0), QPointF(500002.0, 5000000.0), QPointF(500002.0, 4999999.0), QPointF(500000.0, 4999999.0) } };
		area.pixels = 2;
		auto const objects = makeCliffObjects(map, { area }, symbol);
		QCOMPARE(int(objects.size()), 1);
		QCOMPARE(objects[0]->getType(), Object::Path);
		QCOMPARE(objects[0]->getSymbol(), static_cast<const Symbol*>(symbol));
		auto const expected = map.getGeoreferencing().toMapCoordF(QPointF(500002.0, 5000000.0));
		auto const& coords = objects[0]->getRawCoordinateVector();
		QVERIFY(coords.size() >= 4);
		auto const actual = MapCoordF(coords[1]);
		QVERIFY(std::abs(actual.x() - expected.x()) < 0.001 && std::abs(actual.y() - expected.y()) < 0.001);
		QCOMPARE(tagValue(objects[0].get(), "feature"), QStringLiteral("cliff"));
	}

	void treeCases()
	{
		// the design notes, "Trees", Accept (3).
		auto const bumps = [](double separation) {
			return makeGrid(60, 30, 0.0, 30.0, 1.0, [separation](double x, double y) {
				auto const a = std::hypot(x - 20.5, y - 15.5);
				auto const b = std::hypot(x - 20.5 - separation, y - 15.5);
				return 10.0 * std::max(std::exp(-a * a / 4.5), std::exp(-b * b / 4.5));
			});
		};
		auto const close = findTrees(*bumps(6.0), TreeParams{});
		QCOMPARE(int(close.size()), 1);
		QVERIFY2(std::abs(close.front().projected.x() - 23.5) < 0.1 && std::abs(close.front().projected.y() - 15.5) < 0.1,
		         qPrintable(QStringLiteral("%1 %2").arg(close.front().projected.x()).arg(close.front().projected.y())));
		QCOMPARE(int(findTrees(*bumps(9.0), TreeParams{}).size()), 2);
		TreeParams high;
		high.min_height = 20.0;
		QVERIFY(findTrees(*bumps(9.0), high).empty());
	}

	void treeObjects()
	{
		// the design notes, "Trees", Accept (4).
		Map map;
		map.setGeoreferencing(utm32Georeferencing());
		map.setScaleDenominator(10000);
		auto const* symbol = treeSymbol(map);
		QVERIFY(symbol);
		QCOMPARE(symbol->getNumberComponent(0), 418);
		QCOMPARE(symbol->getType(), Symbol::Point);
		auto const count = map.getNumSymbols();
		QCOMPARE(treeSymbol(map), symbol);
		QCOMPARE(map.getNumSymbols(), count);
	
		std::vector<Tree> trees(1);
		trees[0].projected = QPointF(500010.0, 4999990.0);
		trees[0].height = 12.345;
		auto const objects = makeTreeObjects(map, trees, symbol);
		QCOMPARE(int(objects.size()), 1);
		QCOMPARE(objects[0]->getType(), Object::Point);
		QCOMPARE(objects[0]->getSymbol(), static_cast<const Symbol*>(symbol));
		auto const expected = map.getGeoreferencing().toMapCoordF(trees[0].projected);
		auto const position = objects[0]->asPoint()->getCoordF();
		QVERIFY(std::abs(position.x() - expected.x()) < 0.001 && std::abs(position.y() - expected.y()) < 0.001);
		QCOMPARE(tagValue(objects[0].get(), "feature"), QStringLiteral("tree"));
		QCOMPARE(tagValue(objects[0].get(), "height"), QStringLiteral("12.35"));
	}


	void vegetationHeight()
	{
		// the design notes, "Vegetation height and vegetation base map", Accept (2).
		auto const nan = std::numeric_limits<float>::quiet_NaN();
		ElevationModel heights(4, 2, QPointF(950000.0, 500002.0), 1.0, { 0.05f, 1.05f, 7.0f, 20.0f, -1.0f, 40.0f, nan, 0.0f }, QStringLiteral("EPSG:26954"));
		auto const classes = defaultHeightClasses();
		QCOMPARE(int(classes.size()), 4);
		auto const product = vegetationHeightRaster(heights, classes);
		QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
		QCOMPARE(product.image.width(), 4);
		QCOMPARE(product.image.height(), 2);
		QCOMPARE(product.extent, heights.extent());
		auto const near = [](QRgb a, QRgb b) {
			return std::abs(qRed(a) - qRed(b)) <= 1 && std::abs(qGreen(a) - qGreen(b)) <= 1 && std::abs(qBlue(a) - qBlue(b)) <= 1;
		};
		QRgb const expected[] = { qRgb(255, 255, 55), qRgb(255, 130, 28), qRgb(128, 218, 128), qRgb(255, 255, 255),
		                          qRgb(255, 255, 55), qRgb(255, 255, 255), qRgb(0, 0, 0), qRgb(255, 255, 55) };
		for (int i = 0; i < 8; ++i)
		{
			auto const actual = product.image.pixel(i % 4, i / 4);
			QVERIFY2(near(actual, expected[i]), qPrintable(QStringLiteral("%1: %2 %3 %4").arg(i).arg(qRed(actual)).arg(qGreen(actual)).arg(qBlue(actual))));
		}
		
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("height.tif"));
		QString error;
		QVERIFY2(writeGeoTiff(product, path, &error), qPrintable(error));
		auto dataset = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterCount(dataset), 3);
		double gt[6] = {};
		QCOMPARE(GDALGetGeoTransform(dataset, gt), CE_None);
		QCOMPARE(gt[0], 950000.0);
		QCOMPARE(gt[3], 500002.0);
		QVERIFY(std::strlen(GDALGetProjectionRef(dataset)) > 0);
		quint8 green = 0;
		QCOMPARE(GDALRasterIO(GDALGetRasterBand(dataset, 2), GF_Read, 1, 0, 1, 1, &green, 1, 1, GDT_Byte, 0, 0), CE_None);
		QVERIFY(std::abs(int(green) - 130) <= 1);
		GDALClose(dataset);
		
		// A palette image is written with its palette (the vegetation base map).
		RasterProduct indexed;
		indexed.image = QImage(3, 2, QImage::Format_Indexed8);
		indexed.image.setColorTable({ qRgb(255, 0, 0), qRgb(255, 201, 15), qRgb(255, 255, 255) });
		indexed.image.fill(1);
		indexed.image.setPixel(2, 1, 2);
		indexed.extent = QRectF(950000.0, 500000.0, 3.0, 2.0);
		auto const palette_path = dir.filePath(QStringLiteral("palette.tif"));
		QVERIFY2(writeGeoTiff(indexed, palette_path, &error), qPrintable(error));
		dataset = GDALOpen(palette_path.toUtf8().constData(), GA_ReadOnly);
		QVERIFY(dataset);
		QCOMPARE(GDALGetRasterCount(dataset), 1);
		auto band = GDALGetRasterBand(dataset, 1);
		QCOMPARE(int(GDALGetRasterColorInterpretation(band)), int(GCI_PaletteIndex));
		auto table = GDALGetRasterColorTable(band);
		QVERIFY(table);
		auto const* open = GDALGetColorEntry(table, 1);
		QCOMPARE(open->c1, short(255));
		QCOMPARE(open->c2, short(201));
		QCOMPARE(open->c3, short(15));
		quint8 index = 0;
		QCOMPARE(GDALRasterIO(band, GF_Read, 2, 1, 1, 1, &index, 1, 1, GDT_Byte, 0, 0), CE_None);
		QCOMPARE(int(index), 2);
		GDALClose(dataset);
	}


	void vegetationHeightGreyscale()
	{
		// the design notes, "Vegetation height in grey and its classes in files", Accept (1).
		auto const nan = std::numeric_limits<float>::quiet_NaN();
		ElevationModel heights(6, 1, QPointF(950000.0, 500001.0), 1.0, { 0.0f, 15.0f, 30.0f, 40.0f, -1.0f, nan }, QStringLiteral("EPSG:26954"));
		struct Expected { VegetationHeightOptions::GreyMapping mapping; int at15; };
		Expected const mappings[] = {
		    { VegetationHeightOptions::Linear, 128 },
		    { VegetationHeightOptions::QuadraticPositive, 191 },
		    { VegetationHeightOptions::QuadraticNegative, 64 },
		};
		for (auto const& m : mappings)
		{
			auto const product = OpenOrienteering::vegetationHeightGreyscale(heights, m.mapping, 30.0);
			QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
			QCOMPARE(product.image.format(), QImage::Format_Grayscale8);
			QCOMPARE(product.extent, heights.extent());
			auto const grey = [&product](int col) { return qGray(product.image.pixel(col, 0)); };
			QCOMPARE(grey(0), 255);
			QVERIFY2(std::abs(grey(1) - m.at15) <= 1, qPrintable(QString::number(grey(1))));
			QCOMPARE(grey(2), 0);
			QCOMPARE(grey(3), 0);
			QCOMPARE(grey(4), 255);
			QCOMPARE(grey(5), 0);
		}
		// The options choose the raster.
		VegetationHeightOptions options;
		QCOMPARE(vegetationHeightProduct(heights, options).image.format(), QImage::Format_RGB32);
		options.greyscale = true;
		QCOMPARE(vegetationHeightProduct(heights, options).image.format(), QImage::Format_Grayscale8);
	}


	void heightClassFiles()
	{
		// Accept (2).
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("classes.txt"));
		auto const defaults = defaultHeightClasses();
		QString error;
		QVERIFY2(saveHeightClasses(path, defaults, &error), qPrintable(error));
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
			QCOMPARE(QString::fromUtf8(file.readLine()).trimmed(), QStringLiteral("from;to;from_colour;to_colour"));
		}
		std::vector<HeightClass> loaded;
		QVERIFY2(loadHeightClasses(path, loaded, &error), qPrintable(error));
		QCOMPARE(loaded.size(), defaults.size());
		for (std::size_t i = 0; i < defaults.size(); ++i)
		{
			QVERIFY(std::abs(loaded[i].from - defaults[i].from) < 1e-9);
			QVERIFY(std::abs(loaded[i].to - defaults[i].to) < 1e-9);
			QCOMPARE(loaded[i].from_color, defaults[i].from_color);
			QCOMPARE(loaded[i].to_color, defaults[i].to_color);
		}

		auto const refused = [&dir, &defaults](const char* content) {
			auto const bad = dir.filePath(QStringLiteral("bad.txt"));
			QFile file(bad);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return false;
			file.write(content);
			file.close();
			auto classes = defaults;
			QString message;
			auto const ok = loadHeightClasses(bad, classes, &message);
			auto unchanged = classes.size() == defaults.size();
			for (std::size_t i = 0; unchanged && i < classes.size(); ++i)
				unchanged = classes[i].from == defaults[i].from && classes[i].to_color == defaults[i].to_color;
			return !ok && !message.isEmpty() && unchanged;
		};
		QVERIFY(refused("0;2;#000000;#ffffff\n"));
		QVERIFY(refused("from;to;from_colour;to_colour\n5;2;#000000;#ffffff\n"));
		QVERIFY(refused("from;to;from_colour;to_colour\n0;2;#12345;#ffffff\n"));
		QVERIFY(refused("from;to;from_colour;to_colour\n0;2;#000000;#ffffff\n1;5;#000000;#ffffff\n"));
		QVERIFY(refused("from;to;from_colour;to_colour\n"));
		QVERIFY(!refused("from;to;from_colour;to_colour\n0;2;#000000;#ffffff\n2;5;#000000;#ffffff\n"));
	}

	/// the design notes, "Bicubic resampling for hill shading", Accept (1) to (6).
	void bicubicTest()
	{
		auto const nan = std::numeric_limits<double>::quiet_NaN();
		auto const column = makeGrid(8, 8, 0.0, 8.0, 1.0, [](double x, double) { return x > 3.0 && x < 4.0 ? 16.0 : 0.0; });

		// (1) Weights.
		struct { double x; double bicubic; double bilinear; } const along[] = {
		    { 3.0, 9.0, 8.0 }, { 2.0, -1.0, 0.0 }, { 2.75, 3.625, 4.0 }, { 3.25, 13.875, 12.0 } };
		for (auto const& c : along)
		{
			QVERIFY2(qAbs(column->atBicubic(QPointF(c.x, 4.5)) - c.bicubic) < 1e-5, qPrintable(QString::number(c.x)));
			QVERIFY2(qAbs(column->at(QPointF(c.x, 4.5)) - c.bilinear) < 1e-5, qPrintable(QString::number(c.x)));
		}

		// (2) Two dimensions.
		auto const spike = makeGrid(8, 8, 0.0, 8.0, 1.0, [](double x, double y) { return x > 3.0 && x < 4.0 && y > 4.0 && y < 5.0 ? 16.0 : 0.0; });
		QVERIFY(qAbs(spike->atBicubic(QPointF(3.0, 5.0)) - 5.0625) < 1e-5);
		QVERIFY(qAbs(spike->atBicubic(QPointF(2.0, 5.0)) + 0.5625) < 1e-5);

		// (3) Planes.
		auto const plane = makeGrid(10, 10, 0.0, 10.0, 1.0, [](double x, double y) { return 2.0 * x + 3.0 * y + 1.0; });
		for (double y = 2.0; y <= 8.0; y += 0.37)
			for (double x = 2.0; x <= 8.0; x += 0.37)
				QVERIFY2(qAbs(plane->atBicubic(QPointF(x, y)) - (2.0 * x + 3.0 * y + 1.0)) < 1e-4,
				         qPrintable(QString::fromLatin1("%1 %2").arg(x).arg(y)));

		// (4) Voids.
		auto voided = makeGrid(10, 10, 0.0, 10.0, 1.0, [](double, double) { return 5.0; });
		voided->setValue(5, 5, float(nan));
		QVERIFY(qAbs(voided->atBicubic(QPointF(4.2, 6.0)) - 5.0) < 1e-5);
		QVERIFY(std::isnan(voided->atBicubic(QPointF(5.2, 4.8))));
		QVERIFY(std::isnan(voided->atBicubic(QPointF(-0.5, 4.8))));
		QVERIFY(std::isnan(voided->atBicubic(QPointF(4.0, 10.5))));

		// (5) Resampling.
		auto const cubic = column->resampled(0.5, ElevationModel::Interpolation::Bicubic);
		auto const linear = column->resampled(0.5);
		for (auto const* grid : { cubic.get(), linear.get() })
		{
			QCOMPARE(grid->width(), 16);
			QCOMPARE(grid->height(), 16);
			QCOMPARE(grid->extent(), column->extent());
		}
		struct { int col; double bicubic; double bilinear; } const cells[] = {
		    { 4, -1.125, 0.0 }, { 5, 3.625, 4.0 }, { 6, 13.875, 12.0 } };
		for (auto const& c : cells)
		{
			QCOMPARE(cubic->cellCenter(c.col, 0).x(), 0.25 + 0.5 * c.col);
			for (int r = 0; r < 16; ++r)
			{
				QVERIFY2(qAbs(cubic->value(c.col, r) - c.bicubic) < 1e-4, qPrintable(QString::fromLatin1("%1 %2").arg(c.col).arg(r)));
				QVERIFY2(qAbs(linear->value(c.col, r) - c.bilinear) < 1e-4, qPrintable(QString::fromLatin1("%1 %2").arg(c.col).arg(r)));
			}
		}

		// (6) Hill shading.
		QCOMPARE(HillShadeParams().resampling, HillShadeParams::Bicubic);
		HillShadeParams params;
		params.cell_size = 0.5;
		auto const shaded = OpenOrienteering::hillShade(*column, params);
		QVERIFY2(shaded.error.isEmpty(), qPrintable(shaded.error));
		HillShadeParams native = params;
		native.cell_size = 0.0;
		auto const reference = OpenOrienteering::hillShade(*cubic, native);
		QCOMPARE(shaded.image.size(), QSize(15, 15));
		QVERIFY(shaded.image == reference.image);
		params.resampling = HillShadeParams::Bilinear;
		auto const bilinear_shaded = OpenOrienteering::hillShade(*column, params);
		QVERIFY(bilinear_shaded.image == OpenOrienteering::hillShade(*linear, native).image);
		QVERIFY(bilinear_shaded.image != shaded.image);

		auto const inclined = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 + 0.1 * y; });
		HillShadeParams fine;
		fine.multi_directional = false;
		fine.cell_size = 0.5;
		auto const fine_product = OpenOrienteering::hillShade(*inclined, fine);
		QCOMPARE(fine_product.image.size(), QSize(201, 201));
		for (int r = 4; r < 198; ++r)
			for (int c = 4; c < 198; ++c)
				QVERIFY2(qAbs(int(fine_product.image.constScanLine(r)[c]) - 120) <= 1,
				         qPrintable(QString::fromLatin1("%1 %2: %3").arg(r).arg(c).arg(fine_product.image.constScanLine(r)[c])));
	}

	/// the design notes, "The gradient on the corners of 2 by 2 cells", Accept (1) to (3).
	void cornerGradientTest()
	{
		auto const nan = std::numeric_limits<float>::quiet_NaN();
		auto const small = [](bool voided) {
			std::vector<float> values = { 10, 12, 16, 11, 15, 17, 13, 14, 20 };
			if (voided)
				values[4] = std::numeric_limits<float>::quiet_NaN();
			return std::make_unique<ElevationModel>(3, 3, QPointF(0.0, 6.0), 2.0, std::move(values));
		};

		// (1) The formula.
		auto const grid = small(false);
		struct { int col; int row; double gx; double gy; } const corners[] = {
		    { 0, 0, 1.5, -1.0 }, { 1, 0, 1.5, -1.0 }, { 0, 1, 1.25, -0.25 }, { 1, 1, 2.0, -0.5 } };
		for (auto const& c : corners)
		{
			auto const g = grid->cornerGradientAt(c.col, c.row);
			QVERIFY2(qAbs(g.x() - c.gx) < 1e-9 && qAbs(g.y() - c.gy) < 1e-9, qPrintable(QString::fromLatin1("%1 %2").arg(c.col).arg(c.row)));
		}
		auto const voided = small(true);
		for (auto const& c : corners)
		{
			auto const g = voided->cornerGradientAt(c.col, c.row);
			QVERIFY(std::isnan(g.x()) && std::isnan(g.y()));
		}

		// (2) The lattice.
		SlopeParams slope;
		slope.threshold_deg = 80.0;
		QCOMPARE(slope.gradient, GradientMethod::Corners);
		auto product = slopeRaster(*grid, slope);
		QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
		QCOMPARE(product.image.size(), QSize(2, 2));
		QCOMPARE(product.extent, QRectF(1.0, 1.0, 4.0, 4.0));
		QVERIFY(qAbs(int(product.image.constScanLine(1)[0]) - 123) <= 1);
		QVERIFY(qAbs(int(product.image.constScanLine(1)[1]) - 92) <= 1);
		product = slopeRaster(*voided, slope);
		for (int r = 0; r < 2; ++r)
			for (int c = 0; c < 2; ++c)
				QCOMPARE(int(product.image.constScanLine(r)[c]), 0);
		auto const narrow = std::make_unique<ElevationModel>(1, 3, QPointF(0.0, 6.0), 2.0, std::vector<float>{ 1, 2, 3 });
		product = slopeRaster(*narrow, slope);
		QVERIFY(!product.error.isEmpty());
		QVERIFY(product.image.isNull());
		QVERIFY(!hillShade(*narrow, HillShadeParams()).error.isEmpty());
		slope.gradient = GradientMethod::Horn;
		product = slopeRaster(*grid, slope);
		QCOMPARE(product.image.size(), QSize(3, 3));
		QCOMPARE(product.extent, grid->extent());

		// (3) Planes.
		auto inclined = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 + 0.1 * y; });
		auto flat = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double) { return 100.0; });
		auto steep = makeGrid(101, 101, 0.0, 101.0, 1.0, [](double, double y) { return 100.0 + 1.7320508 * y; });
		auto const everywhere = [](const RasterProduct& p, int expected) {
			if (p.image.size() != QSize(100, 100))
				return false;
			for (int r = 0; r < 100; ++r)
				for (int c = 0; c < 100; ++c)
					if (qAbs(int(p.image.constScanLine(r)[c]) - expected) > 1)
						return false;
			return true;
		};
		HillShadeParams hill;
		QCOMPARE(hill.gradient, GradientMethod::Corners);
		struct { HillShadeParams::Method method; bool multi; int expected; } const cases[] = {
		    { HillShadeParams::Oblique, false, 120 },
		    { HillShadeParams::Oblique, true, 167 },
		    { HillShadeParams::SlopeShading, false, 193 },
		    { HillShadeParams::Combined, false, 91 } };
		for (auto const& c : cases)
		{
			hill.method = c.method;
			hill.multi_directional = c.multi;
			QVERIFY2(everywhere(hillShade(*inclined, hill), c.expected), qPrintable(QString::number(c.expected)));
		}
		hill.method = HillShadeParams::Oblique;
		hill.multi_directional = false;
		QVERIFY(everywhere(hillShade(*flat, hill), 180));
		hill.method = HillShadeParams::SlopeShading;
		QVERIFY(everywhere(hillShade(*flat, hill), 255));
		SlopeParams plane_slope;
		QVERIFY(everywhere(slopeRaster(*inclined, plane_slope), 229));
		QVERIFY(everywhere(slopeRaster(*steep, plane_slope), 51));
		QVERIFY(everywhere(slopeRaster(*flat, plane_slope), 255));
		plane_slope.binary = true;
		QVERIFY(everywhere(slopeRaster(*inclined, plane_slope), 255));
		QVERIFY(everywhere(slopeRaster(*steep, plane_slope), 0));
	}
	
	/// the design notes, "The grid's frame", Accept (1) to (4).
	void gridFootprintTest()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto const path = dir.filePath(QStringLiteral("utm14.tif"));
		auto grid = makeGrid(20, 10, 500000.0, 5200050.0, 5.0, [](double, double) { return 400.0; });
		grid->setCrsSpec(QStringLiteral("EPSG:32614"));
		QVERIFY(grid->save(path));

		auto const near = [](const QPointF& a, const QPointF& b, double tolerance) {
			return qAbs(a.x() - b.x()) <= tolerance && qAbs(a.y() - b.y()) <= tolerance;
		};

		// (1) Same system.
		auto const same = gridFootprint(path, QStringLiteral("EPSG:32614"));
		QCOMPARE(same.size(), 64);
		QVERIFY(near(same[0], QPointF(500000.0, 5200050.0), 0.001));
		QVERIFY(near(same[8], QPointF(500050.0, 5200050.0), 0.001));
		QVERIFY(near(same[16], QPointF(500100.0, 5200050.0), 0.001));
		QVERIFY(near(same[32], QPointF(500100.0, 5200000.0), 0.001));
		QVERIFY(near(same[48], QPointF(500000.0, 5200000.0), 0.001));
		for (auto const& p : same)
		{
			auto const on_x = qAbs(p.x() - 500000.0) <= 0.001 || qAbs(p.x() - 500100.0) <= 0.001;
			auto const on_y = qAbs(p.y() - 5200000.0) <= 0.001 || qAbs(p.y() - 5200050.0) <= 0.001;
			QVERIFY(on_x || on_y);
			QVERIFY(p.x() >= 500000.0 - 0.001 && p.x() <= 500100.0 + 0.001);
			QVERIFY(p.y() >= 5200000.0 - 0.001 && p.y() <= 5200050.0 + 0.001);
		}

		// (2) Another system.
		auto const other = gridFootprint(path, QStringLiteral("EPSG:32615"));
		QCOMPARE(other.size(), 64);
		auto source = OSRNewSpatialReference(nullptr);
		auto target = OSRNewSpatialReference(nullptr);
		QCOMPARE(OSRSetFromUserInput(source, "EPSG:32614"), OGRERR_NONE);
		QCOMPARE(OSRSetFromUserInput(target, "EPSG:32615"), OGRERR_NONE);
		OSRSetAxisMappingStrategy(source, OAMS_TRADITIONAL_GIS_ORDER);
		OSRSetAxisMappingStrategy(target, OAMS_TRADITIONAL_GIS_ORDER);
		auto transform = OCTNewCoordinateTransformation(source, target);
		QVERIFY(transform);
		for (auto const index : { 0, 8, 16, 32, 48 })
		{
			double x = same[index].x(), y = same[index].y();
			QVERIFY(OCTTransform(transform, 1, &x, &y, nullptr));
			QVERIFY2(near(other[index], QPointF(x, y), 0.001), qPrintable(QString::number(index)));
		}
		OCTDestroyCoordinateTransformation(transform);
		OSRDestroySpatialReference(source);
		OSRDestroySpatialReference(target);
		QVERIFY(qAbs(other[0].y() - other[16].y()) > 1.0);
		double area = 0.0;
		for (int i = 0; i < other.size(); ++i)
		{
			auto const& a = other[i];
			auto const& b = other[(i + 1) % other.size()];
			area += a.x() * b.y() - b.x() * a.y();
		}
		QVERIFY2(qAbs(qAbs(area) / 2.0 - 5000.0) < 50.0, qPrintable(QString::number(qAbs(area) / 2.0)));

		// (3) No system, no file.
		auto const plain_path = dir.filePath(QStringLiteral("plain.asc"));
		QVERIFY(makeGrid(20, 10, 1000.0, 2050.0, 5.0, [](double, double) { return 1.0; })->save(plain_path));
		QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("plain.prj"))));
		auto const plain = gridFootprint(plain_path, QStringLiteral("EPSG:32615"));
		QCOMPARE(plain.size(), 64);
		QVERIFY(near(plain[0], QPointF(1000.0, 2050.0), 0.001));
		QVERIFY(near(plain[8], QPointF(1050.0, 2050.0), 0.001));
		QVERIFY(near(plain[16], QPointF(1100.0, 2050.0), 0.001));
		QVERIFY(near(plain[32], QPointF(1100.0, 2000.0), 0.001));
		QVERIFY(near(plain[48], QPointF(1000.0, 2000.0), 0.001));
		QVERIFY(gridFootprint(dir.filePath(QStringLiteral("missing.tif")), QStringLiteral("EPSG:32614")).isEmpty());

		// (4) The map.
		Map map;
		auto georef = utm32Georeferencing();
		georef.setProjectedCRS(QStringLiteral("UTM 14"), QStringLiteral("+proj=utm +zone=14 +datum=WGS84"));
		georef.setProjectedRefPoint(QPointF(500000.0, 5200000.0), false, false);
		georef.setGrivation(0.0);
		georef.setCombinedScaleFactor(1.0);
		map.setGeoreferencing(georef);
		QVERIFY(elevationFrames(map).empty());
		map.elevationSet().setHref(ElevationSet::Terrain, path);
		auto frames = elevationFrames(map);
		QCOMPARE(int(frames.size()), 1);
		QCOMPARE(frames[0].kind, ElevationSet::Terrain);
		QCOMPARE(frames[0].ring.size(), 64);
		auto const& map_georef = map.getGeoreferencing();
		for (int i = 0; i < 64; ++i)
			QVERIFY2(near(frames[0].ring[i], map_georef.toMapCoordF(same[i]), 0.001), qPrintable(QString::number(i)));
		QVERIFY(near(frames[0].ring[0], QPointF(0.0, -5.0), 0.01));
		QVERIFY(near(frames[0].ring[32], QPointF(10.0, 0.0), 0.01));
		map.elevationSet().setHref(ElevationSet::Surface, path);
		frames = elevationFrames(map);
		QCOMPARE(int(frames.size()), 2);
		QCOMPARE(frames[1].kind, ElevationSet::Surface);
	}
};


QTEST_MAIN(ElevationTest)


#include "elevation_t.moc"  // IWYU pragma: keep
