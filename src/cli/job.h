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


#ifndef OPENORIENTEERING_CLI_JOB_H
#define OPENORIENTEERING_CLI_JOB_H

#include <vector>

#include <QDir>
#include <QJsonObject>
#include <QString>

class QTextStream;

namespace OpenOrienteering {

namespace Cli {

/**
 * The exit codes of foom-cli, see "Headless runner" in the design notes.
 */
enum ExitCode
{
	Success      = 0,  ///< Every step ran.
	FailedStep   = 1,  ///< A step failed at run time; the log says which.
	MalformedJob = 2   ///< The job file could not be parsed or validated; nothing ran.
};


/**
 * An error in the job file: not JSON, a step without a name, a missing or
 * ill-typed parameter, or a feature which is not built yet.
 *
 * Thrown while loading and validating a job; the runner exits with MalformedJob.
 */
class JobError
{
public:
	explicit JobError(QString message) : message_(std::move(message)) {}
	
	const QString& message() const { return message_; }

private:
	QString message_;
};


/**
 * One step of a job: the step id (e.g. "change-scale") and its parameters.
 */
struct Step
{
	int index;            ///< The 1-based position in the job, for messages.
	QString name;         ///< The step id.
	QJsonObject params;   ///< The step object without the "step" member.
	
	/**
	 * Returns a message prefix naming this step, e.g. "step 3 (export): ".
	 */
	QString prefix() const;
};


/**
 * A job file: a JSON object with a "steps" array, or a bare array of steps.
 *
 * Every step is a JSON object whose "step" member names the step. Path
 * parameters are relative to the folder of the job file.
 */
class Job
{
public:
	/**
	 * Reads and validates the job file at path.
	 *
	 * Validation covers the whole job before any step runs, so that a
	 * malformed step at the end of a job cannot leave half of the outputs
	 * behind.
	 *
	 * @throws JobError when the file cannot be read, parsed or validated.
	 */
	static Job load(const QString& path);
	
	/**
	 * Returns the folder which relative paths in the job refer to.
	 */
	const QDir& baseDir() const { return base_dir; }
	
	/**
	 * Returns the absolute path for a path parameter.
	 */
	QString resolvePath(const QString& path) const;
	
	/**
	 * Returns the validated steps.
	 */
	const std::vector<Step>& steps() const { return job_steps; }
	
	/**
	 * Runs the steps in order and appends one JSON line per executed step
	 * to the log.
	 *
	 * Stops at the first failing step.
	 *
	 * @return Success, or FailedStep.
	 */
	int run(QTextStream& log) const;

private:
	QDir base_dir;
	std::vector<Step> job_steps;
};


}  // namespace Cli

}  // namespace OpenOrienteering

#endif  // OPENORIENTEERING_CLI_JOB_H
