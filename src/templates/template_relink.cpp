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


#include "template_relink.h"

#include <memory>
#include <utility>

#include <Qt>
#include <QtGlobal>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>

#include "core/map.h"
#include "templates/template.h"
#include "templates/template_placeholder.h"


namespace OpenOrienteering {

namespace {

Qt::CaseSensitivity pathCase()
{
#ifdef Q_OS_WIN
	return Qt::CaseInsensitive;
#else
	return Qt::CaseSensitive;
#endif
}

/** The path with forward slashes, cleaned, without a trailing slash (but for a root). */
QString normalPath(const QString& path)
{
	auto result = QDir::cleanPath(QDir::fromNativeSeparators(path));
	while (result.size() > 1 && result.endsWith(QLatin1Char('/')) && !result.endsWith(QLatin1String(":/")))
		result.chop(1);
	return result;
}

/** The part of the path below the folder, or an empty string when the path is not in it. */
QString pathBelow(const QString& path, const QString& folder)
{
	if (folder.isEmpty())
		return {};
	auto const prefix = folder.endsWith(QLatin1Char('/')) ? folder : QString(folder + QLatin1Char('/'));
	auto const clean = normalPath(path);
	if (!clean.startsWith(prefix, pathCase()))
		return {};
	return clean.mid(prefix.size());
}

}  // namespace


int missingTemplateCount(const Map& map)
{
	auto count = 0;
	for (int i = 0; i < map.getNumTemplates(); ++i)
		count += map.getTemplate(i)->getTemplateState() == Template::Invalid ? 1 : 0;
	return count;
}

QString missingTemplatesFolder(const Map& map)
{
	QStringList common;
	auto first = true;
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = map.getTemplate(i);
		if (temp->getTemplateState() != Template::Invalid)
			continue;
		auto parts = normalPath(QFileInfo(temp->getTemplatePath()).path()).split(QLatin1Char('/'));
		if (first)
		{
			common = parts;
			first = false;
			continue;
		}
		int n = 0;
		while (n < common.size() && n < parts.size() && QString::compare(common[n], parts[n], pathCase()) == 0)
			++n;
		common = common.mid(0, n);
	}
	if (first || common.isEmpty())
		return {};
	if (common.size() == 1 && common.front().isEmpty())
		return QStringLiteral("/");
	return common.join(QLatin1Char('/'));
}

TemplateRelinkReport relinkTemplates(Map& map, const QString& old_folder, const QString& new_folder)
{
	TemplateRelinkReport report;
	auto const from = normalPath(old_folder);
	auto const to = QDir(normalPath(new_folder));
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto* temp = map.getTemplate(i);
		auto const rest = pathBelow(temp->getTemplatePath(), from);
		if (rest.isEmpty())
			continue;
		auto const new_path = to.absoluteFilePath(rest);
		if (!QFileInfo(new_path).isFile())
		{
			report.not_found.push_back(normalPath(temp->getTemplatePath()));
			continue;
		}

		if (relinkTemplateFile(map, i, new_path))
			report.relinked.push_back(new_path);
		else
			report.failed.push_back(new_path);
	}
	return report;
}

bool relinkTemplateFile(Map& map, int index, const QString& new_path)
{
	if (index < 0 || index >= map.getNumTemplates())
		return false;
	auto* temp = map.getTemplate(index);
	auto const old_path = temp->getTemplatePath();
	auto const old_state = temp->getTemplateState();
	if (auto* placeholder = qobject_cast<TemplatePlaceholder*>(temp))
	{
		// As the Templates window does: the type is known once the file is.
		placeholder->setTemplatePath(new_path);
		auto actual = placeholder->makeActualTemplate();
		if (!actual || !actual->loadTemplateFile())
		{
			placeholder->setTemplatePath(old_path);
			placeholder->setTemplateState(old_state);
			return false;
		}
		map.setTemplate(index, std::move(actual)).release()->deleteLater();
	}
	else
	{
		temp->switchTemplateFile(new_path, true);
		if (temp->getTemplateState() != Template::Loaded)
		{
			temp->switchTemplateFile(old_path, old_state == Template::Loaded);
			temp->setTemplateState(old_state);
			return false;
		}
	}
	map.getTemplate(index)->setTemplateAreaDirty();
	map.emitTemplateChanged(map.getTemplate(index));
	map.setTemplatesDirty();
	return true;
}

QString relinkStartFolder(const QString& old_folder, const QString& map_path)
{
	auto folder = normalPath(old_folder);
	// foom: a relative path is relative to the map, not to the program's working folder
	// (seen on 2026-11-22: the chooser opened in the folder FOOM was started from).
	if (!folder.isEmpty() && QFileInfo(folder).isRelative())
		folder = map_path.isEmpty() ? QString() : normalPath(QDir(QFileInfo(map_path).absolutePath()).absoluteFilePath(folder));
	while (!folder.isEmpty())
	{
		if (QFileInfo(folder).isDir())
			return folder;
		auto const up = normalPath(QFileInfo(folder).path());
		if (up == folder)
			break;
		folder = up;
	}
	return map_path.isEmpty() ? QString() : normalPath(QFileInfo(map_path).absolutePath());
}

QString relinkByFolderHint(const Map& map)
{
	if (missingTemplateCount(map) < 2)
		return {};
	return QLatin1Char(' ') + QCoreApplication::translate("OpenOrienteering::TemplateRelink",
	                                                      "Several templates are missing: Templates > Relink by folder... finds them all in a new folder at once.");
}


}  // namespace OpenOrienteering
