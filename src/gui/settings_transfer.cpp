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


#include "settings_transfer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QSettings>
#include <QVariant>


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::SettingsTransfer", text);
}

bool isTransferred(const QString& key)
{
	return !key.startsWith(SettingsTransfer::markerGroup() + QLatin1Char('/'))
	       && !SettingsTransfer::excludedKeys().contains(key);
}

}  // namespace


QString SettingsTransfer::markerGroup()
{
	return QStringLiteral("FOOM-Settings");
}

QStringList SettingsTransfer::excludedKeys()
{
	// The crash blocker of MainWindow::openPath.
	return { QStringLiteral("open_in_progress") };
}

int SettingsTransfer::exportTo(const QSettings& settings, const QString& path, QString* error)
{
	if (QFileInfo::exists(path) && !QFile::remove(path))
	{
		if (error)
			*error = tr("Cannot replace %1.").arg(QDir::toNativeSeparators(path));
		return -1;
	}

	auto count = 0;
	{
		QSettings file(path, QSettings::IniFormat);
		file.setValue(markerGroup() + QStringLiteral("/version"), 1);
		file.setValue(markerGroup() + QStringLiteral("/application"), QCoreApplication::applicationName());
		for (auto const& key : settings.allKeys())
		{
			if (!isTransferred(key))
				continue;
			file.setValue(key, settings.value(key));
			++count;
		}
		file.sync();
		if (file.status() != QSettings::NoError)
		{
			if (error)
				*error = tr("Cannot write %1.").arg(QDir::toNativeSeparators(path));
			return -1;
		}
	}
	return count;
}

int SettingsTransfer::importFrom(const QString& path, QSettings& settings, QString* error)
{
	if (!QFileInfo(path).isFile())
	{
		if (error)
			*error = tr("%1 does not exist.").arg(QDir::toNativeSeparators(path));
		return -1;
	}
	QSettings file(path, QSettings::IniFormat);
	if (file.status() != QSettings::NoError || !file.contains(markerGroup() + QStringLiteral("/version")))
	{
		if (error)
			*error = tr("%1 is not a settings file.").arg(QDir::toNativeSeparators(path));
		return -1;
	}

	auto count = 0;
	for (auto const& key : file.allKeys())
	{
		if (!isTransferred(key))
			continue;
		settings.setValue(key, file.value(key));
		++count;
	}
	settings.sync();
	return count;
}


}  // namespace OpenOrienteering
