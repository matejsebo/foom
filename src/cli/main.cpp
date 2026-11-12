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


#include <clocale>
#include <cstdio>

#include <QtGlobal>
#include <QtPlugin>  // IWYU pragma: keep
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLatin1String>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTranslator>

#include "global.h"
#include "mapper_config.h"
#include "mapper_resource.h"
#include "util/translation_util.h"
#include "cli/job.h"

using namespace OpenOrienteering;


#if defined(SCALING_ICON_ENGINE_PLUGIN)
Q_IMPORT_PLUGIN(ScalingIconEnginePlugin)
#endif


// From map.h

namespace OpenOrienteering {

extern QPointer<QTranslator> map_symbol_translator;

}  // namespace OpenOrienteering


/**
 * foom-cli, the headless runner.
 *
 * `foom-cli --run job.json [--platform offscreen]` executes the steps of a
 * job file without the editor window. See "Headless runner" in
 * the design notes for the steps and the exit codes.
 */
int main(int argc, char** argv)
{
	// Text rendering needs fonts, and fonts need a GUI application object.
	// The offscreen platform plugin provides them without a display.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
	
	// QGuiApplication takes -platform / --platform from the arguments.
	QGuiApplication qapp(argc, argv);
	
	// Load resources
	Q_INIT_RESOURCE(resources);
	
	// Same organization and application as FOOM: shared settings, e.g. the language.
	QCoreApplication::setOrganizationName(QString::fromLatin1("FOOM"));
	QCoreApplication::setApplicationName(QString::fromLatin1("FOOM"));
	QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));

#ifdef WIN32
	// Load plugins on Windows
	QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + QLatin1String("/plugins"));
#endif

	MapperResource::setSeachPaths();
	
	// Localization
	QSettings settings;
	TranslationUtil::setBaseName(QLatin1String("OpenOrienteering"));
	TranslationUtil translation(settings);
	QLocale::setDefault(QLocale(translation.code()));
	QCoreApplication::installTranslator(&translation.getQtTranslator());
	QCoreApplication::installTranslator(&translation.getAppTranslator());
	map_symbol_translator = translation.load(QString::fromLatin1("map_symbols")).release();
	if (map_symbol_translator)
		map_symbol_translator->setParent(&qapp);
	
	// Avoid numeric issues in libraries such as GDAL
	setlocale(LC_NUMERIC, "C");
	
	// Initialize static things like the file format registry.
	doStaticInitializations();
	
	QCommandLineParser parser;
	parser.setApplicationDescription(QLatin1String(
	  "foom-cli runs the steps of a job file without the editor window.\n"
	  "The job file is JSON: {\"steps\": [{\"step\": \"open\", \"path\": \"map.foom\"}, ...]}.\n"
	  "Steps: open, import, apply-crt, replace-symbols, change-scale,\n"
	  "set-georeferencing, check, export, save-as. Paths are relative to the job file.\n"
	  "One JSON line per executed step goes to standard output.\n"
	  "Exit code 0 on success, 1 on a failed step, 2 on a malformed job."));
	parser.addHelpOption();
	parser.addVersionOption();
	QCommandLineOption run_option(QStringList{ QLatin1String("run") },
	                              QLatin1String("Run the steps of this job file."),
	                              QLatin1String("job.json"));
	parser.addOption(run_option);
	QCommandLineOption platform_option(QStringList{ QLatin1String("platform") },
	                                   QLatin1String("Qt platform plugin, default offscreen."),
	                                   QLatin1String("name"));
	parser.addOption(platform_option);  // consumed by QGuiApplication; listed for --help
	parser.process(qapp);
	
	if (!parser.isSet(run_option))
	{
		QTextStream(stderr) << QLatin1String("foom-cli: no job file given\n") << parser.helpText();
		return Cli::MalformedJob;
	}
	
	auto const job_path = parser.value(run_option);
	try
	{
		auto const job = Cli::Job::load(job_path);
		QTextStream log(stdout);
		return job.run(log);
	}
	catch (Cli::JobError const& error)
	{
		QTextStream(stderr) << QLatin1String("foom-cli: ") << job_path << QLatin1String(": ") << error.message() << '\n';
		return Cli::MalformedJob;
	}
}
