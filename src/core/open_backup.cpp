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


#include "open_backup.h"

#include <algorithm>
#include <vector>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QVariant>

#include <zlib.h>


namespace OpenOrienteering {

namespace {

const QString on_open_key = QStringLiteral("Backup/OnOpen");
const QString keep_key = QStringLiteral("Backup/Keep");
const QString compress_key = QStringLiteral("Backup/Compress");

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::OpenBackup", text);
}

/** The pattern of the backup names of a file, with the timestamp and counter as the sort key. */
QRegularExpression backupPattern(const QFileInfo& info)
{
	QString const suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + QRegularExpression::escape(info.suffix());
	return QRegularExpression(QLatin1Char('^') + QRegularExpression::escape(info.completeBaseName())
	                          + QStringLiteral("_(\\d{8}-\\d{6})(?:-(\\d+))?") + suffix + QStringLiteral("(?:\\.gz)?$"),
	                          QRegularExpression::CaseInsensitiveOption);
}

}  // namespace


OpenBackupOptions OpenBackupOptions::load(const QSettings& settings)
{
	OpenBackupOptions options;
	options.enabled = settings.value(on_open_key, false).toBool();
	options.keep = std::max(1, settings.value(keep_key, default_keep).toInt());
	options.compress = settings.value(compress_key, false).toBool();
	return options;
}

void OpenBackupOptions::store(QSettings& settings) const
{
	settings.setValue(on_open_key, enabled);
	settings.setValue(keep_key, keep);
	settings.setValue(compress_key, compress);
}


QString openBackupFolder(const QString& path)
{
	return QFileInfo(path).absoluteDir().filePath(QStringLiteral("backup"));
}

QStringList openBackups(const QString& path)
{
	auto const info = QFileInfo(path);
	auto const pattern = backupPattern(info);
	struct Entry { QString stamp; int counter; QString path; };
	std::vector<Entry> entries;
	QDir folder(openBackupFolder(path));
	for (auto const& name : folder.entryList(QDir::Files))
	{
		auto const match = pattern.match(name);
		if (match.hasMatch())
			entries.push_back({ match.captured(1), match.captured(2).isEmpty() ? 1 : match.captured(2).toInt(), folder.filePath(name) });
	}
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		return a.stamp != b.stamp ? a.stamp < b.stamp : a.counter < b.counter;
	});
	QStringList result;
	for (auto const& entry : entries)
		result.push_back(entry.path);
	return result;
}

QString makeOpenBackup(const QString& path, const OpenBackupOptions& options, const QDateTime& now, QString* error)
{
	if (error)
		error->clear();
	if (!options.enabled)
		return {};
	// Only local files: not resources (":/..."), search paths ("data:/...") or URIs.
	// A drive letter ("C:/...") has a single letter before the colon.
	static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
	if (path.isEmpty() || path.startsWith(QLatin1Char(':')) || scheme.match(path).hasMatch() || !QFileInfo(path).isFile())
		return {};

	auto const info = QFileInfo(path);
	QDir folder(openBackupFolder(path));
	if (!folder.exists() && !QDir().mkpath(folder.path()))
	{
		if (error)
			*error = tr("Cannot create the folder %1.").arg(QDir::toNativeSeparators(folder.path()));
		return {};
	}

	QFile source(path);
	if (!source.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = source.errorString();
		return {};
	}
	auto data = source.readAll();
	source.close();
	if (options.compress)
		data = gzipCompress(data);

	auto const stamp = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
	QString const suffix = (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix())
	                    + (options.compress ? QStringLiteral(".gz") : QString());
	QString name = info.completeBaseName() + QLatin1Char('_') + stamp + suffix;
	for (int counter = 2; QFileInfo::exists(folder.filePath(name)); ++counter)
		name = info.completeBaseName() + QLatin1Char('_') + stamp + QLatin1Char('-') + QString::number(counter) + suffix;

	QSaveFile target(folder.filePath(name));
	if (!target.open(QIODevice::WriteOnly) || target.write(data) != data.size() || !target.commit())
	{
		if (error)
			*error = target.errorString();
		return {};
	}

	auto backups = openBackups(path);
	auto const keep = std::max(1, options.keep);
	while (backups.size() > keep)
		QFile::remove(backups.takeFirst());

	return folder.filePath(name);
}

QByteArray readBackup(const QString& backup_path, bool* ok)
{
	QFile file(backup_path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (ok)
			*ok = false;
		return {};
	}
	auto data = file.readAll();
	if (backup_path.endsWith(QLatin1String(".gz"), Qt::CaseInsensitive))
		return gzipDecompress(data, ok);
	if (ok)
		*ok = true;
	return data;
}


QByteArray gzipCompress(const QByteArray& data)
{
	z_stream stream = {};
	// 15 + 16: the largest window, with a gzip header and trailer.
	if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return {};
	QByteArray result;
	result.resize(int(deflateBound(&stream, uLong(data.size()))));
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
	stream.avail_in = uInt(data.size());
	stream.next_out = reinterpret_cast<Bytef*>(result.data());
	stream.avail_out = uInt(result.size());
	auto const status = deflate(&stream, Z_FINISH);
	result.resize(int(stream.total_out));
	deflateEnd(&stream);
	return status == Z_STREAM_END ? result : QByteArray();
}

QByteArray gzipDecompress(const QByteArray& data, bool* ok)
{
	z_stream stream = {};
	if (inflateInit2(&stream, 15 + 16) != Z_OK)
	{
		if (ok)
			*ok = false;
		return {};
	}
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
	stream.avail_in = uInt(data.size());
	QByteArray result;
	char buffer[65536];
	int status = Z_OK;
	while (status == Z_OK)
	{
		stream.next_out = reinterpret_cast<Bytef*>(buffer);
		stream.avail_out = sizeof(buffer);
		status = inflate(&stream, Z_NO_FLUSH);
		if (status == Z_OK || status == Z_STREAM_END)
			result.append(buffer, int(sizeof(buffer) - stream.avail_out));
		if (status == Z_OK && stream.avail_in == 0 && stream.avail_out != 0)
			status = Z_DATA_ERROR;  // truncated
	}
	inflateEnd(&stream);
	if (ok)
		*ok = status == Z_STREAM_END;
	return status == Z_STREAM_END ? result : QByteArray();
}


}  // namespace OpenOrienteering
