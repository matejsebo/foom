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

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "course/course_data.h"
#include "templates/template.h"

using namespace OpenOrienteering;


namespace {

struct JobResult
{
	int exit_code = -1;
	QByteArray out;
	QByteArray err;

	QList<QJsonObject> lines() const
	{
		QList<QJsonObject> result;
		for (auto const& line : out.split('\n'))
		{
			if (!line.trimmed().isEmpty())
				result.push_back(QJsonDocument::fromJson(line).object());
		}
		return result;
	}

	QJsonObject logOf(const char* step) const
	{
		for (auto const& line : lines())
		{
			if (line.value(QLatin1String("step")).toString() == QLatin1String(step))
				return line;
		}
		return {};
	}
};

QJsonObject stepOf(const char* name, QJsonObject params = {})
{
	params.insert(QStringLiteral("step"), QString::fromLatin1(name));
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

bool saveMap(const Map& map, const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && map.exportToIODevice(file);
}

/// The bounding box of a template of a saved map, its file loaded.
QRectF boxOf(Map& map, int index)
{
	auto* temp = map.getTemplate(index);
	if (temp->getTemplateState() != Template::Loaded)
		temp->loadTemplateFile();
	return temp->calculateTemplateBoundingBox();
}

bool near(double a, double b, double tolerance)
{
	return std::abs(a - b) <= tolerance;
}

}  // namespace


/**
 * @test Tests the runner's steps for background maps and GNSS files, and XML scripts.
 *
 * See the design notes, "Background maps and point clouds in the runner"
 * and "XML scripts", and the design notes,
 * "NMEA and FRWD files".
 */
class RunnerBackgroundTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;

	QString file(const char* name) const { return dir.filePath(QString::fromLatin1(name)); }

	JobResult runFile(const QString& job_path, const QStringList& extra = {}) const
	{
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), QStringList{ QStringLiteral("--run"), job_path } + extra);
		if (!process.waitForFinished(120000))
			return {};
		return { process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError() };
	}

	/// Opens the map, runs the steps and saves to out.foom.
	JobResult runJob(const char* name, QList<QJsonObject> steps, const char* map = "map.foom", bool save = true) const
	{
		steps.prepend(stepOf("open", { { QStringLiteral("path"), QString::fromLatin1(map) } }));
		if (save)
			steps.append(stepOf("save-as", { { QStringLiteral("path"), QStringLiteral("out.foom") } }));
		QJsonArray array;
		for (auto const& step : steps)
			array.append(step);
		QFile job_file(file(name));
		if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return {};
		job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), array } }).toJson());
		job_file.close();
		QFile::remove(file("out.foom"));
		return runFile(file(name));
	}

	JobResult runScript(const char* name, const QByteArray& text, const QStringList& extra = {}) const
	{
		QFile script(file(name));
		if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return {};
		script.write(text);
		script.close();
		QFile::remove(file("out.foom"));
		return runFile(file(name), extra);
	}

	std::unique_ptr<Map> loadOut() const
	{
		auto map = std::make_unique<Map>();
		if (!map->loadFrom(file("out.foom")))
			return {};
		return map;
	}

private slots:
	void initTestCase();
	void templateSteps();
	void gnssStep();
	void xmlScript();
};


void RunnerBackgroundTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
	QVERIFY(dir.isValid());
	QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);

	// The georeferenced map, with a line symbol 1 and a point symbol 2.
	Map map;
	map.setScaleDenominator(10000);
	map.setGeoreferencing(utm32Georeferencing());
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	black->setRgbFromCmyk();
	map.addColor(black, 0);
	auto* line = new LineSymbol();
	line->setNumberComponent(0, 1);
	line->setLineWidth(0.5);
	line->setColor(black);
	map.addSymbol(line, 0);
	auto* point = new PointSymbol();
	point->setNumberComponent(0, 2);
	map.addSymbol(point, 1);
	QVERIFY(saveMap(map, file("map.foom")));
	{
		// The same with a line, so that an export has something to draw.
		auto* path = new PathObject(line);
		path->addCoordinate(MapCoord(0.0, 0.0));
		path->addCoordinate(MapCoord(20.0, 10.0));
		map.addObject(path);
		QVERIFY(saveMap(map, file("drawn.foom")));
	}

	// A map which is not georeferenced, and one as a background map.
	Map local;
	local.setScaleDenominator(10000);
	QVERIFY(saveMap(local, file("local.foom")));
	QVERIFY(saveMap(local, file("background.foom")));

	// The image of 100 by 50 pixels with a world file of 2 m per pixel,
	// its top left corner at (500000, 5000000).
	QImage image(100, 50, QImage::Format_RGB32);
	image.fill(QColor(Qt::yellow));
	QVERIFY(image.save(file("a.png")));
	QVERIFY(image.save(file("b.png")));
	for (auto const* world : { "a.pgw", "b.pgw" })
	{
		QFile world_file(file(world));
		QVERIFY(world_file.open(QIODevice::WriteOnly));
		world_file.write("2\n0\n0\n-2\n500001\n4999999\n");
	}

	QVERIFY(QFile::copy(QFINDTESTDATA("data/gnss/walk.nmea"), file("walk.nmea")));
	QVERIFY(QFile::copy(QFINDTESTDATA("data/gnss/walk.nmea"), file("walk.frwd")));
}


