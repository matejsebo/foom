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
#include <functional>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "elevation/elevation_model.h"
#include "elevation/point_cloud.h"

using namespace OpenOrienteering;


namespace {

// the design notes, "Reading point clouds and gridding them", Accept.

constexpr double east0 = 950000.0;
constexpr double north0 = 500000.0;

double plane(double x, double y)
{
	return 100.0 + 0.05 * (x - east0) + 0.02 * (y - north0);
}

/// A fixed pseudo-random sequence in [0, 1).
class Sequence
{
public:
	double next()
	{
		state = state * 1664525u + 1013904223u;
		return (state >> 8) / double(1u << 24);
	}
	
private:
	quint32 state = 12345u;
};

struct CloudOptions
{
	QRectF vegetation;          ///< local coordinates
	bool unclassified = false;
	double hole = 0.0;          ///< radius of the empty disc around (25, 25)
};

std::vector<CloudPoint> testCloud(const CloudOptions& options)
{
	std::vector<CloudPoint> points;
	Sequence random;
	for (int i = 0; i < 100; ++i)
	{
		for (int j = 0; j < 100; ++j)
		{
			auto const lx = 0.5 * i + 0.02 + 0.46 * random.next();
			auto const ly = 0.5 * j + 0.02 + 0.46 * random.next();
			if (options.hole > 0 && std::hypot(lx - 25.0, ly - 25.0) < options.hole)
				continue;
			CloudPoint ground;
			ground.x = east0 + lx;
			ground.y = north0 + ly;
			ground.z = plane(ground.x, ground.y);
			ground.classification = 2;
			ground.intensity = quint16(100 + i);
			if (options.vegetation.contains(lx, ly))
			{
				CloudPoint top = ground;
				top.z += 10.5 + 9.5 * random.next();
				top.classification = 5;
				top.return_number = 1;
				top.number_of_returns = 2;
				points.push_back(top);
				ground.return_number = 2;
				ground.number_of_returns = 2;
			}
			points.push_back(ground);
			if (options.unclassified)
			{
				CloudPoint noise = ground;
				noise.x = east0 + 0.5 * i + 0.02 + 0.46 * random.next();
				noise.y = north0 + 0.5 * j + 0.02 + 0.46 * random.next();
				noise.z = plane(noise.x, noise.y) + 50.0;
				noise.classification = 1;
				noise.return_number = 1;
				noise.number_of_returns = 1;
				points.push_back(noise);
			}
		}
	}
	return points;
}

QString epsg2056Wkt()
{
	auto srs = OSRNewSpatialReference(nullptr);
	OSRImportFromEPSG(srs, 26954);
	char* wkt = nullptr;
	OSRExportToWkt(srs, &wkt);
	auto const result = QString::fromUtf8(wkt);
	CPLFree(wkt);
	OSRDestroySpatialReference(srs);
	return result;
}

}  // namespace



class PointCloudTest : public QObject
{
Q_OBJECT
	
private:
	QTemporaryDir dir;
	
