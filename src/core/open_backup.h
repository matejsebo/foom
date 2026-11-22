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

#ifndef OPENORIENTEERING_OPEN_BACKUP_H
#define OPENORIENTEERING_OPEN_BACKUP_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

class QDateTime;
class QSettings;

namespace OpenOrienteering {

class FileFormat;
class Map;
class MapView;

/**
 * The settings of the backup which is made when a file is opened.
 *
 * See the design notes, "Backup on open: as built".
 */
struct OpenBackupOptions
{
	/** The default number of backups kept per file. */
	static constexpr int default_keep = 10;

	bool enabled = false;
	int keep = default_keep;
	bool compress = false;

	/** Reads the options from the settings (keys Backup/OnOpen, Backup/Keep, Backup/Compress). */
	static OpenBackupOptions load(const QSettings& settings);

	/** Writes the options to the settings. */
	void store(QSettings& settings) const;
};


/** Returns the folder of the backups of a file: "backup" beside it. */
QString openBackupFolder(const QString& path);

/**
 * Returns the backups of a file in its backup folder, oldest first.
 *
 * A backup of "name.ext" is "name_yyyyMMdd-HHmmss.ext", with "-2", "-3"...
 * before the extension when there are several in one second, and ".gz"
 * after it when compressed.
 */
QStringList openBackups(const QString& path);

/**
 * Copies a file to its backup folder and removes the oldest backups beyond
 * options.keep.
 *
 * Returns the path of the backup, or an empty string when options.enabled is
 * false, the path is not a local file, or the copy failed; then error, if
 * given, says why (it stays empty when nothing was to be done).
 */
QString makeOpenBackup(const QString& path, const OpenBackupOptions& options, const QDateTime& now, QString* error = nullptr);

/**
 * Writes the map as it is, unsaved changes included, to the backup folder of
 * path, named as makeOpenBackup() names its copies, and removes the oldest
 * backups of that series beyond options.keep. options.enabled is ignored.
 *
 * The map is written by format, the format of the file, when it can save;
 * otherwise in FOOM's own format, and the backup is then named with ".foom".
 * The file at path is not read or written.
 *
 * Returns the path of the backup, or an empty string, and then error says why:
 * when path is empty (a map which was never saved), the folder cannot be made
 * or the map not written. warnings, if given, receives the exporter's warnings.
 *
 * See the design notes, "Manual backup".
 */
QString makeMapBackup(const Map& map, const MapView* view, const QString& path, const FileFormat* format,
                      const OpenBackupOptions& options, const QDateTime& now,
                      QString* error = nullptr, std::vector<QString>* warnings = nullptr);

/** Returns the content of a backup, decompressed when its name ends in ".gz". */
QByteArray readBackup(const QString& backup_path, bool* ok = nullptr);

/** Returns the gzip form of the data. */
QByteArray gzipCompress(const QByteArray& data);

/** Returns the data of a gzip stream, or sets ok to false. */
QByteArray gzipDecompress(const QByteArray& data, bool* ok = nullptr);


}  // namespace OpenOrienteering

#endif
