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


#include <map>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <gdal.h>
#include <ogr_api.h>

#include "global.h"
#include "core/dxf_crt.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "fileformats/xml_file_format.h"
#include "gdal/dxf_colours.h"
#include "gdal/ogr_file_format_p.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"

using namespace OpenOrienteering;


namespace {

const QByteArray table_text =
        "; comment\n"
        "501\troads\t3\n"
        "502.1 \"foot paths\"\n"
        "\n"
        "503 water 5 extra\n"
        "x y\n"
        "504 lake 300\n"
        "501 again 4\n";

MapColor* addColor(Map& map, const QString& name, float r, float g, float b)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setRgb(MapColorRgb(r, g, b));
	color->setCmykFromRgb();
	map.addColor(color, map.getNumColors());
	return color;
}

LineSymbol* addLine(Map& map, const QString& name, int number, int sub, const MapColor* color)
{
	auto* symbol = new LineSymbol();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	if (sub >= 0)
		symbol->setNumberComponent(1, sub);
	symbol->setLineWidth(0.5);
	symbol->setColor(color);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

void addPath(Map& map, const Symbol* symbol, MapCoordVector coords)
{
	map.addObject(new PathObject(symbol, coords));
}

/// Road 501, foot path 502.1, stream 503, open land 401, one object each.
void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto* red = addColor(map, QStringLiteral("Red"), 1, 0, 0);
	auto* black = addColor(map, QStringLiteral("Black"), 0, 0, 0);
	auto* blue = addColor(map, QStringLiteral("Blue"), 0, 0, 1);
	auto* yellow = addColor(map, QStringLiteral("Yellow"), 1, 1, 0);
	addPath(map, addLine(map, QStringLiteral("Road"), 501, -1, red), { MapCoord(0, 0), MapCoord(10, 0) });
	addPath(map, addLine(map, QStringLiteral("Foot path"), 502, 1, black), { MapCoord(0, 5), MapCoord(10, 5) });
	addPath(map, addLine(map, QStringLiteral("Stream"), 503, -1, blue), { MapCoord(0, 10), MapCoord(10, 10) });
	auto* area = new AreaSymbol();
	area->setName(QStringLiteral("Open land"));
	area->setNumberComponent(0, 401);
	area->setColor(yellow);
	map.addSymbol(area, map.getNumSymbols());
	MapCoordVector square { MapCoord(20, 0), MapCoord(30, 0), MapCoord(30, 10), MapCoord(20, 10), MapCoord(20, 0) };
	square.back().setClosePoint(true);
	addPath(map, area, square);
}

struct DxfFeature
{
	QString layer;
	QString style;
};

/// The features of a DXF file as OGR reads them.
std::vector<DxfFeature> readFeatures(const QString& path)
{
	std::vector<DxfFeature> features;
	auto* dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
	if (!dataset)
		return features;
	for (int i = 0; i < GDALDatasetGetLayerCount(dataset); ++i)
	{
		auto* layer = GDALDatasetGetLayer(dataset, i);
		OGR_L_ResetReading(layer);
		while (auto* feature = OGR_L_GetNextFeature(layer))
		{
			auto const index = OGR_F_GetFieldIndex(feature, "Layer");
			auto const* style = OGR_F_GetStyleString(feature);
			features.push_back({ QString::fromUtf8(OGR_F_GetFieldAsString(feature, index)), QString::fromLatin1(style ? style : "") });
			OGR_F_Destroy(feature);
		}
	}
	GDALClose(dataset);
	return features;
}

const DxfFeature* featureOn(const std::vector<DxfFeature>& features, const QString& layer)
{
	for (auto const& feature : features)
	{
		if (feature.layer == layer)
			return &feature;
	}
	return nullptr;
}

/// The group codes and values of the first entity on the layer, from the DXF text.
std::map<int, QString> entityGroups(const QString& path, const QString& layer)
{
	std::map<int, QString> groups;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return groups;
	auto const lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));
	auto in_entities = false;
	std::map<int, QString> current;
	for (int i = 0; i + 1 < lines.size(); i += 2)
	{
		auto const code = lines[i].trimmed().toInt();
		auto const value = lines[i + 1].trimmed();
		if (code == 2 && value == QLatin1String("ENTITIES"))
			in_entities = true;
		if (!in_entities)
			continue;
		if (code == 0)
		{
			if (current.count(8) && current[8] == layer)
				return current;
			current.clear();
			continue;
		}
		current[code] = value;
	}
	return groups;
}

QString colourText(int index)
{
	return QString::fromLatin1(dxfAciStyleColour(index));
}

}  // namespace


/**
 * @test DXF export through a cross-reference table.
 *
 * the design notes, "DXF export through a cross-reference table".
 */
class DxfCrtTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;

	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}

	QString writeTable(const char* name, const QByteArray& text) const
	{
		QFile table(file(name));
		if (table.open(QIODevice::WriteOnly | QIODevice::Truncate))
			table.write(text);
		return table.fileName();
	}

	bool exportDxf(Map& map, const QString& path, const QString& crt, bool only = false, const QString& write_crt = {}, QString* message = nullptr) const
	{
		OgrFileExport exporter(path, &map, nullptr, "DXF");
		exporter.setOption(QStringLiteral("foom dxf crt"), crt);
		exporter.setOption(QStringLiteral("foom dxf crt only"), only);
		exporter.setOption(QStringLiteral("foom dxf write crt"), write_crt);
		auto const ok = exporter.doExport();
		if (message && !exporter.warnings().empty())
			*message = exporter.warnings().back();
		return ok;
	}

	int runJob(const QJsonObject& job, const QString& job_path, QByteArray* output = nullptr) const
	{
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			job_file.write(QJsonDocument(job).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return -1;
		if (output)
			*output = process.readAllStandardOutput() + process.readAllStandardError();
		return process.exitCode();
	}

	QJsonObject jobOf(const QString& map_path, QJsonObject export_step) const
	{
		export_step.insert(QStringLiteral("step"), QStringLiteral("export"));
		QJsonObject open { { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } };
		return QJsonObject{ { QStringLiteral("steps"), QJsonArray{ open, export_step } } };
	}

	QString savedMap() const
	{
		Map map;
		setupMap(map);
		auto const path = file("dxf.foom");
		XMLFileFormat format;
		auto exporter = format.makeExporter(path, &map, nullptr);
		if (!exporter || !exporter->doExport())
			return {};
		return path;
	}

private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
		GDALAllRegister();
	}

	/// Accept (1)
	void readTest()
	{
		QString message;
		auto const crt = readDxfCrt(table_text, &message);
		QCOMPARE(int(crt.rows.size()), 2);
		QCOMPARE(crt.rows[0].symbol, QStringLiteral("501"));
		QCOMPARE(crt.rows[0].layer, QStringLiteral("roads"));
		QCOMPARE(crt.rows[0].colour, 3);
		QCOMPARE(crt.rows[1].symbol, QStringLiteral("502.1"));
		QCOMPARE(crt.rows[1].layer, QStringLiteral("foot paths"));
		QCOMPARE(crt.rows[1].colour, 0);
		QCOMPARE(crt.skipped, 4);
		QVERIFY2(message.contains(QLatin1Char('4')), qPrintable(message));
		QCOMPARE(crt.find(QStringLiteral("502.1")), &crt.rows[1]);
		QCOMPARE(crt.find(QStringLiteral("501.0")), &crt.rows[0]);
		QVERIFY(!crt.find(QStringLiteral("503")));

		message.clear();
		auto const empty = readDxfCrt("; nothing\n\n", &message);
		QVERIFY(empty.rows.empty());
		QVERIFY(!message.isEmpty());
	}

	/// Accept (2)
	void aciTest()
	{
		QRgb rgb = 0;
		QVERIFY(dxfAciColour(1, &rgb));
		QCOMPARE(rgb & 0xffffff, QRgb(0xff0000));
		QVERIFY(dxfAciColour(5, &rgb));
		QCOMPARE(rgb & 0xffffff, QRgb(0x0000ff));
		// GDAL reads 7, white or black by the background, as black.
		QVERIFY(dxfAciColour(7, &rgb));
		QCOMPARE(rgb & 0xffffff, QRgb(0x000000));
		QVERIFY(!dxfAciColour(0, &rgb));
		QVERIFY(!dxfAciColour(256, &rgb));
		QCOMPARE(dxfAciStyleColour(1), QByteArray("#ff0000"));
		QVERIFY(dxfAciStyleColour(0).isEmpty());
	}

	/// Accept (3) to (5)
	void exportTest()
	{
		Map map;
		setupMap(map);
		auto const table = writeTable("table.crt", table_text);
		auto const path = file("map.dxf");
		QString message;
		QVERIFY2(exportDxf(map, path, table, false, {}, &message), qPrintable(message));
		auto features = readFeatures(path);
		QCOMPARE(int(features.size()), 4);
		auto const* road = featureOn(features, QStringLiteral("roads"));
		QVERIFY(road);
		QVERIFY2(road->style.contains(colourText(3), Qt::CaseInsensitive), qPrintable(road->style));
		auto const* foot = featureOn(features, QStringLiteral("foot paths"));
		QVERIFY(foot);
		QVERIFY2(foot->style.contains(QLatin1String("#000000")), qPrintable(foot->style));
		QVERIFY(featureOn(features, QStringLiteral("Stream")));
		QVERIFY(featureOn(features, QStringLiteral("Open land")));
		{
			QFile dxf(path);
			QVERIFY(dxf.open(QIODevice::ReadOnly));
			QVERIFY(dxf.readAll().contains("\nfoot paths"));
		}
		auto const groups = entityGroups(path, QStringLiteral("roads"));
		QVERIFY(groups.count(62));
		QCOMPARE(groups.at(62), QStringLiteral("3"));
		QVERIFY(!groups.count(420));

		// (4)
		auto const listed = file("listed.dxf");
		QVERIFY(exportDxf(map, listed, table, true));
		features = readFeatures(listed);
		QCOMPARE(int(features.size()), 2);
		QVERIFY(featureOn(features, QStringLiteral("roads")));
		QVERIFY(featureOn(features, QStringLiteral("foot paths")));

		// (5)
		auto const written = file("written.crt");
		QVERIFY(exportDxf(map, file("written.dxf"), table, false, written));
		auto const back = readDxfCrtFile(written);
		QCOMPARE(int(back.rows.size()), 4);
		QVERIFY(back.find(QStringLiteral("401")));
		QCOMPARE(back.find(QStringLiteral("401"))->layer, QStringLiteral("Open land"));
		QCOMPARE(back.find(QStringLiteral("501"))->layer, QStringLiteral("roads"));
		QCOMPARE(back.find(QStringLiteral("501"))->colour, 3);
		QCOMPARE(back.find(QStringLiteral("502.1"))->layer, QStringLiteral("foot paths"));
		QCOMPARE(back.find(QStringLiteral("503"))->layer, QStringLiteral("Stream"));
		QCOMPARE(back.skipped, 0);

		auto const missing = file("missing.dxf");
		message.clear();
		QVERIFY(!exportDxf(map, missing, file("no such table.crt"), false, {}, &message));
		QVERIFY(!message.isEmpty());
		QVERIFY(!QFileInfo::exists(missing));
		auto const empty = writeTable("empty.crt", "; nothing\n");
		QVERIFY(!exportDxf(map, missing, empty));
		QVERIFY(!QFileInfo::exists(missing));
	}

	/// Accept (6)
	void runnerTest()
	{
		QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
		auto const map_path = savedMap();
		QVERIFY(!map_path.isEmpty());
		auto const table = writeTable("runner.crt", table_text);
		QJsonObject export_step {
			{ QStringLiteral("path"), file("runner.dxf") },
			{ QStringLiteral("crt"), table },
		};
		QByteArray output;
		QVERIFY2(runJob(jobOf(map_path, export_step), file("dxf1.json"), &output) == 0, output.constData());
		auto features = readFeatures(file("runner.dxf"));
		QCOMPARE(int(features.size()), 4);
		QVERIFY(featureOn(features, QStringLiteral("roads")));
		QVERIFY(featureOn(features, QStringLiteral("foot paths")));

		export_step.insert(QStringLiteral("path"), file("runner-only.dxf"));
		export_step.insert(QStringLiteral("crt_only"), true);
		export_step.insert(QStringLiteral("write_crt"), file("runner-written.crt"));
		QVERIFY2(runJob(jobOf(map_path, export_step), file("dxf2.json"), &output) == 0, output.constData());
		QCOMPARE(int(readFeatures(file("runner-only.dxf")).size()), 2);
		QCOMPARE(int(readDxfCrtFile(file("runner-written.crt")).rows.size()), 2);

		// Rejected when the job is read.
		QJsonObject gpx { { QStringLiteral("path"), file("bad.gpx") }, { QStringLiteral("crt"), table } };
		QVERIFY(runJob(jobOf(map_path, gpx), file("bad1.json"), &output) != 0);
		QVERIFY(!QFileInfo::exists(file("bad.gpx")));
		QJsonObject only { { QStringLiteral("path"), file("bad.dxf") }, { QStringLiteral("crt_only"), true } };
		QVERIFY(runJob(jobOf(map_path, only), file("bad2.json"), &output) != 0);
		only.insert(QStringLiteral("crt"), table);
		only.insert(QStringLiteral("crt_only"), QStringLiteral("yes"));
		QVERIFY(runJob(jobOf(map_path, only), file("bad3.json"), &output) != 0);
		QVERIFY(!QFileInfo::exists(file("bad.dxf")));
	}

	/// Accept (7)
	void editorTest()
	{
		auto* map = new Map();
		setupMap(*map);
		// Georeferenced: the warning about a local georeferencing would open a dialog.
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
		georef.setProjectedRefPoint(QPointF(500000, 5200000), false, false);
		georef.setMapRefPoint(MapCoord(0, 0));
		georef.setCombinedScaleFactor(1.0);
		map->setGeoreferencing(georef);
		auto* window = new MainWindow();
		auto* editor = new MapEditorController(MapEditorController::MapEditor, map);
		window->setController(editor);
		QVERIFY(editor->getAction("export-dxf-crt"));
		// A table without skipped lines: warnings would open a dialog.
		auto const table = writeTable("editor.crt", "501 roads 3\n502.1 \"foot paths\"\n");
		auto const path = file("editor.dxf");
		QVERIFY(editor->exportDxfWithCrt(path, table, true));
		QCOMPARE(int(readFeatures(path).size()), 2);
		map->setHasUnsavedChanges(false);  // no question on closing
		window->close();
		window->deleteLater();
	}
};


namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(DxfCrtTest)
#include "dxf_crt_t.moc"  // IWYU pragma: keep