	QString writeCloud(const QString& name, const std::vector<CloudPoint>& points, const QString& wkt = {}, int epsg = 0)
	{
		auto const path = dir.filePath(name);
		QString error;
		if (!PointCloud::write(path, points, wkt, epsg, &error))
			qFatal("Cannot write %s: %s", qPrintable(path), qPrintable(error));
		return path;
	}
	
private slots:
	void initTestCase()
	{
		if (!PointCloud::supported())
			QSKIP("Built without LASzip");
		QVERIFY(dir.isValid());
		GDALAllRegister();
	}
	
	
	void readTest()
	{
		// Accept (1).
		std::vector<CloudPoint> points;
		quint8 const classes[] = { 2, 5, 1 };
		for (int i = 0; i < 9; ++i)
		{
			CloudPoint p;
			p.x = east0 + 1.234 * (i % 3);
			p.y = north0 + 2.345 * (i / 3);
			p.z = 432.101 + i;
			p.intensity = quint16(1000 + i);
			p.classification = classes[i % 3];
			p.return_number = quint8(1 + i % 2);
			p.number_of_returns = 2;
			points.push_back(p);
		}
		// Enough points for compression to pay off.
		auto many = points;
		for (int k = 1; k < 200; ++k)
			many.insert(many.end(), points.begin(), points.end());
		auto const las = writeCloud(QStringLiteral("small.las"), many);
		auto const laz = writeCloud(QStringLiteral("small.laz"), many);
		QVERIFY(QFileInfo(laz).size() < QFileInfo(las).size());
		for (auto const& path : { las, laz })
		{
			std::vector<CloudPoint> back;
			QString crs, error;
			QVERIFY2(PointCloud::read(path, [&back](const CloudPoint& p) { back.push_back(p); }, &crs, &error), qPrintable(error));
			QCOMPARE(back.size(), many.size());
			for (std::size_t i = 0; i < 9; ++i)
			{
				QVERIFY(std::abs(back[i].x - points[i].x) < 0.005);
				QVERIFY(std::abs(back[i].y - points[i].y) < 0.005);
				QVERIFY(std::abs(back[i].z - points[i].z) < 0.005);
				QCOMPARE(back[i].classification, points[i].classification);
				QCOMPARE(back[i].return_number, points[i].return_number);
				QCOMPARE(back[i].number_of_returns, points[i].number_of_returns);
				QCOMPARE(back[i].intensity, points[i].intensity);
			}
			QVERIFY(crs.isEmpty());
		}
		QString error;
		QVERIFY(!PointCloud::read(dir.filePath(QStringLiteral("absent.las")), [](const CloudPoint&) {}, nullptr, &error));
		QVERIFY(!error.isEmpty());
	}
	
	
	void analyseTest()
	{
		// Accept (2).
		CloudOptions options;
		options.vegetation = QRectF(0, 0, 25, 25);
		auto const points = testCloud(options);
		QCOMPARE(int(points.size()), 12500);
		auto const path = writeCloud(QStringLiteral("analyse.laz"), points);
		auto const info = PointCloud::analyse({ path });
		QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
		QCOMPARE(info.count, qint64(12500));
		auto left = points[0].x, right = left, bottom = points[0].y, top = bottom, low = points[0].z, high = low;
		for (auto const& p : points)
		{
			left = std::min(left, p.x);
			right = std::max(right, p.x);
			bottom = std::min(bottom, p.y);
			top = std::max(top, p.y);
			low = std::min(low, p.z);
			high = std::max(high, p.z);
		}
		QVERIFY(std::abs(info.extent.left() - left) < 0.01);
		QVERIFY(std::abs(info.extent.right() - right) < 0.01);
		QVERIFY(std::abs(info.extent.top() - bottom) < 0.01);
		QVERIFY(std::abs(info.extent.bottom() - top) < 0.01);
		QVERIFY(std::abs(info.density - 12500 / ((right - left) * (top - bottom))) < 0.01);
		QCOMPARE(info.per_class[2], qint64(10000));
		QCOMPARE(info.per_class[5], qint64(2500));
		QCOMPARE(info.per_class[1], qint64(0));
		QVERIFY(std::abs(info.z_min - low) < 0.01);
		QVERIFY(std::abs(info.z_max - high) < 0.01);
		
		// Two files add up.
		auto const twice = PointCloud::analyse({ path, path });
		QCOMPARE(twice.count, qint64(25000));
		QVERIFY(!PointCloud::analyse({ dir.filePath(QStringLiteral("absent.las")) }).error.isEmpty());
	}
	
	
	void dtmDsmTest()
	{
		// Accept (3), (4) and (8).
		CloudOptions options;
		options.vegetation = QRectF(30, 30, 10, 10);
		options.unclassified = true;
		auto const points = testCloud(options);
		auto const las = writeCloud(QStringLiteral("cloud.las"), points);
		auto const laz = writeCloud(QStringLiteral("cloud.laz"), points);
		auto const result = PointCloud::grid({ laz }, GriddingParams());
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QVERIFY(result.dtm);
		QVERIFY(result.dsm);
		auto const& dtm = *result.dtm;
		auto const& dsm = *result.dsm;
		QCOMPARE(dtm.width(), 50);
		QCOMPARE(dtm.height(), 50);
		QCOMPARE(dtm.cellSize(), 1.0);
		QCOMPARE(dtm.extent(), QRectF(east0, north0, 50, 50));
		QCOMPARE(dsm.extent(), dtm.extent());
		auto worst = 0.0;
		for (int row = 0; row < 50; ++row)
		{
			for (int col = 0; col < 50; ++col)
			{
				auto const c = dtm.cellCenter(col, row);
				auto const z = double(dtm.value(col, row));
				QVERIFY(!std::isnan(z));
				worst = std::max(worst, std::abs(z - plane(c.x(), c.y())));
				QVERIFY(dsm.value(col, row) >= dtm.value(col, row));
				if (QRectF(east0 + 30, north0 + 30, 10, 10).contains(c))
				{
					auto const above = dsm.value(col, row) - plane(c.x(), c.y());
					QVERIFY2(above >= 10.0 && above <= 20.5, qPrintable(QString::number(above)));
				}
			}
		}
		QVERIFY2(worst < 0.05, qPrintable(QString::number(worst)));
		
		auto const again = PointCloud::grid({ las }, GriddingParams());
		QVERIFY(again.dtm && again.dsm);
		QCOMPARE(again.dtm->values(), dtm.values());
		QCOMPARE(again.dsm->values(), dsm.values());
		
		GriddingParams only_dtm;
		only_dtm.dsm = false;
		auto const one = PointCloud::grid({ laz }, only_dtm);
		QVERIFY(one.dtm);
		QVERIFY(!one.dsm);
	}
	
	
	void voidTest()
	{
		// Accept (5).
		CloudOptions small;
		small.hole = 5.0;
		auto result = PointCloud::grid({ writeCloud(QStringLiteral("hole5.laz"), testCloud(small)) }, GriddingParams());
		QVERIFY2(result.dtm, qPrintable(result.error));
		auto worst = 0.0;
		for (int row = 0; row < 50; ++row)
		{
			for (int col = 0; col < 50; ++col)
			{
				auto const c = result.dtm->cellCenter(col, row);
				auto const z = double(result.dtm->value(col, row));
				QVERIFY(!std::isnan(z));
				worst = std::max(worst, std::abs(z - plane(c.x(), c.y())));
			}
		}
		QVERIFY2(worst < 0.15, qPrintable(QString::number(worst)));
		
		CloudOptions large;
		large.hole = 15.0;
		result = PointCloud::grid({ writeCloud(QStringLiteral("hole15.laz"), testCloud(large)) }, GriddingParams());
		QVERIFY2(result.dtm, qPrintable(result.error));
		auto const& dtm = *result.dtm;
		QVERIFY(std::isnan(dtm.value(24, 25)));
		QVERIFY(std::isnan(dtm.value(25, 24)));
		for (int row = 0; row < 50; ++row)
		{
			for (int col = 0; col < 50; ++col)
			{
				auto const c = dtm.cellCenter(col, row);
				auto const d = std::hypot(c.x() - east0 - 25, c.y() - north0 - 25);
				if (d >= 12.0)
					QVERIFY2(!std::isnan(dtm.value(col, row)), qPrintable(QString::number(d)));
			}
		}
	}
	
	
	void boxTest()
	{
		// Accept (6).
		auto const path = writeCloud(QStringLiteral("box.laz"), testCloud(CloudOptions()));
		GriddingParams params;
		params.box = QRectF(east0 + 10, north0 + 10, 20, 20);
		auto const result = PointCloud::grid({ path }, params);
		QVERIFY2(result.dtm, qPrintable(result.error));
		QCOMPARE(result.dtm->width(), 20);
		QCOMPARE(result.dtm->height(), 20);
		QCOMPARE(result.dtm->extent(), params.box);
		auto const c = result.dtm->cellCenter(3, 4);
		QVERIFY(std::abs(result.dtm->value(3, 4) - plane(c.x(), c.y())) < 0.05);
		
		params.cell = 2.0;
		params.box = {};
		auto const coarse = PointCloud::grid({ path }, params);
		QVERIFY(coarse.dtm);
		QCOMPARE(coarse.dtm->width(), 25);
		QCOMPARE(coarse.dtm->height(), 25);
	}
	
	
	/// the design notes, "Tiles of 1 km", Accept.
	void tiledGridTest()
	{
		// Accept (1): the default tile holds the test cloud in one row.
		CloudOptions options;
		options.hole = 15.0;
		options.vegetation = QRectF(30, 30, 10, 10);
		auto const cloud = testCloud(options);
		std::vector<CloudPoint> north, south;
		for (auto const& p : cloud)
			(p.y - north0 >= 25.0 ? north : south).push_back(p);
		QStringList const paths = { writeCloud(QStringLiteral("tiles_north.laz"), north), writeCloud(QStringLiteral("tiles_south.laz"), south) };
		GriddingParams params;
		params.fill_cells = 3;
		auto const whole = PointCloud::grid(paths, params);
		QVERIFY2(whole.dtm && whole.dsm, qPrintable(whole.error));
		QCOMPARE(whole.tile_rows, 1);
		QCOMPARE(whole.file_reads, 2);

		// Accept (2) and (3): rows of 10 m with the buffer of the void filling.
		params.tile_size = 10.0;
		params.tile_buffer = 0.0;
		auto const tiled = PointCloud::grid(paths, params);
		QVERIFY2(tiled.dtm && tiled.dsm, qPrintable(tiled.error));
		QCOMPARE(tiled.tile_rows, 5);
		QCOMPARE(tiled.file_reads, 6);
		for (auto const& pair : { std::make_pair(whole.dtm.get(), tiled.dtm.get()), std::make_pair(whole.dsm.get(), tiled.dsm.get()) })
		{
			QCOMPARE(pair.second->width(), pair.first->width());
			QCOMPARE(pair.second->height(), pair.first->height());
			QCOMPARE(pair.second->extent(), pair.first->extent());
			QCOMPARE(pair.second->crsSpec(), pair.first->crsSpec());
			auto voids = 0;
			for (int row = 0; row < pair.first->height(); ++row)
			{
				for (int col = 0; col < pair.first->width(); ++col)
				{
					auto const a = pair.first->value(col, row);
					auto const b = pair.second->value(col, row);
					QCOMPARE(std::isnan(a), std::isnan(b));
					voids += std::isnan(a);
					if (!std::isnan(a))
						QVERIFY2(std::abs(a - b) < 1e-4, qPrintable(QStringLiteral("%1 %2: %3 %4").arg(col).arg(row).arg(a).arg(b)));
				}
			}
			QVERIFY(voids > 0);  // the hole of 15 m stays void in its centre
		}

		// Accept (4): the limit.
		QVERIFY(PointCloud::maxCells() >= qint64(400000000));
	}


