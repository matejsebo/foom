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

#ifndef OPENORIENTEERING_SETTINGS_TRANSFER_H
#define OPENORIENTEERING_SETTINGS_TRANSFER_H

#include <QString>
#include <QStringList>

class QSettings;

namespace OpenOrienteering {

/**
 * Export and import of all settings as one file.
 *
 * The file is an ini file (QSettings::IniFormat) with every key of the
 * settings, and the group "FOOM-Settings" which marks it as such a file.
 *
 * See the design notes, "Settings transfer: as built".
 */
namespace SettingsTransfer
{
	/** The group which marks a settings file. */
	QString markerGroup();

	/** The keys which are never transferred: state of this machine's session. */
	QStringList excludedKeys();

	/**
	 * Writes all keys of the settings, except the excluded ones, to a new file
	 * at path. Returns the number of keys written, or -1 with error set.
	 */
	int exportTo(const QSettings& settings, const QString& path, QString* error = nullptr);

	/**
	 * Copies all keys of the file at path, except the marker and the excluded
	 * keys, into the settings; other keys of the settings stay. Returns the
	 * number of keys copied, or -1 with error set when the file is not a
	 * settings file.
	 */
	int importFrom(const QString& path, QSettings& settings, QString* error = nullptr);
}


}  // namespace OpenOrienteering

#endif
