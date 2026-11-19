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
	// On Windows the platform of the editor draws text as the editor does,
	// and opens no window. Elsewhere the offscreen platform knows the fonts
	// of the system and needs no display.
	auto platform = qgetenv("QT_QPA_PLATFORM");
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (qstrcmp(argv[i], "-platform") == 0 || qstrcmp(argv[i], "--platform") == 0)
			platform = argv[i + 1];
	}
	if (platform.isEmpty())
	{
#ifdef Q_OS_WIN
		platform = "windows";
#else
		platform = "offscreen";
#endif
		qputenv("QT_QPA_PLATFORM", platform);
	}
#ifdef Q_OS_WIN
	// On Windows the offscreen platform knows no fonts unless it is told
	// where they are, and then draws every text as a filled box.
	if (platform.startsWith("offscreen") && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
	{
		auto fonts = qgetenv("WINDIR");
		if (fonts.isEmpty())
			fonts = "C:\\Windows";
		fonts += "\\Fonts";
		qputenv("QT_QPA_FONTDIR", fonts);
	}
#endif
	
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
	  "Steps: open, new-map, import, apply-crt, replace-symbols, change-scale,\n"
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
	                                   QLatin1String("Qt platform plugin; default windows on Windows, offscreen elsewhere."),
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
