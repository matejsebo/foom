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


#include "job.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QStringList>
#include <QTextStream>

#include "xml_script.h"  // foom
#include "steps.h"


namespace OpenOrienteering {

namespace Cli {

QString Step::prefix() const
{
	if (name.isEmpty())
		return QString::fromLatin1("step %1: ").arg(index);
	return QString::fromLatin1("step %1 (%2): ").arg(QString::number(index), name);
}



// static
Job Job::load(const QString& path, bool strict)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		throw JobError(QLatin1String("cannot read the job file: ") + file.errorString());
	auto const bytes = file.readAll();
	
	// foom: the design notes
	QJsonObject script_report;
	std::vector<QStringList> script_warnings;  // for each step of a script
	QJsonDocument document;
	if (isXmlScript(bytes))
	{
		auto const script = readXmlScript(bytes, QFileInfo(path).absoluteDir());
		if (strict && !script.unsupported.empty())
		{
			QStringList names;
			for (auto const& entry : script.unsupported)
				names.push_back(QString::fromLatin1("%1 (line %2: %3)").arg(entry.command, QString::number(entry.line), entry.reason));
			throw JobError(QLatin1String("unsupported commands: ") + names.join(QLatin1String(", ")));
		}
		script_report = script.report(path);
		script_warnings = script.warnings;
		document = QJsonDocument(script.steps);
	}
	else
	{
		QJsonParseError parse_error;
		document = QJsonDocument::fromJson(bytes, &parse_error);
		if (document.isNull())
			throw JobError(QLatin1String("the job file is not JSON: ") + parse_error.errorString());
	}
	
	QJsonArray steps;
	if (document.isArray())
	{
		steps = document.array();
	}
	else if (document.isObject())
	{
		auto const steps_value = document.object().value(QLatin1String("steps"));
		if (!steps_value.isArray())
			throw JobError(QLatin1String("the job object needs a \"steps\" array"));
		steps = steps_value.toArray();
	}
	else
	{
		throw JobError(QLatin1String("the job file must hold a JSON object with a \"steps\" array"));
	}
	
	Job job;
	job.base_dir = QFileInfo(path).absoluteDir();
	job.script_report = script_report;
	job.job_steps.reserve(std::size_t(steps.size()));
	
	auto map_open = false;
	auto index = 0;
	for (auto const& value : steps)
	{
		++index;
		Step step { index, {}, {}, {} };
		if (std::size_t(index) <= script_warnings.size())
			step.warnings = script_warnings[std::size_t(index) - 1];  // foom
		if (!value.isObject())
			throw JobError(step.prefix() + QLatin1String("a step must be a JSON object"));
		
		step.params = value.toObject();
		auto const name = step.params.take(QLatin1String("step"));
		if (!name.isString() || name.toString().isEmpty())
			throw JobError(step.prefix() + QLatin1String("the step has no \"step\" name"));
		step.name = name.toString();
		
		StepRunner::validate(step, map_open);
		job.job_steps.push_back(std::move(step));
	}
	
	return job;
}


QString Job::resolvePath(const QString& path) const
{
	return QDir::cleanPath(base_dir.absoluteFilePath(path));
}


int Job::run(QTextStream& log) const
{
	if (!script_report.isEmpty())  // foom
	{
		log << QString::fromUtf8(QJsonDocument(script_report).toJson(QJsonDocument::Compact)) << '\n';
		log.flush();
	}
	StepRunner runner(*this);
	for (auto const& step : job_steps)
	{
		QElapsedTimer timer;
		timer.start();
		auto result = runner.run(step);
		
		auto line = result.details;
		line.insert(QLatin1String("step"), step.name);
		line.insert(QLatin1String("index"), step.index);
		line.insert(QLatin1String("ok"), result.ok);
		line.insert(QLatin1String("ms"), timer.elapsed());
		if (!result.ok)
			line.insert(QLatin1String("message"), result.message);
		if (!step.warnings.isEmpty())  // foom: what an XML script asked for and the step does not do
		{
			// Beside the step's own warnings; a step which counts its warnings there keeps its number.
			auto const own = line.value(QLatin1String("warnings"));
			auto const name = own.isUndefined() || own.isArray() ? QLatin1String("warnings") : QLatin1String("script_warnings");
			auto list = line.value(name).toArray();
			for (auto const& warning : step.warnings)
				list.append(warning);
			line.insert(name, list);
		}
		
		log << QString::fromUtf8(QJsonDocument(line).toJson(QJsonDocument::Compact)) << '\n';
		log.flush();
		
		if (!result.ok)
			return FailedStep;
	}
	return Success;
}


}  // namespace Cli

}  // namespace OpenOrienteering