	void crsTest()
	{
		// Accept (7).
		auto const points = testCloud(CloudOptions());
		auto const with_wkt = writeCloud(QStringLiteral("wkt.laz"), points, epsg2056Wkt());
		auto const with_keys = writeCloud(QStringLiteral("keys.las"), points, {}, 26954);
		auto const without = writeCloud(QStringLiteral("none.laz"), points);
		
		auto result = PointCloud::grid({ with_wkt }, GriddingParams());
		QVERIFY(result.dtm && result.dsm);
		QVERIFY2(ElevationModel::sameCrs(result.dtm->crsSpec(), QStringLiteral("EPSG:26954")), qPrintable(result.dtm->crsSpec()));
		QVERIFY(ElevationModel::sameCrs(result.dsm->crsSpec(), QStringLiteral("EPSG:26954")));
		
		QString crs;
		QVERIFY(PointCloud::read(with_keys, [](const CloudPoint&) {}, &crs, nullptr));
		QCOMPARE(crs, QStringLiteral("EPSG:26954"));
		QCOMPARE(PointCloud::analyse({ with_keys }).crs_spec, QStringLiteral("EPSG:26954"));
		
		result = PointCloud::grid({ without }, GriddingParams());
		QVERIFY(result.dtm);
		QVERIFY(result.dtm->crsSpec().isEmpty());
	}
	
