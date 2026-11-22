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
#include <vector>

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"

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

struct JobResult
{
	int exit_code = -1;
	QByteArray out;
	QByteArray err;
	
	/// The log lines of the steps with the name, in order.
	QList<QJsonObject> logsOf(const char* step) const
	{
		QList<QJsonObject> result;
		for (auto const& line : out.split('\n'))
		{
			auto const object = QJsonDocument::fromJson(line).object();
			if (object.value(QLatin1String("step")).toString() == QLatin1String(step))
				result.push_back(object);
		}
		return result;
	}
};

QJsonObject stepOf(const char* name, QJsonObject params)
{
	params.insert(QStringLiteral("step"), QString::fromLatin1(name));
	return params;
}

QJsonObject generate(const char* generator, QJsonObject params)
{
	params.insert(QStringLiteral("generator"), QString::fromLatin1(generator));
	return stepOf("generate", params);
}

QJsonArray pair(double x, double y)
{
	return QJsonArray{ x, y };
}

/// The objects of a part with the generator's tag.
std::vector<const Object*> generated(const Map& map, int part_index, const char* generator)
{
	std::vector<const Object*> result;
	if (part_index < 0)
		return result;
	auto const* part = map.getPart(std::size_t(part_index));
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto const* object = part->getObject(i);
		if (object->getTag(QStringLiteral("generator")) == QLatin1String(generator))
			result.push_back(object);
	}
	return result;
}

MapCoordF coordOf(const Object* object, int index)
{
	return MapCoordF(object->getRawCoordinateVector()[MapCoordVector::size_type(index)]);
}

}  // namespace



/**
 * @test Tests the steps `generate` and `regenerate` of the headless runner.
 *
 * See the design notes, "Generators", third change, Accept (8) to (10),
 * and the design notes, "Headless runner".
 */
class RunnerGenerateTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	QString map_path;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	JobResult runJob(const char* name, QList<QJsonObject> steps, const QString& open_path) const
	{
		if (!open_path.isEmpty())
			steps.prepend(stepOf("open", {{ QStringLiteral("path"), open_path }}));
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
	
	JobResult runJob(const char* name, const QList<QJsonObject>& steps) const
	{
		return runJob(name, steps, map_path);
	}

private slots:
	void initTestCase();
	void generateTest();
	void failureTest();
	void malformedTest();
};


void RunnerGenerateTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
	QVERIFY(dir.isValid());
	map_path = file("generate.foom");
	Map map;
	map.setScaleDenominator(10000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setName(QStringLiteral("Line"));
	line->setNumberComponent(0, 101);
	line->setColor(black);
	line->setLineWidth(0.2);
	map.addSymbol(line, 0);
	QVERIFY(saveMap(map, map_path));
}


