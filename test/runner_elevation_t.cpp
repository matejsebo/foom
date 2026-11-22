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
#include <utility>
#include <vector>

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/raster_products.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"

using namespace OpenOrienteering;


namespace {

const QString utm14 = QStringLiteral("+proj=utm +zone=14 +datum=WGS84");

bool saveMap(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

void georeference(Map& map)
{
	auto georef = map.getGeoreferencing();
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 14"), utm14);
	georef.setProjectedRefPoint(QPointF(500000.0, 5200000.0), false, false);
	georef.setMapRefPoint(MapCoord(0, 0));
	georef.setGrivation(0.0);
	georef.setCombinedScaleFactor(1.0);
	map.setGeoreferencing(georef);
}

struct JobResult
{
	int exit_code = -1;
	QByteArray out;
	QByteArray err;

	QJsonObject logOf(const char* step) const
	{
		for (auto const& line : out.split('\n'))
		{
			auto const object = QJsonDocument::fromJson(line).object();
			if (object.value(QLatin1String("step")).toString() == QLatin1String(step))
				return object;
		}
		return {};
	}

	QByteArray all() const { return out + err; }
};

QJsonObject stepOf(const char* name, QJsonObject params)
{
	params.insert(QStringLiteral("step"), QString::fromLatin1(name));
	return params;
}

/// The grey values of a GeoTIFF product equal those of the image.
bool sameGrey(const QString& path, const QImage& image)
{
	auto const written = ElevationModel::open(path);
	if (!written || written->width() != image.width() || written->height() != image.height())
		return false;
	for (int r = 0; r < image.height(); ++r)
		for (int c = 0; c < image.width(); ++c)
			if (int(written->value(c, r)) != int(image.constScanLine(r)[c]))
				return false;
	return true;
}

}  // namespace



/**
 * @test Tests the step `hill-shade` of the headless runner.
 *
 * See the design notes, "Bicubic resampling for hill shading", Accept (7).
 */
class RunnerElevationTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString map_path;
	QString bare_map_path;
	QString grid_path;

	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}

	JobResult runJob(const char* name, const QString& map, QList<QJsonObject> steps) const
	{
		steps.prepend(stepOf("open", {{ QStringLiteral("path"), map }}));
		QJsonArray array;
		for (auto const& step : steps)
			array.append(step);
		auto const job_path = file(name);
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return {};
			job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), array } }).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return {};
		return { process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError() };
	}

private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());

		// The grid of Accept (1), moved to (500000, 5200000): 0 except column 3, 16.
		std::vector<float> values(64, 0.0f);
		for (int r = 0; r < 8; ++r)
			values[std::size_t(r) * 8 + 3] = 16.0f;
		grid_path = file("terrain.tif");
		QVERIFY(ElevationModel(8, 8, QPointF(500000.0, 5200008.0), 1.0, std::move(values), utm14).save(grid_path));

		Map map;
		georeference(map);
		map.elevationSet().setHref(ElevationSet::Terrain, grid_path);
		map_path = file("map.omap");
		QVERIFY(saveMap(map, map_path));

		Map bare;
		georeference(bare);
		bare_map_path = file("bare.omap");
		QVERIFY(saveMap(bare, bare_map_path));
	}

	void hillShadeStepTest()
	{
		ElevationModel::OpenOptions options;
		options.target_crs = utm14;
		auto const grid = ElevationModel::open(grid_path, options);
		QVERIFY(grid);
		HillShadeParams params;
		params.cell_size = 0.5;
		auto const cubic = hillShade(*grid, params);
		params.resampling = HillShadeParams::Bilinear;
		auto const linear = hillShade(*grid, params);
		QVERIFY(cubic.image != linear.image);

		auto result = runJob("cubic.json", map_path, { stepOf("hill-shade", {
			{ QStringLiteral("cell_size"), 0.5 },
			{ QStringLiteral("out"), QStringLiteral("cubic.tif") },
		}) });
		QVERIFY2(result.exit_code == 0, result.all().constData());
		auto const log = result.logOf("hill-shade");
		QCOMPARE(log.value(QLatin1String("path")).toString(), file("cubic.tif"));
		QCOMPARE(log.value(QLatin1String("width")).toInt(), 15);
		QCOMPARE(log.value(QLatin1String("height")).toInt(), 15);
		QVERIFY(sameGrey(file("cubic.tif"), cubic.image));

		result = runJob("linear.json", map_path, { stepOf("hill-shade", {
			{ QStringLiteral("cell_size"), 0.5 },
			{ QStringLiteral("resampling"), QStringLiteral("bilinear") },
			{ QStringLiteral("out"), QStringLiteral("linear.tif") },
		}) });
		QVERIFY2(result.exit_code == 0, result.all().constData());
		QVERIFY(sameGrey(file("linear.tif"), linear.image));

		// the design notes, "The gradient on the corners of 2 by 2 cells", Accept (5).
		HillShadeParams horn_params;
		horn_params.gradient = GradientMethod::Horn;
		auto const horn = hillShade(*grid, horn_params);
		QCOMPARE(horn.image.size(), QSize(8, 8));
		result = runJob("horn.json", map_path, { stepOf("hill-shade", {
			{ QStringLiteral("gradient"), QStringLiteral("horn") },
			{ QStringLiteral("out"), QStringLiteral("horn.tif") },
		}) });
		QVERIFY2(result.exit_code == 0, result.all().constData());
		QVERIFY(sameGrey(file("horn.tif"), horn.image));
	}

	void hillShadeStepFailures()
	{
		auto result = runJob("nearest.json", map_path, { stepOf("hill-shade", {
			{ QStringLiteral("resampling"), QStringLiteral("nearest") },
			{ QStringLiteral("out"), QStringLiteral("nearest.tif") },
		}) });
		QVERIFY2(result.exit_code == 2, result.all().constData());

		result = runJob("centre.json", map_path, { stepOf("hill-shade", {
			{ QStringLiteral("gradient"), QStringLiteral("centre") },
			{ QStringLiteral("out"), QStringLiteral("centre.tif") },
		}) });
		QVERIFY2(result.exit_code == 2, result.all().constData());

		result = runJob("bare.json", bare_map_path, { stepOf("hill-shade", {
			{ QStringLiteral("out"), QStringLiteral("bare.tif") },
		}) });
		QVERIFY2(result.exit_code == 1, result.all().constData());
		QVERIFY(!QFile::exists(file("bare.tif")));
	}
};


QTEST_MAIN(RunnerElevationTest)
#include "runner_elevation_t.moc"  // IWYU pragma: keep