	void dsmMeanTest()
	{
		// the design notes, "Vegetation height and vegetation base map", Accept (1).
		std::vector<CloudPoint> points;
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				CloudPoint ground;
				ground.x = east0 + 0.25 + 0.5 * i;
				ground.y = north0 + 0.25 + 0.5 * j;
				ground.z = 100.0;
				ground.classification = 2;
				points.push_back(ground);
			}
		}
		// Only vegetation as first returns in the south-west cell.
		for (auto& p : points)
		{
			if (p.x < east0 + 1 && p.y < north0 + 1)
			{
				p.return_number = 2;
				p.number_of_returns = 2;
			}
		}
		for (double h : { 1.0, 2.0, 6.0 })
		{
			CloudPoint top;
			top.x = east0 + 0.3 + 0.1 * h;
			top.y = north0 + 0.5;
			top.z = 100.0 + h;
			top.classification = 5;
			top.return_number = 1;
			top.number_of_returns = 2;
			points.push_back(top);
		}
		auto const result = PointCloud::grid({ writeCloud(QStringLiteral("mean.laz"), points) }, GriddingParams());
		QVERIFY2(result.dsm, qPrintable(result.error));
		QCOMPARE(result.dsm->width(), 2);
		QCOMPARE(result.dsm->height(), 2);
		QVERIFY(std::abs(result.dsm->value(0, 1) - 103.0) < 0.005);
		QVERIFY(std::abs(result.dsm->value(1, 1) - 100.0) < 0.005);
		QVERIFY(std::abs(result.dtm->value(0, 1) - 100.0) < 0.005);
	}
	
	
	void baseMapRulesTest()
	{
		// Accept (3): blocks of 10 by 10 m, 20 returns per cell of 1 m, flat ground at 100 m.
		struct Block { int ground, canopy, vegetation, undergrowth; int expected; };
		Block const blocks[] = {
		    { 20, 0, 0, 0, PointCloud::Open },
		    { 10, 10, 0, 0, PointCloud::Runnable },
		    { 15, 0, 5, 0, PointCloud::Slow },
		    { 12, 0, 8, 0, PointCloud::Walk },
		    { 8, 0, 12, 0, PointCloud::Fight },
		    { 17, 0, 0, 3, PointCloud::UndergrowthSome },
		    { 14, 0, 0, 6, PointCloud::UndergrowthDense },
		    { 10, 0, 8, 2, PointCloud::Fight },
		    { 8, 0, 12, 0, PointCloud::NoData },   // its centre cell has no returns
		};
		auto const count = int(sizeof(blocks) / sizeof(blocks[0]));
		std::vector<CloudPoint> points;
		for (int b = 0; b < count; ++b)
		{
			for (int col = 0; col < 10; ++col)
			{
				for (int row = 0; row < 10; ++row)
				{
					if (b == count - 1 && col == 5 && row == 5)
						continue;
					auto const& block = blocks[b];
					int k = 0;
					auto add = [&](int n, double h, quint8 classification) {
						for (int i = 0; i < n; ++i, ++k)
						{
							CloudPoint p;
							p.x = east0 + 10 * b + col + 0.1 + 0.2 * (k % 5);
							p.y = north0 + row + 0.1 + 0.2 * (k / 5);
							p.z = 100.0 + h;
							p.classification = classification;
							points.push_back(p);
						}
					};
					add(block.ground, 0.0, 2);
					add(block.canopy, 12.0, 5);
					add(block.vegetation, 2.0, 5);
					add(block.undergrowth, 0.7, 3);
				}
			}
		}
		auto const path = writeCloud(QStringLiteral("rules.laz"), points);
		auto const result = PointCloud::grid({ path }, GriddingParams());
		QVERIFY2(result.dtm, qPrintable(result.error));
		QCOMPARE(result.dtm->width(), 10 * count);
		auto const product = PointCloud::vegetationBaseMap({ path }, *result.dtm, VegetationParams());
		QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
		QCOMPARE(product.image.format(), QImage::Format_Indexed8);
		QCOMPARE(product.image.width(), 10 * count);
		QCOMPARE(product.image.height(), 10);
		QCOMPARE(product.extent, result.dtm->extent());
		for (int b = 0; b < count; ++b)
			QCOMPARE(product.image.pixelIndex(10 * b + 5, 4), blocks[b].expected);
		// The palette.
		QCOMPARE(product.image.color(PointCloud::Open), qRgb(255, 201, 15));
		QCOMPARE(product.image.color(PointCloud::NoData), qRgb(255, 0, 0));
		QCOMPARE(product.image.color(PointCloud::Fight), qRgb(51, 255, 0));
		QCOMPARE(product.image.color(PointCloud::UndergrowthDense), qRgb(128, 0, 255));
	}
	
	
};


QTEST_GUILESS_MAIN(PointCloudTest)


#include "point_cloud_t.moc"  // IWYU pragma: keep