/// Accept (8)
void RunnerGenerateTest::generateTest()
{
	auto const first = runJob("generate.json", {
		generate("gen-scale-bar", {{ QStringLiteral("at"), pair(10, 20) }}),
		generate("gen-qr-code", {
			{ QStringLiteral("at"), pair(100, 20) },
			{ QStringLiteral("text"), QStringLiteral("FOOM") },
			{ QStringLiteral("background"), false },
		}),
		generate("gen-north-lines", {
			{ QStringLiteral("spacing_m"), 300 },
			{ QStringLiteral("outline"), QJsonArray{ pair(5, -65), pair(105, -65), pair(105, -5), pair(5, -5) } },
		}),
		generate("gen-legend", {{ QStringLiteral("at"), pair(0, 100) }}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("a.foom") }}),
	});
	QVERIFY2(first.exit_code == 0, QByteArray(first.out + first.err).constData());
	auto const logs = first.logsOf("generate");
	QCOMPARE(logs.size(), 4);
	QCOMPARE(logs[0].value(QLatin1String("generator")).toString(), QStringLiteral("gen-scale-bar"));
	QCOMPARE(logs[0].value(QLatin1String("part")).toString(), QStringLiteral("layout"));
	QVERIFY(logs[0].value(QLatin1String("group")).toInt() > 0);
	QVERIFY(logs[0].value(QLatin1String("objects")).toInt() > 6);
	QCOMPARE(logs[1].value(QLatin1String("objects")).toInt(), 1);
	QCOMPARE(logs[2].value(QLatin1String("part")).toString(), QStringLiteral("map"));
	QCOMPARE(logs[2].value(QLatin1String("objects")).toInt(), 3);
	QVERIFY(logs[3].value(QLatin1String("group")).toInt() != logs[0].value(QLatin1String("group")).toInt());
	
	Map map;
	QVERIFY(loadMap(map, file("a.foom")));
	auto const layout = map.findPartIndexWithRole(MapPart::Role::Layout);
	auto const content = map.findPartIndexWithRole(MapPart::Role::Map);
	QVERIFY(layout >= 0);
	QVERIFY(content >= 0);
	
	auto const bar = generated(map, layout, "gen-scale-bar");
	QVERIFY(!bar.empty());
	auto const* frame = bar.front();
	QCOMPARE(frame->getType(), Object::Path);
	QVERIFY(std::abs(coordOf(frame, 0).x() - 10) < 0.002 && std::abs(coordOf(frame, 0).y() - 20) < 0.002);
	QVERIFY(std::abs(coordOf(frame, 1).x() - 60) < 0.002);
	
	auto const qr = generated(map, layout, "gen-qr-code");
	QCOMPARE(qr.size(), std::size_t(1));
	QCOMPARE(qr.front()->getType(), Object::Path);
	QVERIFY(!generated(map, layout, "gen-legend").empty());
	
	auto const lines = generated(map, content, "gen-north-lines");
	QCOMPARE(lines.size(), std::size_t(3));
	std::vector<double> xs;
	for (auto const* line : lines)
	{
		QVERIFY(std::abs(coordOf(line, 0).x() - coordOf(line, 1).x()) < 0.002);
		xs.push_back(coordOf(line, 0).x());
	}
	std::sort(xs.begin(), xs.end());
	QVERIFY(std::abs(xs[0] - 30) < 0.002 && std::abs(xs[1] - 60) < 0.002 && std::abs(xs[2] - 90) < 0.002);
	
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			QVERIFY(!object->getTag(QStringLiteral("generator")).isEmpty());
			QVERIFY(!object->getTag(QStringLiteral("params")).isEmpty());
		}
	}
	
	auto const second = runJob("regenerate.json", {
		stepOf("change-scale", {{ QStringLiteral("scale"), 15000 }}),
		stepOf("regenerate", {{ QStringLiteral("generators"), QJsonArray{ QStringLiteral("gen-scale-bar") } }}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("b.foom") }}),
	}, file("a.foom"));
	QVERIFY2(second.exit_code == 0, QByteArray(second.out + second.err).constData());
	QCOMPARE(second.logsOf("regenerate").value(0).value(QLatin1String("groups")).toInt(), 1);
	
	Map rebuilt;
	QVERIFY(loadMap(rebuilt, file("b.foom")));
	auto const rebuilt_bar = generated(rebuilt, rebuilt.findPartIndexWithRole(MapPart::Role::Layout), "gen-scale-bar");
	QVERIFY(!rebuilt_bar.empty());
	auto const length = coordOf(rebuilt_bar.front(), 1).x() - coordOf(rebuilt_bar.front(), 0).x();
	QVERIFY2(std::abs(length - 100.0 / 3) < 0.002, qPrintable(QString::number(length)));
	QStringList texts;
	for (auto const* object : rebuilt_bar)
	{
		if (object->getType() == Object::Text)
			texts.append(object->asText()->getText());
	}
	QVERIFY2(texts.contains(QStringLiteral("500 m")), qPrintable(texts.join(QLatin1Char('|'))));
}


