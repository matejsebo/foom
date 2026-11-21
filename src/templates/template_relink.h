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


#ifndef OPENORIENTEERING_TEMPLATE_RELINK_H
#define OPENORIENTEERING_TEMPLATE_RELINK_H

#include <QString>
#include <QStringList>

namespace OpenOrienteering {

class Map;


/** What relinkTemplates() did. */
struct TemplateRelinkReport
{
	/** The new paths of the templates which were relinked and loaded. */
	QStringList relinked;
	/** The paths of the templates in the old folder whose file is not in the new one; they were not changed. */
	QStringList not_found;
	/** The new paths at which a file was found but could not be loaded; those templates were put back. */
	QStringList failed;
};


/** The number of templates whose file was not found (state Invalid). */
int missingTemplateCount(const Map& map);

/**
 * The deepest folder which holds the paths of all missing templates;
 * empty when none is missing.
 */
QString missingTemplatesFolder(const Map& map);

/**
 * Moves the templates whose path lies in old_folder to the same path in
 * new_folder, where a file is there, and loads them.
 *
 * See the design notes, "Relink by folder".
 */
TemplateRelinkReport relinkTemplates(Map& map, const QString& old_folder, const QString& new_folder);

/**
 * A sentence for the warning about lost templates which names the command,
 * starting with a space, when two or more templates are missing; else empty.
 */
QString relinkByFolderHint(const Map& map);


}  // namespace OpenOrienteering

#endif
