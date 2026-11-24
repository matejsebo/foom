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
#include <vector>

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
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "cli/job.h"
#include "cli/xml_script.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "course/course_data.h"
#ifdef FOOM_TEST_ELEVATION
#  include "elevation/elevation_model.h"
#  include "elevation/point_cloud.h"
#endif
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

/// A step or a list as one line of JSON, its members in the order of their names.
QString compact(const QJsonObject& object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// Whether one of the warnings begins with the name of the field.
bool names(const QStringList& warnings, const char* field)
{
	for (auto const& warning : warnings)
	{
		if (warning.startsWith(QLatin1String(field)))
			return true;
	}
	return false;
}

bool touch(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly);
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
	void lidarScript();
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


/// Accept (18)
void RunnerBackgroundTest::lidarScript()
{
	QFile fixture(QFINDTESTDATA("data/jobs/lidar-script.xml"));
	QVERIFY(fixture.open(QIODevice::ReadOnly));
	auto const bytes = fixture.readAll();
	auto const folders = { "0_LAZ", "1_ocdDem", "5_CreateContourLines", "6_ImportAllOcds", "7_png" };

	// (1) The steps of the script.
	QDir const read_dir(file("read"));
	for (auto const* folder : folders)
		QVERIFY(read_dir.mkpath(QLatin1String(folder)));
	for (auto const* name : { "0_LAZ/b.las", "0_LAZ/a.laz", "0_LAZ/notes.txt" })
		QVERIFY(touch(read_dir.filePath(QLatin1String(name))));
	auto const path = [&read_dir](const char* name) {
		return QDir::cleanPath(read_dir.absoluteFilePath(QLatin1String(name)));
	};
	auto const script = Cli::readXmlScript(bytes, read_dir);
	QCOMPARE(script.commands, 20);
	QCOMPARE(script.steps.size(), 15);
	QCOMPARE(int(script.warnings.size()), 15);
	QVERIFY(script.unsupported.empty());
	QStringList noted;
	for (auto const& note : script.notes)
		noted.push_back(note.command);
	QCOMPARE(noted, (QStringList{ QStringLiteral("Dem.MatchTiles"), QStringLiteral("Dem.MergeTiles"), QStringLiteral("Dem.CropTiles"), QStringLiteral("Dem.MergeRasterMapTiles") }));

	auto const open = stepOf("open", { { QStringLiteral("path"), QStringLiteral("Template.ocd") } });
	auto const save = [](const QString& to, bool current) {
		QJsonObject step { { QStringLiteral("path"), to } };
		if (current)
			step.insert(QStringLiteral("current"), true);
		return stepOf("save-as", step);
	};
	auto const clouds = QJsonArray{ path("0_LAZ/a.laz"), path("0_LAZ/b.las") };
	QList<QJsonObject> const expected = {
	    open,
	    save(QStringLiteral("1_ocdDem/Map.ocd"), true),
	    stepOf("point-cloud", {
	        { QStringLiteral("files"), clouds },
	        { QStringLiteral("cell"), 2 },
	        { QStringLiteral("dtm"), true },
	        { QStringLiteral("dsm"), true },
	        { QStringLiteral("dtm_classes"), QJsonArray{ QStringLiteral("ground") } },
	        { QStringLiteral("dtm_returns"), QStringLiteral("last") },
	        { QStringLiteral("dsm_classes"), QJsonArray{ QStringLiteral("ground"), QStringLiteral("high-vegetation") } },
	        { QStringLiteral("dsm_returns"), QStringLiteral("all") },
	        { QStringLiteral("products"), QJsonArray{} } }),
	    save(QStringLiteral("1_ocdDem/Map.ocd"), false),
	    open,
	    stepOf("elevation-wizard", {
	        { QStringLiteral("source"), QStringLiteral("grid") },
	        { QStringLiteral("files"), QJsonArray{ path("1_ocdDem/Map_DTM.tif") } },
	        { QStringLiteral("products"), QJsonArray{ QStringLiteral("contours") } },
	        { QStringLiteral("contour_sets"), QJsonArray{ QJsonObject{
	            { QStringLiteral("smoothed"), true },
	            { QStringLiteral("interval"), 5 },
	            { QStringLiteral("main_interval"), 5 },
	            { QStringLiteral("index_interval"), 25 },
	            { QStringLiteral("curves"), true },
	            { QStringLiteral("depressions"), false },
	            { QStringLiteral("symbols"), QJsonObject{
	                { QStringLiteral("smallest"), QStringLiteral("101.0") },
	                { QStringLiteral("main"), QStringLiteral("101.0") },
	                { QStringLiteral("index"), QStringLiteral("102.0") } } } } } } }),
	    save(path("5_CreateContourLines/Contours.ocd"), false),
	    open,
	    save(QStringLiteral("6_ImportAllOcds/Contour Lines.ocd"), true),
	    stepOf("import", { { QStringLiteral("path"), path("5_CreateContourLines/Contours.ocd") } }),
	    save(QStringLiteral("6_ImportAllOcds/Contour Lines.ocd"), false),
	    open,
	    save(QStringLiteral("7_png/Map_Vegetation.ocd"), true),
	    stepOf("point-cloud", {
	        { QStringLiteral("files"), clouds },
	        { QStringLiteral("cell"), 1 },
	        { QStringLiteral("dtm"), true },
	        { QStringLiteral("dsm"), false },
	        { QStringLiteral("dtm_classes"), QJsonArray{ QStringLiteral("ground") } },
	        { QStringLiteral("dtm_returns"), QStringLiteral("last") },
	        { QStringLiteral("products"), QJsonArray{ QStringLiteral("vegetation-map") } },
	        { QStringLiteral("vegetation_map"), QJsonObject{
	            { QStringLiteral("undergrowth_kernel"), 3 },
	            { QStringLiteral("undergrowth_from"), 0 },
	            { QStringLiteral("undergrowth_to"), 0 },
	            { QStringLiteral("undergrowth_thresholds"), QJsonArray{ 1, 1 } },
	            { QStringLiteral("vegetation_kernel"), 5 },
	            { QStringLiteral("vegetation_from"), 0.3 },
	            { QStringLiteral("vegetation_to"), 3 },
	            { QStringLiteral("vegetation_thresholds"), QJsonArray{ -0.4, -0.2, 0.2 } },
	            { QStringLiteral("no_data_red"), false } } } }),
	    save(QStringLiteral("7_png/Map_Vegetation.ocd"), false),
	};
	for (int i = 0; i < expected.size(); ++i)
		QCOMPARE(compact(script.steps.at(i).toObject()), compact(expected.at(i)));

	for (int i = 0; i < 15; ++i)
	{
		if (i != 9 && i != 13)
			QVERIFY2(script.warnings[std::size_t(i)].isEmpty(), qPrintable(script.warnings[std::size_t(i)].join(QLatin1Char('|'))));
	}
	auto const& vegetation_warnings = script.warnings[13];
	QVERIFY(names(vegetation_warnings, "UseFilter"));
	QVERIFY(names(vegetation_warnings, "ThresholdOverlapPointsLow"));
	QVERIFY(names(vegetation_warnings, "LasClassification"));
	QCOMPARE(vegetation_warnings.size(), 3);
	QVERIFY(names(script.warnings[9], "SymbolOption"));
	QVERIFY(names(script.warnings[9], "ColorOption"));
	auto const report = script.report(QStringLiteral("script.xml"));
	QCOMPARE(report.value(QLatin1String("notes")).toArray().size(), 4);
	QCOMPARE(report.value(QLatin1String("notes")).toArray().at(0).toObject().value(QLatin1String("command")).toString(), QStringLiteral("Dem.MatchTiles"));

	// (2) Other scripts.
	auto const read = [&read_dir](const char* commands) {
		return Cli::readXmlScript(QByteArray("<OcadScript>") + commands + "</OcadScript>", read_dir);
	};
	auto other = read("<FileOpen><FileName>map.foom</FileName></FileOpen>"
	                  "<LidarImport><FileName>a.las</FileName><FileName>b.las</FileName><CellSize>2</CellSize></LidarImport>");
	QCOMPARE(other.steps.size(), 2);
	QCOMPARE(compact(other.steps.at(1).toObject()), compact(stepOf("point-cloud", {
	    { QStringLiteral("files"), QJsonArray{ QStringLiteral("a.las"), QStringLiteral("b.las") } }, { QStringLiteral("cell"), 2 } })));

	other = read("<File.Open><File>Template.ocd</File></File.Open>"
	             "<Dem.DemImport><LasImport><Directory>0_LAZ</Directory></LasImport><Sharpen>1</Sharpen>"
	             "<CreateOcdDem><LasDtm><ReturnType>0</ReturnType></LasDtm><LasDsm><ReturnType>7</ReturnType></LasDsm></CreateOcdDem>"
	             "</Dem.DemImport>");
	QVERIFY(other.unsupported.empty());
	QCOMPARE(other.steps.size(), 2);
	QCOMPARE(compact(other.steps.at(1).toObject()), compact(stepOf("point-cloud", {
	    { QStringLiteral("files"), clouds }, { QStringLiteral("dtm_returns"), QStringLiteral("first") }, { QStringLiteral("products"), QJsonArray{} } })));
	QCOMPARE(other.warnings[1].size(), 3);
	QVERIFY(names(other.warnings[1], "Sharpen"));
	QVERIFY(names(other.warnings[1], "LasDtm/ReturnType 0"));
	QVERIFY(names(other.warnings[1], "LasDsm/ReturnType 7"));

	other = read("<File.Open><File>Template.ocd</File></File.Open>"
	             "<File.SaveAs><File>out.ocd</File><Encrypt>true</Encrypt></File.SaveAs>");
	QCOMPARE(other.steps.size(), 1);
	QCOMPARE(int(other.unsupported.size()), 1);
	QCOMPARE(other.unsupported.front().reason, QStringLiteral("Encrypt"));

	other = read("<File.Open><File>Template.ocd</File></File.Open>"
	             "<Dem.DemImport><Enabled>false</Enabled><LasImport><Directory>0_LAZ</Directory></LasImport></Dem.DemImport>"
	             "<Dem.DemImport><LasImport><Directory>1_ocdDem</Directory></LasImport></Dem.DemImport>");
	QCOMPARE(other.steps.size(), 1);
	QCOMPARE(int(other.notes.size()), 1);
	QCOMPARE(other.notes.front().command, QStringLiteral("Dem.DemImport"));
	QCOMPARE(other.notes.front().line, 1);
	QCOMPARE(int(other.unsupported.size()), 1);
	QVERIFY(other.unsupported.front().reason.startsWith(QLatin1String("LasImport/Directory")));

	QVERIFY_EXCEPTION_THROWN(read("<File.Save><Enabled>true</Enabled></File.Save>"), Cli::JobError);
	QVERIFY_EXCEPTION_THROWN(read("<File.Open><File>Template.ocd</File></File.Open><File.Close/><File.Save/>"), Cli::JobError);

	auto const contours = [&read](const char* before, const char* input) {
		return read(QByteArray(QByteArray(before) + "<Dem.CreateContourLines><TemplateFile>Template.ocd</TemplateFile><InputFolder>" + input
		             + "</InputFolder><OutputFolder>5_CreateContourLines</OutputFolder>"
		               "<CreateNotSmoothedContours>true</CreateNotSmoothedContours><EquidistanceOriginal>100</EquidistanceOriginal>"
		               "<EquidistanceLevel1>100</EquidistanceLevel1><EquidistanceLevel2>500</EquidistanceLevel2><EquidistanceLevel3>2500</EquidistanceLevel3>"
		               "</Dem.CreateContourLines>").constData());
	};
	other = contours("", "7_png");
	QCOMPARE(other.steps.size(), 0);
	QCOMPARE(int(other.unsupported.size()), 1);
	QVERIFY(other.unsupported.front().reason.startsWith(QLatin1String("InputFolder")));
	QVERIFY(read_dir.mkpath(QStringLiteral("grids")));
	for (auto const* name : { "grids/x_DTM.ocdDem", "grids/x_DSM.ocdDem", "grids/y_DTM.tif", "grids/y_hillshade.tif" })
		QVERIFY(touch(read_dir.filePath(QLatin1String(name))));
	other = contours("<File.Open><File>Template.ocd</File></File.Open>", "grids");
	QCOMPARE(other.steps.size(), 1);
	QCOMPARE(int(other.unsupported.size()), 1);
	other = contours("", "grids\\");
	QVERIFY(other.unsupported.empty());
	QCOMPARE(other.steps.size(), 3);
	QCOMPARE(compact(other.steps.at(1).toObject()), compact(stepOf("elevation-wizard", {
	    { QStringLiteral("source"), QStringLiteral("grid") },
	    { QStringLiteral("files"), QJsonArray{ path("grids/x_DTM.ocdDem"), path("grids/y_DTM.tif") } },
	    { QStringLiteral("products"), QJsonArray{ QStringLiteral("contours") } },
	    { QStringLiteral("contour_sets"), QJsonArray{ QJsonObject{
	        { QStringLiteral("smoothed"), false },
	        { QStringLiteral("interval"), 1 },
	        { QStringLiteral("main_interval"), 5 },
	        { QStringLiteral("index_interval"), 25 },
	        { QStringLiteral("depressions"), false } } } } })));
	QVERIFY(other.warnings[1].isEmpty());

	// The symbols go with the intervals of their levels.
	struct SetOf
	{
		QJsonObject set;
		QStringList warnings;
	};
	auto const setOf = [&read](const char* fields) {
		auto const script = read(QByteArray(QByteArray("<Dem.CreateContourLines><TemplateFile>Template.ocd</TemplateFile><InputFolder>grids</InputFolder>"
		                                               "<OutputFolder>5_CreateContourLines</OutputFolder><CreateNotSmoothedContours>true</CreateNotSmoothedContours>")
		                                    + fields + "</Dem.CreateContourLines>").constData());
		SetOf result;
		if (script.steps.size() == 3 && script.unsupported.empty())
		{
			result.set = script.steps.at(1).toObject().value(QLatin1String("contour_sets")).toArray().at(0).toObject();
			result.warnings = script.warnings[1];
		}
		return result;
	};
	auto const three = QJsonObject{ { QStringLiteral("smallest"), QStringLiteral("103.0") },
	                                { QStringLiteral("main"), QStringLiteral("101.0") },
	                                { QStringLiteral("index"), QStringLiteral("102.1") } };
	auto set = setOf("<EquidistanceLevel1>100</EquidistanceLevel1><EquidistanceLevel2>500</EquidistanceLevel2><EquidistanceLevel3>2500</EquidistanceLevel3>"
	                 "<SymbolNumberLevel1>103000</SymbolNumberLevel1><SymbolNumberLevel2>101000</SymbolNumberLevel2><SymbolNumberLevel3>102001</SymbolNumberLevel3>");
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(three));
	QVERIFY(!set.set.contains(QLatin1String("depression_symbols")));
	QVERIFY2(set.warnings.isEmpty(), qPrintable(set.warnings.join(QLatin1Char('|'))));
	set = setOf("<EquidistanceLevel1>2500</EquidistanceLevel1><EquidistanceLevel2>100</EquidistanceLevel2><EquidistanceLevel3>500</EquidistanceLevel3>"
	            "<SymbolNumberLevel1>102001</SymbolNumberLevel1><SymbolNumberLevel2>103000</SymbolNumberLevel2><SymbolNumberLevel3>101000</SymbolNumberLevel3>");
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(three));
	QCOMPARE(set.set.value(QLatin1String("interval")).toDouble(), 1.0);
	QCOMPARE(set.set.value(QLatin1String("index_interval")).toDouble(), 25.0);

	set = setOf("<EquidistanceLevel1>500</EquidistanceLevel1><SymbolNumberLevel1>101000</SymbolNumberLevel1>");
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(QJsonObject{
	    { QStringLiteral("smallest"), QStringLiteral("101.0") }, { QStringLiteral("main"), QStringLiteral("101.0") }, { QStringLiteral("index"), QStringLiteral("101.0") } }));

	set = setOf("<EquidistanceLevel1>500</EquidistanceLevel1><EquidistanceLevel2>2500</EquidistanceLevel2>"
	            "<SymbolNumberLevel1>0</SymbolNumberLevel1><SymbolNumberLevel2>102000</SymbolNumberLevel2>");
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(QJsonObject{ { QStringLiteral("index"), QStringLiteral("102.0") } }));
	QCOMPARE(set.warnings.size(), 1);
	QVERIFY(names(set.warnings, "SymbolNumberLevel1 "));

	auto const with_depressions = [&setOf](const char* different) {
		return setOf(QByteArray(QByteArray("<EquidistanceLevel1>100</EquidistanceLevel1><EquidistanceLevel2>500</EquidistanceLevel2><EquidistanceLevel3>2500</EquidistanceLevel3>"
		                                   "<SymbolNumberLevel1>103000</SymbolNumberLevel1><SymbolNumberLevel2>101000</SymbolNumberLevel2><SymbolNumberLevel3>102001</SymbolNumberLevel3>"
		                                   "<SymbolNumberLevel1Depr>0</SymbolNumberLevel1Depr><SymbolNumberLevel2Depr>104000</SymbolNumberLevel2Depr>"
		                                   "<UseDifferentSymbolsForDepressions>") + different + "</UseDifferentSymbolsForDepressions>").constData());
	};
	set = with_depressions("true");
	QCOMPARE(set.set.value(QLatin1String("depressions")).toBool(), true);
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(three));
	QCOMPARE(compact(set.set.value(QLatin1String("depression_symbols")).toObject()), compact(QJsonObject{ { QStringLiteral("main"), QStringLiteral("104.0") } }));
	QVERIFY2(set.warnings.isEmpty(), qPrintable(set.warnings.join(QLatin1Char('|'))));
	set = with_depressions("false");
	QCOMPARE(set.set.value(QLatin1String("depressions")).toBool(true), false);
	QCOMPARE(compact(set.set.value(QLatin1String("symbols")).toObject()), compact(three));
	QVERIFY(!set.set.contains(QLatin1String("depression_symbols")));
	QVERIFY2(set.warnings.isEmpty(), qPrintable(set.warnings.join(QLatin1Char('|'))));

	// (3) The script run on a cloud.