/// Accept (9)
void RunnerGenerateTest::failureTest()
{
	auto result = runJob("symbol.json", {
		generate("gen-north-lines", {
			{ QStringLiteral("line_symbol"), QStringLiteral("999") },
			{ QStringLiteral("outline"), QJsonArray{ pair(5, -65), pair(105, -65), pair(105, -5) } },
		}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("c.foom") }}),
	});
	QCOMPARE(result.exit_code, 1);
	auto log = result.logsOf("generate").value(0);
	QCOMPARE(log.value(QLatin1String("ok")).toBool(true), false);
	QVERIFY2(log.value(QLatin1String("message")).toString().contains(QLatin1String("999")), result.out.constData());
	QVERIFY(result.logsOf("save-as").isEmpty());
	QVERIFY(!QFileInfo::exists(file("c.foom")));
	
	result = runJob("qr.json", {
		generate("gen-qr-code", {{ QStringLiteral("at"), pair(0, 0) }}),
		stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("d.foom") }}),
	});
	QCOMPARE(result.exit_code, 1);
	log = result.logsOf("generate").value(0);
	QCOMPARE(log.value(QLatin1String("ok")).toBool(true), false);
	QVERIFY(!log.value(QLatin1String("message")).toString().isEmpty());
	QVERIFY(!QFileInfo::exists(file("d.foom")));
}


/// Accept (10)
void RunnerGenerateTest::malformedTest()
{
	auto const square = QJsonArray{ pair(0, 0), pair(10, 0), pair(10, 10) };
	const QList<QJsonObject> malformed = {
		generate("gen-nothing", {{ QStringLiteral("at"), pair(0, 0) }}),
		generate("gen-scale-bar", {{ QStringLiteral("at"), pair(0, 0) }, { QStringLiteral("lenght_m"), 500 }}),
		generate("gen-scale-bar", {{ QStringLiteral("at"), pair(0, 0) }, { QStringLiteral("divisions"), QStringLiteral("5") }}),
		generate("gen-scale-bar", {{ QStringLiteral("at"), pair(0, 0) }, { QStringLiteral("divisions"), 0 }}),
		generate("gen-scale-bar", {{ QStringLiteral("at"), pair(0, 0) }, { QStringLiteral("style"), QStringLiteral("dots") }}),
		generate("gen-scale-bar", {}),
		generate("gen-grid", {{ QStringLiteral("at"), pair(0, 0) }}),
		generate("gen-grid", {{ QStringLiteral("clip"), QStringLiteral("selection") }}),
		generate("gen-grid", {{ QStringLiteral("outline"), QJsonArray{ pair(0, 0), pair(10, 0) } }}),
		generate("gen-grid", {{ QStringLiteral("outline"), square }, { QStringLiteral("clip"), QStringLiteral("extent") }}),
		stepOf("regenerate", {{ QStringLiteral("generators"), QJsonArray{ QStringLiteral("gen-nothing") } }}),
	};
	auto index = 0;
	for (auto const& step : malformed)
	{
		const QByteArray name = QByteArray("malformed") + QByteArray::number(index++) + ".json";
		auto const result = runJob(name.constData(), { step, stepOf("save-as", {{ QStringLiteral("path"), QStringLiteral("e.foom") }}) });
		QVERIFY2(result.exit_code == 2, QJsonDocument(step).toJson(QJsonDocument::Compact).constData());
		QVERIFY2(!result.err.isEmpty(), QJsonDocument(step).toJson(QJsonDocument::Compact).constData());
		QVERIFY(!QFileInfo::exists(file("e.foom")));
	}
	
	// Before an open.
	auto const result = runJob("no-map.json", { generate("gen-scale-bar", {{ QStringLiteral("at"), pair(0, 0) }}) }, QString{});
	QCOMPARE(result.exit_code, 2);
}


QTEST_MAIN(RunnerGenerateTest)
#include "runner_generate_t.moc"  // IWYU pragma: keep