/// Accept (14)
void RunnerBackgroundTest::templateSteps()
{
	auto const add_a = stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("crs"), QStringLiteral("UTM:13 N") } });

	// 1. Georeferenced by its world file.
	auto result = runJob("add.json", { add_a });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	auto line = result.logOf("add-template");
	QCOMPARE(line.value(QLatin1String("templates")).toInt(), 1);
	QVERIFY(line.value(QLatin1String("georeferenced")).toBool());
	auto out = loadOut();
	QVERIFY(out);
	QCOMPARE(out->getNumTemplates(), 1);
	QVERIFY(out->getTemplate(0)->isTemplateGeoreferenced());
	auto box = boxOf(*out, 0);
	QVERIFY2(near(box.width(), 20.0, 0.05) && near(box.height(), 10.0, 0.05), qPrintable(QString::fromLatin1("%1 x %2").arg(box.width()).arg(box.height())));
	QVERIFY2(near(box.left(), 0.0, 0.05) && near(box.top(), 0.0, 0.05), qPrintable(QString::fromLatin1("%1, %2").arg(box.left()).arg(box.top())));

	// 2. Not georeferenced, placed.
	result = runJob("place.json", { stepOf("add-template", {
	    { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("georeferenced"), false },
	    { QStringLiteral("center"), QJsonArray{ 50, 50 } }, { QStringLiteral("meters_per_pixel"), 1 } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QVERIFY(!result.logOf("add-template").value(QLatin1String("georeferenced")).toBool());
	out = loadOut();
	QVERIFY(out);
	QVERIFY(!out->getTemplate(0)->isTemplateGeoreferenced());
	box = boxOf(*out, 0);
	QVERIFY(near(box.width(), 10.0, 0.01) && near(box.height(), 5.0, 0.01));
	QVERIFY(near(box.center().x(), 50.0, 0.01) && near(box.center().y(), 50.0, 0.01));
	result = runJob("dpi.json", { stepOf("add-template", {
	    { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("georeferenced"), false },
	    { QStringLiteral("dpi"), 254 }, { QStringLiteral("rotation"), 90 } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	out = loadOut();
	QVERIFY(out);
	box = boxOf(*out, 0);
	QVERIFY2(near(box.width(), 5.0, 0.01) && near(box.height(), 10.0, 0.01), qPrintable(QString::fromLatin1("%1 x %2").arg(box.width()).arg(box.height())));

	// 3. A map file; faults.
	result = runJob("map.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("background.foom") } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QCOMPARE(result.logOf("add-template").value(QLatin1String("type")).toString(), QStringLiteral("TemplateMap"));
	QCOMPARE(runJob("missing.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("none.png") } }) }).exit_code, 1);
	QCOMPARE(runJob("local.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("georeferenced"), true } }) }, "local.foom").exit_code, 1);
	QCOMPARE(runJob("unplaced.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("georeferenced"), false } }) }).exit_code, 1);
	QCOMPARE(runJob("both.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("dpi"), 300 }, { QStringLiteral("meters_per_pixel"), 1 } }) }).exit_code, 2);
	QCOMPARE(runJob("unknown.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("opacity"), 0.5 } }) }).exit_code, 2);
	QCOMPARE(runJob("rotation.json", { stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("rotation"), QStringLiteral("left") } }) }).exit_code, 2);

	// 4. Removing: a.png is added first, b.png last, at the top of the list.
	auto const add_b = stepOf("add-template", { { QStringLiteral("path"), QStringLiteral("b.png") }, { QStringLiteral("crs"), QStringLiteral("UTM:13 N") } });
	result = runJob("remove-name.json", { add_a, add_b, stepOf("remove-template", { { QStringLiteral("template"), QStringLiteral("B.PNG") } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QCOMPARE(result.logOf("remove-template").value(QLatin1String("removed")).toInt(), 1);
	out = loadOut();
	QCOMPARE(out->getNumTemplates(), 1);
	QCOMPARE(out->getTemplate(0)->getTemplateFilename(), QStringLiteral("a.png"));
	result = runJob("remove-number.json", { add_a, add_b, stepOf("remove-template", { { QStringLiteral("template"), 1 } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	out = loadOut();
	QCOMPARE(out->getNumTemplates(), 1);
	QCOMPARE(out->getTemplate(0)->getTemplateFilename(), QStringLiteral("a.png"));
	result = runJob("remove-missing.json", { add_a, stepOf("remove-template", { { QStringLiteral("template"), QJsonArray{ QStringLiteral("a.png"), QStringLiteral("c.png") } } }) });
	QCOMPARE(result.exit_code, 1);
	QCOMPARE(runJob("remove-bare.json", { stepOf("remove-template") }).exit_code, 2);

	// 5. Georeferencing a template.
	auto const add_placed = stepOf("add-template", {
	    { QStringLiteral("path"), QStringLiteral("a.png") }, { QStringLiteral("georeferenced"), false },
	    { QStringLiteral("center"), QJsonArray{ 0, 0 } }, { QStringLiteral("meters_per_pixel"), 1 } });
	auto const pass = [](double fx, double fy, double tx, double ty) {
		return QJsonObject{ { QStringLiteral("from"), QJsonArray{ fx, fy } }, { QStringLiteral("to"), QJsonArray{ tx, ty } } };
	};
	result = runJob("shift.json", { add_placed, stepOf("georeference-template", {
	    { QStringLiteral("template"), QStringLiteral("a.png") },
	    { QStringLiteral("pass_points"), QJsonArray{ pass(0, 0, 10, 0), pass(10, 0, 20, 0) } } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QVERIFY(result.logOf("georeference-template").value(QLatin1String("error")).toDouble() < 0.001);
	out = loadOut();
	box = boxOf(*out, 0);
	QVERIFY2(near(box.center().x(), 10.0, 0.01) && near(box.center().y(), 0.0, 0.01), qPrintable(QString::fromLatin1("%1, %2").arg(box.center().x()).arg(box.center().y())));
	QVERIFY(near(box.width(), 10.0, 0.01));
	result = runJob("turn.json", { add_placed, stepOf("georeference-template", {
	    { QStringLiteral("template"), 1 },
	    { QStringLiteral("pass_points"), QJsonArray{ pass(0, 0, 0, 0), pass(10, 0, 0, 10) } } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	out = loadOut();
	box = boxOf(*out, 0);
	QVERIFY2(near(box.width(), 5.0, 0.01) && near(box.height(), 10.0, 0.01), qPrintable(QString::fromLatin1("%1 x %2").arg(box.width()).arg(box.height())));

	result = runJob("ungeoref.json", { add_a, stepOf("georeference-template", { { QStringLiteral("template"), 1 }, { QStringLiteral("georeferenced"), false } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	out = loadOut();
	QVERIFY(!out->getTemplate(0)->isTemplateGeoreferenced());
	box = boxOf(*out, 0);
	QVERIFY(near(box.width(), 20.0, 0.05) && near(box.left(), 0.0, 0.05));
	result = runJob("regeoref.json", { add_placed, stepOf("georeference-template", {
	    { QStringLiteral("template"), 1 }, { QStringLiteral("georeferenced"), true }, { QStringLiteral("crs"), QStringLiteral("UTM:13 N") } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	out = loadOut();
	QVERIFY(out->getTemplate(0)->isTemplateGeoreferenced());
	box = boxOf(*out, 0);
	QVERIFY(near(box.width(), 20.0, 0.05) && near(box.left(), 0.0, 0.05) && near(box.top(), 0.0, 0.05));
	QCOMPARE(runJob("two-ways.json", { add_a, stepOf("georeference-template", {
	    { QStringLiteral("template"), 1 }, { QStringLiteral("georeferenced"), true },
	    { QStringLiteral("pass_points"), QJsonArray{ pass(0, 0, 1, 1) } } }) }).exit_code, 2);
}


/// Accept (4) of "NMEA and FRWD files"
void RunnerBackgroundTest::gnssStep()
{
	auto result = runJob("track.json", { stepOf("import-gnss", {
	    { QStringLiteral("path"), QStringLiteral("walk.nmea") }, { QStringLiteral("as"), QStringLiteral("track") }, { QStringLiteral("symbol"), 1 } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	auto const line = result.logOf("import-gnss");
	QCOMPARE(line.value(QLatin1String("objects")).toInt(), 1);
	QCOMPARE(line.value(QLatin1String("fixes")).toInt(), 4);
	QCOMPARE(line.value(QLatin1String("skipped")).toInt(), 1);
	QCOMPARE(line.value(QLatin1String("bad_checksums")).toInt(), 1);
	auto out = loadOut();
	QVERIFY(out);
	QCOMPARE(out->getNumObjects(), 1);

	result = runJob("positions.json", { stepOf("import-gnss", {
	    { QStringLiteral("path"), QStringLiteral("walk.nmea") }, { QStringLiteral("as"), QStringLiteral("positions") }, { QStringLiteral("symbol"), QStringLiteral("2") } }) });
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QCOMPARE(result.logOf("import-gnss").value(QLatin1String("objects")).toInt(), 4);

	QCOMPARE(runJob("wrong.json", { stepOf("import-gnss", {
	    { QStringLiteral("path"), QStringLiteral("walk.nmea") }, { QStringLiteral("as"), QStringLiteral("positions") }, { QStringLiteral("symbol"), 1 } }) }).exit_code, 1);
	QCOMPARE(runJob("routes.json", { stepOf("import-gnss", {
	    { QStringLiteral("path"), QStringLiteral("walk.nmea") }, { QStringLiteral("as"), QStringLiteral("routes") }, { QStringLiteral("symbol"), 1 } }) }).exit_code, 2);
	result = runJob("frwd.json", { stepOf("import-gnss", { { QStringLiteral("path"), QStringLiteral("walk.frwd") }, { QStringLiteral("symbol"), 1 } }) });
	QCOMPARE(result.exit_code, 1);
	QVERIFY(result.logOf("import-gnss").value(QLatin1String("message")).toString().contains(QStringLiteral("FRWD")));
}


/// Accept (16)
void RunnerBackgroundTest::xmlScript()
{
	// (1) Supported and unsupported commands, one per line.
	QByteArray const script =
	    "<?xml version=\"1.0\"?>\r\n"
	    "<OcadScript>\r\n"
	    "<FileOpen><FileName>map.foom</FileName></FileOpen>\r\n"
	    "<MapChangeScale><Scale>15000</Scale></MapChangeScale>\r\n"
	    "<MapOptimizeAndRepair/>\r\n"
	    "<FileSaveAs><FileName>out.foom</FileName></FileSaveAs>\r\n"
	    "</OcadScript>\r\n";
	auto result = runScript("script.xml", script);
	QVERIFY2(result.exit_code == 0, result.err.constData());
	auto lines = result.lines();
	QVERIFY(lines.size() >= 4);
	auto const report = lines.front();
	QCOMPARE(report.value(QLatin1String("commands")).toInt(), 4);
	QCOMPARE(report.value(QLatin1String("steps")).toInt(), 3);
	auto const unsupported = report.value(QLatin1String("unsupported")).toArray();
	QCOMPARE(unsupported.size(), 1);
	QCOMPARE(unsupported.at(0).toObject().value(QLatin1String("command")).toString(), QStringLiteral("MapOptimizeAndRepair"));
	QCOMPARE(unsupported.at(0).toObject().value(QLatin1String("line")).toInt(), 5);
	auto out = loadOut();
	QVERIFY(out);
	QCOMPARE(int(out->getScaleDenominator()), 15000);

	result = runScript("strict.xml", script, { QStringLiteral("--strict") });
	QCOMPARE(result.exit_code, 2);
	QVERIFY(!QFileInfo::exists(file("out.foom")));

	// (2) The canvases, as the format has them.
	QByteArray const canvas =
	    "<?xml version=\"1.0\"?>\r\n<OcadScript>\r\n"
	    "<FileOpen><FileName>map.foom</FileName></FileOpen>\r\n"
	    "<CsCanvas>\r\n<Activate>true</Activate>\r\n<IsCourses>true</IsCourses>\r\n"
	    "<Canvas>\r\n<Name>Canvas 1</Name>\r\n<ExportRectangleName pageStatus=\"psSingle\">Whole</ExportRectangleName>\r\n"
	    "<ExportRectangleFrontside>\r\n<L>-142.03</L>\r\n<B>-291.03</B>\r\n<R>255.37</R>\r\n<T>77.26</T>\r\n</ExportRectangleFrontside>\r\n"
	    "<ExportScale>10000</ExportScale>\r\n<CdSymbolsSize>6.0</CdSymbolsSize>\r\n<CdMaxLength>25</CdMaxLength>\r\n"
	    "<Courses>\r\n<Course>Easy Course</Course>\r\n</Courses>\r\n</Canvas>\r\n</CsCanvas>\r\n"
	    "<FileSaveAs><FileName>out.foom</FileName></FileSaveAs>\r\n"
	    "</OcadScript>\r\n";
	result = runScript("canvas.xml", canvas);
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QCOMPARE(result.logOf("load-canvases").value(QLatin1String("canvases")).toInt(), 1);
	out = loadOut();
	QVERIFY(out);
	QCOMPARE(int(out->courseData().canvases.canvases.size()), 1);
	QCOMPARE(out->courseData().canvases.canvases.front().name, QStringLiteral("Canvas 1"));
	auto has_whole = false;
	for (auto const& extent : out->extents())
		has_whole = has_whole || extent.name == QLatin1String("Whole");
	QVERIFY(has_whole);

	// (3) A parameter which the table does not name.
	result = runScript("encrypt.xml",
	    "<OcadScript>\n<FileOpen><FileName>map.foom</FileName></FileOpen>\n"
	    "<FileSaveAs><FileName>out.foom</FileName><Encrypt>true</Encrypt></FileSaveAs>\n</OcadScript>\n");
	QCOMPARE(result.exit_code, 0);
	auto const encrypt = result.lines().front().value(QLatin1String("unsupported")).toArray();
	QCOMPARE(encrypt.size(), 1);
	QCOMPARE(encrypt.at(0).toObject().value(QLatin1String("reason")).toString(), QStringLiteral("Encrypt"));
	QVERIFY(!QFileInfo::exists(file("out.foom")));

	// (4) Not well formed; nothing but unsupported commands.
	result = runScript("broken.xml", "<OcadScript>\n<FileOpen><FileName>map.foom</FileName>\n</OcadScript>\n");
	QCOMPARE(result.exit_code, 2);
	QVERIFY(result.err.contains("line"));
	result = runScript("none.xml", "<OcadScript><MapOptimizeAndRepair/><DatabaseLink/></OcadScript>");
	QCOMPARE(result.exit_code, 0);
	QCOMPARE(result.lines().size(), 1);
	QCOMPARE(result.lines().front().value(QLatin1String("unsupported")).toArray().size(), 2);

	// (5) A PNG of 100 dpi.
	result = runScript("png.xml",
	    "<OcadScript><FileOpen><FileName>drawn.foom</FileName></FileOpen>"
	    "<BackgroundMapOpen><FileName>a.png</FileName></BackgroundMapOpen>"
	    "<ExportPng><FileName>out.png</FileName><Resolution>100</Resolution></ExportPng></OcadScript>");
	QVERIFY2(result.exit_code == 0, result.err.constData());
	QImage png(file("out.png"));
	QVERIFY(!png.isNull());
	QVERIFY(near(png.dotsPerMeterX(), 100 / 0.0254, 1.0));
}


QTEST_MAIN(RunnerBackgroundTest)
#include "runner_background_t.moc"  // IWYU pragma: keep