#ifdef FOOM_TEST_ELEVATION
	if (!PointCloud::supported())
		QSKIP("Built without LASzip");
	QDir const run_dir(file("run"));
	for (auto const* folder : folders)
		QVERIFY(run_dir.mkpath(QLatin1String(folder)));
	{
		// The template: the map of the other cases with the contour symbols 101.0 and 102.0.
		Map map;
		map.setScaleDenominator(10000);
		map.setGeoreferencing(utm32Georeferencing());
		auto* brown = new MapColor(QStringLiteral("Brown"), 0);
		brown->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
		brown->setRgbFromCmyk();
		map.addColor(brown, 0);
		for (auto number : { 101, 102 })
		{
			auto* line = new LineSymbol();
			line->setNumberComponent(0, number);
			line->setNumberComponent(1, 0);
			line->setLineWidth(number == 101 ? 0.14 : 0.25);
			line->setColor(brown);
			map.addSymbol(line, map.getNumSymbols());
		}
		QVERIFY(saveMap(map, file("contour-template.foom")));
	}
	auto result = runJob("template.json", { stepOf("save-as", { { QStringLiteral("path"), QStringLiteral("run/Template.ocd") } }) }, "contour-template.foom", false);
	QVERIFY2(result.exit_code == 0, result.err.constData());
	auto const template_bytes = [&run_dir]() {
		QFile ocd(run_dir.filePath(QStringLiteral("Template.ocd")));
		return ocd.open(QIODevice::ReadOnly) ? ocd.readAll() : QByteArray();
	};
	auto const before = template_bytes();
	QVERIFY(!before.isEmpty());

	std::vector<CloudPoint> points;
	for (int i = 0; i < 40; ++i)
	{
		for (int j = 0; j < 40; ++j)
		{
			CloudPoint ground;
			ground.x = 500000.5 + i;
			ground.y = 5000000.5 + j;
			ground.z = 100.0 + 0.5 * (i + 0.5);
			ground.classification = 2;
			ground.return_number = 2;
			ground.number_of_returns = 2;
			points.push_back(ground);
			auto canopy = ground;
			canopy.z += 10.0;
			canopy.classification = 5;
			canopy.return_number = 1;
			points.push_back(canopy);
			auto noise = ground;
			noise.z = 50.0;
			noise.classification = 7;
			noise.return_number = 1;
			noise.number_of_returns = 1;
			points.push_back(noise);
		}
	}
	QString error;
	QVERIFY2(PointCloud::write(run_dir.filePath(QStringLiteral("0_LAZ/plane.laz")), points, {}, 0, &error), qPrintable(error));
	QVERIFY(QFile::copy(fixture.fileName(), run_dir.filePath(QStringLiteral("script.xml"))));

	result = runFile(run_dir.filePath(QStringLiteral("script.xml")));
	QVERIFY2(result.exit_code == 0, QByteArray(result.err + result.out).constData());
	QCOMPARE(result.lines().front().value(QLatin1String("notes")).toArray().size(), 4);
	QVERIFY(result.lines().front().value(QLatin1String("unsupported")).toArray().isEmpty());

	// The grids lie beside the saved map, from the points the script chose.
	QVERIFY(!QFileInfo::exists(run_dir.filePath(QStringLiteral("Template_DTM.tif"))));
	auto const dtm = ElevationModel::open(run_dir.filePath(QStringLiteral("1_ocdDem/Map_DTM.tif")));
	auto const dsm = ElevationModel::open(run_dir.filePath(QStringLiteral("1_ocdDem/Map_DSM.tif")));
	QVERIFY(dtm);
	QVERIFY(dsm);
	QCOMPARE(dtm->cellSize(), 2.0);
	QVERIFY2(near(dtm->at(QPointF(500021.0, 5000021.0)), 110.5, 0.01), qPrintable(QString::number(dtm->at(QPointF(500021.0, 5000021.0)))));
	QVERIFY2(near(dsm->at(QPointF(500021.0, 5000021.0)), 115.5, 0.01), qPrintable(QString::number(dsm->at(QPointF(500021.0, 5000021.0)))));

	// The contours, in a map of their own and then imported.
	auto const wizard = result.logOf("elevation-wizard");
	auto const contour_count = wizard.value(QLatin1String("contours")).toInt();
	QVERIFY2(contour_count >= 3, qPrintable(QString::number(contour_count)));
	QVERIFY(wizard.value(QLatin1String("warnings")).toArray().isEmpty());
	// On the symbol which the script names (no level of the plane is a multiple of 25 m), and no symbol of FOOM's own.
	Map template_map;
	QVERIFY(template_map.loadFrom(run_dir.filePath(QStringLiteral("Template.ocd"))));
	Map contours_only;
	QVERIFY(contours_only.loadFrom(run_dir.filePath(QStringLiteral("5_CreateContourLines/Contours.ocd"))));
	QCOMPARE(contours_only.getNumObjects(), contour_count);
	for (int i = 0; i < contour_count; ++i)
		QCOMPARE(contours_only.getPart(0)->getObject(i)->getSymbol()->getNumberAsString(), QStringLiteral("101.0"));
	QCOMPARE(contours_only.getNumSymbols(), template_map.getNumSymbols());
	Map contour_map;
	QVERIFY(contour_map.loadFrom(run_dir.filePath(QStringLiteral("6_ImportAllOcds/Contour Lines.ocd"))));
	QCOMPARE(contour_map.getNumObjects(), contour_count);

	// The vegetation base map, beside its map and a template of it.
	QVERIFY(QFileInfo::exists(run_dir.filePath(QStringLiteral("7_png/Map_Vegetation_vegetation.tif"))));
	Map vegetation_map;
	QVERIFY(vegetation_map.loadFrom(run_dir.filePath(QStringLiteral("7_png/Map_Vegetation.ocd"))));
	auto has_raster = false;
	for (int i = 0; i < vegetation_map.getNumTemplates(); ++i)
		has_raster = has_raster || vegetation_map.getTemplate(i)->getTemplateFilename() == QLatin1String("Map_Vegetation_vegetation.tif");
	QVERIFY(has_raster);

	QCOMPARE(template_bytes(), before);
#else
	QSKIP("Built without the elevation library");
#endif
}


QTEST_MAIN(RunnerBackgroundTest)
#include "runner_background_t.moc"  // IWYU pragma: keep
