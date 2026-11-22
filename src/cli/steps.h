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


#ifndef OPENORIENTEERING_CLI_STEPS_H
#define OPENORIENTEERING_CLI_STEPS_H

#include <memory>

#include <QJsonObject>
#include <QString>

namespace OpenOrienteering {

class Map;

namespace Cli {

class Job;
struct Step;


/**
 * The outcome of one step.
 */
struct StepResult
{
	bool ok = true;
	QString message;        ///< Why the step failed; empty on success.
	QJsonObject details;    ///< Extra members for the log line, e.g. the written path.
};


/**
 * Validates and runs the steps of a job.
 *
 * The steps are those of the table in "Headless runner",
 * the design notes. Parameters whose features are not built yet
 * are rejected by validate(): the `course` parameter of `export`.
 */
/**
 * foom: The PROJ specification of a CRS written as the `crs` parameter of
 * `set-georeferencing` takes it; empty for `local`.
 *
 * @throws JobError on a CRS which is not known.
 */
QString crsSpecOf(const Step& step, const QString& text);


class StepRunner
{
public:
	explicit StepRunner(const Job& job);
	
	StepRunner(const StepRunner&) = delete;
	StepRunner& operator=(const StepRunner&) = delete;
	
	~StepRunner();
	
	/**
	 * Checks the step's name and parameters.
	 *
	 * map_open tells whether an `open` step precedes this one; the steps
	 * which work on a map require it, and `open` sets it.
	 *
	 * @throws JobError on a problem.
	 */
	static void validate(const Step& step, bool& map_open);
	
	/**
	 * Runs a validated step.
	 */
	StepResult run(const Step& step);

private:
	StepResult open(const Step& step);
	StepResult import(const Step& step);
	StepResult newMap(const Step& step);  // foom
	StepResult applyCrt(const Step& step);
	StepResult replaceSymbols(const Step& step);
	StepResult changeScale(const Step& step);
	StepResult setGeoreferencing(const Step& step);
	StepResult exportMap(const Step& step);
	StepResult saveAs(const Step& step);
	StepResult exportCourseMapsStep(const Step& step);  // foom
	StepResult analyseRoutesStep(const Step& step);  // foom
	StepResult exportRoutesMapStep(const Step& step);  // foom
	StepResult checkStep(const Step& step);  // foom
	StepResult courseStatisticsStep(const Step& step);  // foom
	StepResult exportCoursesStep(const Step& step);  // foom
	StepResult importCoursesStep(const Step& step);  // foom
	StepResult hillShadeStep(const Step& step);  // foom
	StepResult elevationWizardStep(const Step& step);  // foom
	StepResult fieldCopyCreate(const Step& step);  // foom
	StepResult mergeFromFile(const Step& step);  // foom
	StepResult fieldCopyClose(const Step& step);  // foom
	StepResult backupStep(const Step& step);  // foom
	
	StepResult exportImage(const Step& step, const QString& path, const QString& format);
	StepResult exportPdf(const Step& step, const QString& path);
	
	const Job& job;
	std::unique_ptr<Map> map;
	QString map_path;  ///< The file the map was opened from, or empty.
};


}  // namespace Cli

}  // namespace OpenOrienteering

#endif  // OPENORIENTEERING_CLI_STEPS_H
