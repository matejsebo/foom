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


#include <cstdlib>
#include <memory>
#include <vector>

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
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global.h"
#include "test_config.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "core/print_output.h"
#include "core/objects/object.h"
#include "core/symbols/area_symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"

using namespace OpenOrienteering;


namespace {

const QRectF area(0, 0, 20, 10);
constexpr auto dpi = 254.0;  // 10 pixels per mm

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

MapColor* addColor(Map& map, const QString& name, const MapColorCmyk& cmyk, const QString& spot_name = {})
{
	auto* color = new MapColor(name, map.getNumColors());
	if (!spot_name.isEmpty())
		color->setSpotColorName(spot_name);
	color->setCmyk(cmyk);
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

int ink(const RasterPlate& plate, int x, int y)
{
	return plate.ink.constScanLine(y)[x];
}

const RasterPlate* plateNamed(const std::vector<RasterPlate>& plates, const char* name)
{
	for (auto const& plate : plates)
	{
		if (plate.name == QLatin1String(name))
			return &plate;
	}
	return nullptr;
}

}  // namespace



/**
 * @test Tests templates on the plate of a spot colour.
 *
 * See the design notes, "Templates on a spot colour plate".
 */
class TemplateSeparationTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/** The map of the acceptance criteria; returns the template. */
	TemplateImage* fillMap(Map& map) const
	{
		map.setScaleDenominator(10000);
		addColor(map, QStringLiteral("Black"), { 0.0f, 0.0f, 0.0f, 1.0f }, QStringLiteral("BLACK"));
		auto* brown = addColor(map, QStringLiteral("Brown"), { 0.0f, 0.56f, 1.0f, 0.18f }, QStringLiteral("BROWN"));
		addColor(map, QStringLiteral("Process"), { 0.4f, 0.0f, 0.0f, 0.2f });
		auto* symbol = new AreaSymbol();
		symbol->setColor(brown);
		map.addSymbol(symbol, 0);
		MapCoordVector coords { MapCoord(0.0, 0.0), MapCoord(2.0, 0.0), MapCoord(2.0, 2.0), MapCoord(0.0, 2.0), MapCoord(0.0, 0.0) };
		coords.back().setClosePoint(true);
		map.addObject(new PathObject(symbol, coords));
		
		map.addTemplate(-1, std::make_unique<TemplateImage>(file("grey.png"), &map));
		auto* temp = qobject_cast<TemplateImage*>(map.getTemplate(0));
		temp->setTemplateScaleX(1.0);
		temp->setTemplateScaleY(1.0);
		if (!temp->loadTemplateFile())
			return nullptr;
		temp->setTemplateState(Template::Loaded);
		return temp;
	}
	
	const MapColor* colorNamed(const Map& map, const char* name) const
	{
		for (int i = 0; i < map.getNumColors(); ++i)
		{
			if (map.getColor(i)->getName() == QLatin1String(name))
				return map.getColor(i);
		}
		return nullptr;
	}
	
	int runJob(const QString& map_path, const QJsonObject& export_step, const char* name, QByteArray* output) const
	{
		QJsonArray steps;
		steps.append(QJsonObject{ { QStringLiteral("step"), QStringLiteral("open") }, { QStringLiteral("path"), map_path } });
		auto step = export_step;
		step.insert(QStringLiteral("step"), QStringLiteral("export"));
		steps.append(step);
		auto const job_path = file(name);
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return -1;
		*output = process.readAllStandardOutput() + process.readAllStandardError();
		return process.exitCode();
	}
	
private slots:
	void initTestCase()
	{
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(dir.isValid());
		QImage grey(200, 100, QImage::Format_RGB32);
		grey.fill(QColor(128, 128, 128));
		QVERIFY(grey.save(file("grey.png"), "PNG"));
	}
	
	/// Accept (1)
	void flagTest()
	{
		Map map;
		auto* temp = fillMap(map);
		QVERIFY(temp);
		QVERIFY(!temp->separationColor());
		auto const* brown = colorNamed(map, "Brown");
		temp->setSeparationColor(brown);
		QCOMPARE(temp->separationColor(), brown);
		QVERIFY(map.areTemplatesDirty());
		auto copy = std::unique_ptr<Template>(temp->duplicate());
		QCOMPARE(copy->separationColor(), brown);
		
		QVERIFY(saveMap(map, file("separation.foom")));
		QFile saved(file("separation.foom"));
		QVERIFY(saved.open(QIODevice::ReadOnly));
		QCOMPARE(saved.readAll().count("separation=\""), 1);
		
		Map reloaded;
		QVERIFY(loadMap(reloaded, file("separation.foom")));
		QCOMPARE(reloaded.getNumTemplates(), 1);
		QVERIFY(reloaded.getTemplate(0)->separationColor());
		QCOMPARE(reloaded.getTemplate(0)->separationColor()->getName(), QStringLiteral("Brown"));
		
		reloaded.deleteColor(reloaded.findColorIndex(reloaded.getTemplate(0)->separationColor()));
		QVERIFY(!reloaded.getTemplate(0)->separationColor());
	}
	
