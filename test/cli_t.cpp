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


#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_color.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/xml_file_format.h"
#include "fileformats/file_import_export.h"
#include "image-difference.h"

using namespace OpenOrienteering;


namespace {

/// The exit codes of the headless runner, see the design notes.
enum ExitCode
{
	Success      = 0,
	FailedStep   = 1,
	MalformedJob = 2
};

struct RunResult
{
	int exit_code;
	QByteArray out;
	QByteArray err;
	
	QList<QByteArray> logLines() const
	{
		QList<QByteArray> lines;
		for (auto const& line : out.split('\n'))
		{
			if (!line.trimmed().isEmpty())
				lines.push_back(line.trimmed());
		}
		return lines;
	}
};

}  // namespace



/**
 * @test Tests the headless runner foom-cli, see "Headless runner" in the design notes.
 */
class CliTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		
		executable = QString::fromUtf8(FOOM_CLI_EXECUTABLE);
		QVERIFY2(QFileInfo::exists(executable), qPrintable(executable));
		
		test_data_dir = QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data"));
		QVERIFY(QFileInfo(test_data_dir).isDir());
		
		QVERIFY(dir.isValid());
	}
	
	
	/// Accept (1): PNG export at 300 dpi matches the stored reference.
	void pngExportTest()
	{
		auto const out_path = outPath(QStringLiteral("clipping.png"));
		auto const job = writeJob(QStringLiteral("png.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("export/issue-1024-clipping.xmap")) }}),
			step(QStringLiteral("export"), {
				{ QStringLiteral("format"), QStringLiteral("png") },
				{ QStringLiteral("path"), out_path },
				{ QStringLiteral("extent"), QStringLiteral("print-area") },
				{ QStringLiteral("dpi"), 300 },
			}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		QVERIFY(QFileInfo::exists(out_path));
		
		auto const lines = result.logLines();
		QCOMPARE(lines.size(), 2);
		for (auto const& line : lines)
		{
			auto const entry = QJsonDocument::fromJson(line).object();
			QVERIFY2(entry.value(QLatin1String("ok")).toBool(false), line);
		}
		QCOMPARE(QJsonDocument::fromJson(lines[1]).object().value(QLatin1String("step")).toString(), QStringLiteral("export"));
		
		QImage image;
		QVERIFY(image.load(out_path));
		QCOMPARE(image.dotsPerMeterX(), qRound(300 / 25.4 * 1000));
		
		QImage expected_image;
		auto const reference_path = testData(QStringLiteral("jobs/issue-1024-clipping-300dpi.png"));
		QVERIFY2(expected_image.load(reference_path), qPrintable(reference_path));
		image = image.convertToFormat(expected_image.format());
		QCOMPARE(fuzzyDifference(image, expected_image), QPoint(-1, -1));
	}
	
	
	/// Accept (10): text is drawn as text, on the platform the runner chooses
	/// by itself and on the offscreen platform.
	void textExportTest_data()
	{
		QTest::addColumn<QString>("platform");
		QTest::newRow("default") << QString{};
		QTest::newRow("offscreen") << QStringLiteral("offscreen");
	}
	
	void textExportTest()
	{
		QFETCH(QString, platform);
		
		// The word FOOM, 20 mm high, in the middle of 100 mm by 40 mm.
		Map map;
		map.setScaleDenominator(10000);
		auto* black = new MapColor(QStringLiteral("Black"), 0);
		black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
		black->setRgbFromCmyk();
		map.addColor(black, 0);
		auto* symbol = new TextSymbol();
		symbol->setName(QStringLiteral("text"));
		symbol->setNumberComponent(0, 1);
		symbol->setColor(black);
		symbol->setFontFamily(QStringLiteral("Arial"));
		symbol->setFontSize(20000);
		map.addSymbol(symbol, 0);
		auto* text = new TextObject(symbol);
		text->setAnchorPosition(MapCoordF(50, 20));
		text->setText(QStringLiteral("FOOM"));
		text->setHorizontalAlignment(TextObject::AlignHCenter);
		text->setVerticalAlignment(TextObject::AlignVCenter);
		map.addObject(text);
		auto const map_path = outPath(QStringLiteral("text.omap"));
		{
			XMLFileFormat format;
			auto exporter = format.makeExporter(map_path, &map, nullptr);
			QVERIFY(exporter && exporter->doExport());
		}
		
		auto const out_path = outPath(QStringLiteral("text-%1.png").arg(QString::fromLatin1(QTest::currentDataTag())));
		auto const job = writeJob(QStringLiteral("text.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), map_path }}),
			step(QStringLiteral("export"), {
				{ QStringLiteral("format"), QStringLiteral("png") },
				{ QStringLiteral("path"), out_path },
				{ QStringLiteral("extent"), QJsonArray{ 0, 0, 100, 40 } },
				{ QStringLiteral("dpi"), 100 },
			}),
		});
		auto const result = run(job, platform);
		QVERIFY2(result.exit_code == int(Success), result.err.constData());
		
		QImage image;
		QVERIFY(image.load(out_path));
		QVERIFY(image.width() > 380 && image.height() > 150);
		
		// Letters cover a small share of the picture; the boxes which stand
		// for letters where there are no fonts cover a third and more.
		auto dark = 0;
		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
				dark += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
		}
		auto const share = double(dark) / (image.width() * image.height());
		QVERIFY2(share > 0.03, qPrintable(QString::number(share)));
		QVERIFY2(share < 0.25, qPrintable(QString::number(share)));
		
		// The row through the middle crosses the four letters: it changes
		// between dark and light many times, where a box changes twice.
		auto changes = 0;
		auto const y = image.height() / 2;
		for (int x = 1; x < image.width(); ++x)
			changes += (qGray(image.pixel(x, y)) < 128) != (qGray(image.pixel(x - 1, y)) < 128) ? 1 : 0;
		QVERIFY2(changes >= 8, qPrintable(QString::number(changes)));
	}
	
	
	/// PDF export writes a PDF file.
	void pdfExportTest()
	{
		auto const out_path = outPath(QStringLiteral("clipping.pdf"));
		auto const job = writeJob(QStringLiteral("pdf.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("export/issue-1024-clipping.xmap")) }}),
			step(QStringLiteral("export"), {
				{ QStringLiteral("path"), out_path },  // format from the extension
				{ QStringLiteral("extent"), QStringLiteral("map") },
				{ QStringLiteral("mode"), QStringLiteral("vector") },
			}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		
		QFile file(out_path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.read(5), QByteArray("%PDF-"));
	}
	
	
	/// change-scale and save-as to .foom and to OCD.
	void changeScaleAndSaveAsTest()
	{
		auto const foom_path = outPath(QStringLiteral("rotated-10000.foom"));
		auto const ocd_path = outPath(QStringLiteral("rotated-10000.ocd"));
		auto const job = writeJob(QStringLiteral("scale.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("change-scale"), {
				{ QStringLiteral("scale"), 10000 },
				{ QStringLiteral("scale_symbols"), false },
			}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), foom_path }}),
			step(QStringLiteral("save-as"), {
				{ QStringLiteral("path"), ocd_path },
				{ QStringLiteral("format"), QStringLiteral("ocd") },
				{ QStringLiteral("version"), 12 },
			}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		QCOMPARE(result.logLines().size(), 4);
		
		Map original;
		QVERIFY(original.loadFrom(testData(QStringLiteral("rotated.omap"))));
		QCOMPARE(original.getScaleDenominator(), 4000u);
		QCOMPARE(original.getNumObjects(), 4);
		
		{
			Map map;
			QVERIFY(map.loadFrom(foom_path));
			QCOMPARE(map.getScaleDenominator(), 10000u);
			QCOMPARE(map.getNumObjects(), 4);
			// scale_symbols false: symbol dimensions unchanged
			QCOMPARE(map.getNumSymbols(), original.getNumSymbols());
			QVERIFY(map.getSymbol(0)->equals(original.getSymbol(0), Qt::CaseSensitive));
			
			QFile file(foom_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QVERIFY(file.readAll().contains("foom=\"1\""));
		}
		{
			Map map;
			QVERIFY(map.loadFrom(ocd_path));
			QCOMPARE(map.getScaleDenominator(), 10000u);
			QCOMPARE(map.getNumObjects(), 4);
			
			QFile file(ocd_path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			auto const header = file.read(6);  // Ocd::FileHeaderGeneric
			QCOMPARE(quint8(header[0]) | (quint8(header[1]) << 8), 0x0cad);
			QCOMPARE(quint8(header[4]) | (quint8(header[5]) << 8), 12);
		}
	}
	
	
	/// set-georeferencing with a UTM zone, a geographic reference point and a declination.
	void setGeoreferencingTest()
	{
		auto const foom_path = outPath(QStringLiteral("georeferenced.foom"));
		auto const job = writeJob(QStringLiteral("georef.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("set-georeferencing"), {
				{ QStringLiteral("crs"), QStringLiteral("UTM:14 N") },
				{ QStringLiteral("ref_point"), QJsonObject{
					{ QStringLiteral("map"), QJsonArray{ 10.0, -20.0 } },
					{ QStringLiteral("geographic"), QJsonArray{ 59.9, 15.0 } },
				} },
				{ QStringLiteral("declination"), 3.5 },
			}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), foom_path }}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		
		Map map;
		QVERIFY(map.loadFrom(foom_path));
		auto const& georef = map.getGeoreferencing();
		QCOMPARE(georef.getState(), Georeferencing::Geospatial);
		QCOMPARE(georef.getProjectedCRSId(), QStringLiteral("UTM"));
		QVERIFY(georef.getProjectedCRSSpec().contains(QLatin1String("+zone=14")));
		QCOMPARE(georef.getMapRefPoint(), MapCoord(10.0, -20.0));
		QVERIFY(qAbs(georef.getGeographicRefPoint().latitude() - 59.9) < 0.00001);
		QVERIFY(qAbs(georef.getGeographicRefPoint().longitude() - 15.0) < 0.00001);
		QVERIFY(qAbs(georef.getDeclination() - 3.5) < 0.01);
	}
	
	
	/// import adds the objects of another map; apply-crt reassigns symbols.
	void importAndApplyCrtTest()
	{
		auto const foom_path = outPath(QStringLiteral("imported.foom"));
		// rotated-pattern.omap: 6 objects; one area object each with the area
		// symbols 3, 4 and 5. barrier.omap: one text object.
		// The CRT moves the objects of symbol 4 to symbol 3.
		auto const crt_path = writeFile(QStringLiteral("rules.crt"), QByteArray("# comment\n3   4\n"));
		auto const job = writeJob(QStringLiteral("import.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated-pattern.omap")) }}),
			step(QStringLiteral("import"), {{ QStringLiteral("path"), testData(QStringLiteral("barrier.omap")) }}),
			step(QStringLiteral("apply-crt"), {{ QStringLiteral("path"), crt_path }}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), foom_path }}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		auto const lines = result.logLines();
		QCOMPARE(lines.size(), 4);
		QCOMPARE(QJsonDocument::fromJson(lines[1]).object().value(QLatin1String("objects")).toInt(), 1);
		QCOMPARE(QJsonDocument::fromJson(lines[2]).object().value(QLatin1String("rules")).toInt(), 1);
		
		Map map;
		QVERIFY(map.loadFrom(foom_path));
		QCOMPARE(map.getNumObjects(), 7);
		QCOMPARE(countObjects(map, QStringLiteral("3")), 2);
		QCOMPARE(countObjects(map, QStringLiteral("4")), 0);
		QCOMPARE(countObjects(map, QStringLiteral("5")), 1);
	}
	
	
	/// replace-symbols matches the used symbols against another symbol set.
	void replaceSymbolsTest()
	{
		auto const foom_path = outPath(QStringLiteral("replaced.foom"));
		// rotated.omap uses symbols 1, 2 and 3. rotated-pattern.omap has the
		// same three plus the area symbols 4 and 5. The step imports the whole
		// set and removes the replaced symbols, as the editor's dialog does.
		auto const job = writeJob(QStringLiteral("replace.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("replace-symbols"), {
				{ QStringLiteral("path"), testData(QStringLiteral("rotated-pattern.omap")) },
				{ QStringLiteral("match"), QStringLiteral("number") },
			}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), foom_path }}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		auto const lines = result.logLines();
		QCOMPARE(lines.size(), 3);
		auto const entry = QJsonDocument::fromJson(lines[1]).object();
		QCOMPARE(entry.value(QLatin1String("replaced")).toInt(), 3);
		QVERIFY(!entry.contains(QLatin1String("unmatched")));
		
		Map map;
		QVERIFY(map.loadFrom(foom_path));
		QCOMPARE(map.getNumObjects(), 4);
		QCOMPARE(map.getNumSymbols(), 5);
		QCOMPARE(countObjects(map, QStringLiteral("1")), 1);
		QCOMPARE(countObjects(map, QStringLiteral("2")), 2);
		QCOMPARE(countObjects(map, QStringLiteral("3")), 1);
	}
	
	
	/// Accept (2): malformed jobs exit 2 and write nothing.
	void malformedJobTest_data()
	{
		QTest::addColumn<QByteArray>("content");
		
		QTest::newRow("not JSON") << QByteArray("this is not JSON");
		QTest::newRow("not an object") << QByteArray("42");
		QTest::newRow("step without name") << jobDocument({
			QJsonObject{{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }},
		});
		QTest::newRow("unknown step") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("frobnicate"), {}),
		});
		QTest::newRow("open without path") << jobDocument({
			step(QStringLiteral("open"), {}),
		});
		QTest::newRow("change-scale without scale") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("change-scale"), {{ QStringLiteral("scale_symbols"), true }}),
		});
		QTest::newRow("change-scale with text scale") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("change-scale"), {{ QStringLiteral("scale"), QStringLiteral("10000") }}),
		});
		QTest::newRow("save-as before open") << jobDocument({
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), outPath(QStringLiteral("malformed.foom")) }}),
		});
		QTest::newRow("export without path") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), outPath(QStringLiteral("malformed.foom")) }}),
			step(QStringLiteral("export"), {{ QStringLiteral("format"), QStringLiteral("png") }}),
		});
		QTest::newRow("export with unknown mode") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("export"), {
				{ QStringLiteral("path"), outPath(QStringLiteral("malformed.png")) },
				{ QStringLiteral("mode"), QStringLiteral("colourful") },
			}),
		});
		// Features which do not exist yet are reported, not ignored.
		QTest::newRow("check is unsupported") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("check"), {
				{ QStringLiteral("rules"), QJsonArray{ QStringLiteral("min-width") } },
				{ QStringLiteral("out"), outPath(QStringLiteral("malformed.csv")) },
			}),
		});
		QTest::newRow("export course is unsupported") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("export"), {
				{ QStringLiteral("path"), outPath(QStringLiteral("malformed.pdf")) },
				{ QStringLiteral("course"), QStringLiteral("H21") },
			}),
		});
		QTest::newRow("import clip needs four numbers") << jobDocument({
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("import"), {
				{ QStringLiteral("path"), testData(QStringLiteral("barrier.omap")) },
				{ QStringLiteral("clip"), QJsonArray{ 0, 0, 10 } },
			}),
		});
	}
	
	void malformedJobTest()
	{
		QFETCH(QByteArray, content);
		
		auto const job = writeFile(QStringLiteral("malformed.json"), content);
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(MalformedJob));
		QVERIFY2(!result.err.trimmed().isEmpty(), "A malformed job must be explained on stderr");
		QVERIFY(result.logLines().isEmpty());
		
		QVERIFY(!QFileInfo::exists(outPath(QStringLiteral("malformed.foom"))));
		QVERIFY(!QFileInfo::exists(outPath(QStringLiteral("malformed.png"))));
		QVERIFY(!QFileInfo::exists(outPath(QStringLiteral("malformed.pdf"))));
		QVERIFY(!QFileInfo::exists(outPath(QStringLiteral("malformed.csv"))));
	}
	
	
	/// Accept (3): a failing step exits 1 after the earlier steps, and the log stops there.
	void failedStepTest()
	{
		auto const first_output = outPath(QStringLiteral("before-failure.foom"));
		auto const later_output = outPath(QStringLiteral("after-failure.foom"));
		auto const job = writeJob(QStringLiteral("failing.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), testData(QStringLiteral("rotated.omap")) }}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), first_output }}),
			step(QStringLiteral("open"), {{ QStringLiteral("path"), outPath(QStringLiteral("does-not-exist.omap")) }}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), later_output }}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(FailedStep));
		QVERIFY(QFileInfo::exists(first_output));
		QVERIFY(!QFileInfo::exists(later_output));
		
		auto const lines = result.logLines();
		QCOMPARE(lines.size(), 3);
		auto const failed = QJsonDocument::fromJson(lines[2]).object();
		QCOMPARE(failed.value(QLatin1String("step")).toString(), QStringLiteral("open"));
		QCOMPARE(failed.value(QLatin1String("ok")).toBool(true), false);
		QVERIFY(!failed.value(QLatin1String("message")).toString().isEmpty());
	}
	
	
	/// Paths in the job file are relative to the job file.
	void relativePathTest()
	{
		QVERIFY(QFile::copy(testData(QStringLiteral("rotated.omap")), outPath(QStringLiteral("relative-in.omap"))));
		auto const job = writeJob(QStringLiteral("relative.json"), {
			step(QStringLiteral("open"), {{ QStringLiteral("path"), QStringLiteral("relative-in.omap") }}),
			step(QStringLiteral("save-as"), {{ QStringLiteral("path"), QStringLiteral("relative-out.foom") }}),
		});
		
		auto const result = run(job);
		QCOMPARE(result.exit_code, int(Success));
		QVERIFY(QFileInfo::exists(outPath(QStringLiteral("relative-out.foom"))));
	}


