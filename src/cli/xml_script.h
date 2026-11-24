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


#ifndef OPENORIENTEERING_CLI_XML_SCRIPT_H
#define OPENORIENTEERING_CLI_XML_SCRIPT_H

#include <vector>

#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace OpenOrienteering {

namespace Cli {

/**
 * An XML script, read as the steps of a job.
 *
 * See the design notes, "XML scripts" and "The commands
 * of the LiDAR script". The root OcadScript, the command CsCanvas and
 * the commands of the files, of the DEM and of LiDAR (File.Open,
 * Dem.DemImport, ...) are the format's; the other commands are FOOM's own.
 */
struct XmlScript
{
	struct Unsupported
	{
		QString command;
		int line = 0;
		QString reason;
	};

	/// A command which was read and makes no step.
	struct Note
	{
		QString command;
		int line = 0;
		QString note;
	};

	QJsonArray steps;                       ///< As the "steps" of a job file.
	std::vector<QStringList> warnings;      ///< For each step, what the script asked for and FOOM does not do.
	int commands = 0;                       ///< Commands in the script.
	std::vector<Unsupported> unsupported;   ///< Commands which are not run.
	std::vector<Note> notes;                ///< Commands which need no step.

	/// The first line of the log: the script, its commands and what is not run.
	QJsonObject report(const QString& path) const;
};


/** True when the bytes begin, after white space, an XML declaration and comments, with <OcadScript. */
bool isXmlScript(const QByteArray& bytes);

/**
 * Translates the commands of a script into steps.
 *
 * The folder is that of the script: the commands which name a folder of
 * point clouds, grids or maps are given the files which are in it now.
 *
 * @throws JobError when the script is not well formed, or saves a file
 *         while none is open.
 */
XmlScript readXmlScript(const QByteArray& bytes, const QDir& folder);


}  // namespace Cli

}  // namespace OpenOrienteering

#endif