	/// Accept (2)
	void platesTest()
	{
		Map map;
		auto* temp = fillMap(map);
		QVERIFY(temp);
		auto const bounds = temp->calculateTemplateBoundingBox();
		QVERIFY2(bounds.contains(area), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bounds.left()).arg(bounds.top()).arg(bounds.width()).arg(bounds.height())));
		temp->setSeparationColor(colorNamed(map, "Brown"));
		MapView view(&map);
		view.setTemplateVisibility(temp, { 1.0f, true });
		
		auto plates = renderSpotPlates(map, area, dpi, 1.0, true, &view);
		auto const* brown = plateNamed(plates, "BROWN");
		auto const* black = plateNamed(plates, "BLACK");
		QVERIFY(brown && black);
		QVERIFY2(std::abs(ink(*brown, 100, 50) - 127) <= 3, qPrintable(QString::number(ink(*brown, 100, 50))));
		QCOMPARE(ink(*brown, 10, 10), 255);
		QCOMPARE(ink(*black, 100, 50), 0);
		
		plates = renderSpotPlates(map, area, dpi, 1.0, false, &view);
		QCOMPARE(ink(*plateNamed(plates, "BROWN"), 100, 50), 0);
		
		view.setTemplateVisibility(temp, { 1.0f, false });
		plates = renderSpotPlates(map, area, dpi, 1.0, true, &view);
		QCOMPARE(ink(*plateNamed(plates, "BROWN"), 100, 50), 0);
		
		view.setTemplateVisibility(temp, { 1.0f, true });
		temp->setSeparationColor(nullptr);
		plates = renderSpotPlates(map, area, dpi, 1.0, true, &view);
		QCOMPARE(ink(*plateNamed(plates, "BROWN"), 100, 50), 0);
	}
	
	/// Accept (3)
	void saveTest()
	{
		Map map;
		auto* temp = fillMap(map);
		QVERIFY(temp);
		temp->setSeparationColor(colorNamed(map, "Brown"));
		QStringList files;
		QString error;
		QVERIFY2(saveSpotPlates(map, area, dpi, 1.0, file("saved.png"), PlateForm::Grey, &files, &error, true, nullptr), qPrintable(error));
		const QImage brown(file("saved-BROWN.png"));
		QVERIFY(!brown.isNull());
		QVERIFY2(std::abs(qGray(brown.pixel(100, 50)) - 128) <= 3, qPrintable(QString::number(qGray(brown.pixel(100, 50)))));
	}
	
	/// Accept (4)
	void runnerTest()
	{
		{
			Map map;
			auto* temp = fillMap(map);
			QVERIFY(temp);
			temp->setSeparationColor(colorNamed(map, "Brown"));
			QVERIFY(saveMap(map, file("runner.foom")));
		}
		QJsonObject export_step {
			{ QStringLiteral("path"), file("with.tif") },
			{ QStringLiteral("extent"), QJsonArray{ 0, 0, 20, 10 } },
			{ QStringLiteral("dpi"), 254 },
			{ QStringLiteral("plates"), QStringLiteral("spot") },
			{ QStringLiteral("templates"), true },
		};
		QByteArray output;
		QVERIFY2(runJob(file("runner.foom"), export_step, "with.json", &output) == 0, output.constData());
		const QImage with(file("with-BROWN.tif"));
		QVERIFY2(!with.isNull(), output.constData());
		QVERIFY2(std::abs(qGray(with.pixel(100, 50)) - 128) <= 3, qPrintable(QString::number(qGray(with.pixel(100, 50)))));
		
		export_step.insert(QStringLiteral("path"), file("without.tif"));
		export_step.insert(QStringLiteral("templates"), false);
		QVERIFY2(runJob(file("runner.foom"), export_step, "without.json", &output) == 0, output.constData());
		const QImage without(file("without-BROWN.tif"));
		QVERIFY(!without.isNull());
		QCOMPARE(qGray(without.pixel(100, 50)), 255);
	}
};


QTEST_MAIN(TemplateSeparationTest)
#include "template_separation_t.moc"  // IWYU pragma: keep