private:
	static int countObjects(const Map& map, const QString& symbol_number)
	{
		auto count = 0;
		auto const* part = map.getPart(0);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* symbol = part->getObject(i)->getSymbol();
			if (symbol && symbol->getNumberAsString() == symbol_number)
				++count;
		}
		return count;
	}
	
	QString testData(const QString& name) const
	{
		return QDir(test_data_dir).absoluteFilePath(name);
	}
	
	QString outPath(const QString& name) const
	{
		return QDir(dir.path()).absoluteFilePath(name);
	}
	
	static QJsonObject step(const QString& name, QJsonObject params)
	{
		params.insert(QStringLiteral("step"), name);
		return params;
	}
	
	static QByteArray jobDocument(const QList<QJsonObject>& steps)
	{
		QJsonArray array;
		for (auto const& step : steps)
			array.append(step);
		return QJsonDocument(QJsonObject{{ QStringLiteral("steps"), array }}).toJson();
	}
	
	QString writeFile(const QString& name, const QByteArray& content) const
	{
		auto const path = outPath(name);
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(content) != content.size())
			return {};
		return path;
	}
	
	QString writeJob(const QString& name, const QList<QJsonObject>& steps) const
	{
		return writeFile(name, jobDocument(steps));
	}
	
	/// Runs the job; with an empty platform, on the one the runner chooses by itself.
	RunResult run(const QString& job_path, const QString& platform = QStringLiteral("offscreen")) const
	{
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.remove(QStringLiteral("QT_QPA_FONTDIR"));
		if (platform.isEmpty())
			env.remove(QStringLiteral("QT_QPA_PLATFORM"));
		else
			env.insert(QStringLiteral("QT_QPA_PLATFORM"), platform);  // Accept (4)
		process.setProcessEnvironment(env);
		process.start(executable, { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return { -1, {}, QByteArray("timeout") };
		if (process.exitStatus() != QProcess::NormalExit)
			return { -2, process.readAllStandardOutput(), process.readAllStandardError() };
		return { process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError() };
	}
	
	QString executable;
	QString test_data_dir;
	QTemporaryDir dir;
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(CliTest)
#include "cli_t.moc"  // IWYU pragma: keep
