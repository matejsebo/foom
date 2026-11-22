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


#include <QtTest>
#include <QByteArray>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSettings>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>

#include "global.h"
#include "core/map.h"
#include "core/open_backup.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/main_window.h"
#include "gui/widgets/backup_settings_page.h"

using namespace OpenOrienteering;


namespace {

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QDateTime at(int hour, int minute, int second)
{
	return QDateTime(QDate(2026, 11, 21), QTime(hour, minute, second));
}

}  // namespace


/**
 * Tests the backup on open and the settings transfer.
 *
 * See the design notes. The settings are an ini file
 * of a test organization in a temporary folder, never FOOM's own.
 */
class SettingsBackupTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("FOOM-Tests"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
		Q_INIT_RESOURCE(resources);
		doStaticInitializations();
		QVERIFY(QSettings().fileName().startsWith(settings_dir.path()));
	}


	// ### Backup on open ###

	void backupOptions()
	{
		QTemporaryDir dir;
		QSettings settings(dir.filePath(QStringLiteral("options.ini")), QSettings::IniFormat);
		auto options = OpenBackupOptions::load(settings);
		QCOMPARE(options.enabled, false);
		QCOMPARE(options.keep, 10);
		QCOMPARE(options.compress, false);
		options.enabled = true;
		options.keep = 3;
		options.compress = true;
		options.store(settings);
		QCOMPARE(settings.value(QStringLiteral("Backup/OnOpen")).toBool(), true);
		auto const loaded = OpenBackupOptions::load(settings);
		QCOMPARE(loaded.enabled, true);
		QCOMPARE(loaded.keep, 3);
		QCOMPARE(loaded.compress, true);
		settings.setValue(QStringLiteral("Backup/Keep"), 0);
		QCOMPARE(OpenBackupOptions::load(settings).keep, 1);
	}

	void backupOnOpen()
	{
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("forest.foom"));
		QVERIFY(writeFile(path, "first"));

		// Off: nothing, not even the folder.
		OpenBackupOptions options;
		QString error;
		QVERIFY(makeOpenBackup(path, options, at(10, 15, 0), &error).isEmpty());
		QVERIFY(error.isEmpty());
		QVERIFY(!QFileInfo::exists(openBackupFolder(path)));

		// On: a copy named with the time, in "backup" beside the file.
		options.enabled = true;
		auto const backup = makeOpenBackup(path, options, at(10, 15, 0), &error);
		QCOMPARE(backup, QDir(dir.filePath(QStringLiteral("backup"))).filePath(QStringLiteral("forest_20261121-101500.foom")));
		QCOMPARE(readFile(backup), QByteArray("first"));

		// Twice in one second: a counter.
		QCOMPARE(QFileInfo(makeOpenBackup(path, options, at(10, 15, 0))).fileName(), QStringLiteral("forest_20261121-101500-2.foom"));

		// Keep the newest 3, whatever the counters; other files' backups stay.
		QVERIFY(writeFile(QDir(openBackupFolder(path)).filePath(QStringLiteral("other_20200101-000000.foom")), "x"));
		QVERIFY(writeFile(QDir(openBackupFolder(path)).filePath(QStringLiteral("forest_notes.txt")), "x"));
		options.keep = 3;
		QVERIFY(writeFile(path, "second"));
		makeOpenBackup(path, options, at(11, 0, 0));
		QVERIFY(writeFile(path, "third"));
		makeOpenBackup(path, options, at(12, 0, 0));
		auto const backups = openBackups(path);
		QCOMPARE(backups.size(), 3);
		QCOMPARE(QFileInfo(backups[0]).fileName(), QStringLiteral("forest_20261121-101500-2.foom"));
		QCOMPARE(QFileInfo(backups[1]).fileName(), QStringLiteral("forest_20261121-110000.foom"));
		QCOMPARE(QFileInfo(backups[2]).fileName(), QStringLiteral("forest_20261121-120000.foom"));
		QCOMPARE(readFile(backups[2]), QByteArray("third"));
		QVERIFY(QFileInfo::exists(QDir(openBackupFolder(path)).filePath(QStringLiteral("other_20200101-000000.foom"))));
		QVERIFY(QFileInfo::exists(QDir(openBackupFolder(path)).filePath(QStringLiteral("forest_notes.txt"))));

		// Compressed: gzip, named .gz, read back to the original.
		options.compress = true;
		auto const compressed = makeOpenBackup(path, options, at(13, 0, 0));
		QCOMPARE(QFileInfo(compressed).fileName(), QStringLiteral("forest_20261121-130000.foom.gz"));
		auto const raw = readFile(compressed);
		QVERIFY(raw.startsWith("\x1f\x8b"));
		bool ok = false;
		QCOMPARE(readBackup(compressed, &ok), QByteArray("third"));
		QVERIFY(ok);
		QCOMPARE(openBackups(path).size(), 3);  // compressed ones count with the others

		// Not local files: nothing, no error.
		QVERIFY(makeOpenBackup(QStringLiteral(":/images/mapper.png"), options, at(14, 0, 0), &error).isEmpty());
		QVERIFY(error.isEmpty());
		QVERIFY(makeOpenBackup(QStringLiteral("data:/examples/forest sample.omap"), options, at(14, 0, 0), &error).isEmpty());
		QVERIFY(error.isEmpty());
		QVERIFY(makeOpenBackup(dir.filePath(QStringLiteral("missing.foom")), options, at(14, 0, 0), &error).isEmpty());
	}

	void gzip()
	{
		QByteArray data;
		for (int i = 0; i < 100000; ++i)
			data.append(char(i % 251));
		auto const compressed = gzipCompress(data);
		QVERIFY(!compressed.isEmpty());
		bool ok = false;
		QCOMPARE(gzipDecompress(compressed, &ok), data);
		QVERIFY(ok);
		gzipDecompress(compressed.left(compressed.size() / 2), &ok);
		QVERIFY(!ok);
		gzipDecompress("not gzip", &ok);
		QVERIFY(!ok);
	}

	void backupSettingsPage()
	{
		QSettings().remove(QStringLiteral("Backup"));
		BackupSettingsPage page;
		QCOMPARE(page.title(), QStringLiteral("Backup"));
		auto checks = page.findChildren<QCheckBox*>();
		QCOMPARE(checks.size(), 2);
		auto* keep = page.findChild<QSpinBox*>();
		QVERIFY(keep);
		QCOMPARE(keep->value(), 10);
		QVERIFY(!keep->isEnabled());  // until the backup is on
		checks[0]->setChecked(true);
		QVERIFY(keep->isEnabled());
		keep->setValue(4);
		checks[1]->setChecked(true);
		page.apply();
		auto const options = OpenBackupOptions::load(QSettings());
		QVERIFY(options.enabled);
		QCOMPARE(options.keep, 4);
		QVERIFY(options.compress);
		QSettings().remove(QStringLiteral("Backup"));
		page.reset();
		QVERIFY(!checks[0]->isChecked());
	}
	
	void openingMakesBackup()
	{
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("opened.foom"));
		{
			Map map;
			XMLFileFormat format;
			auto exporter = format.makeExporter(path, &map, nullptr);
			QVERIFY(exporter && exporter->doExport());
		}

		QSettings().setValue(QStringLiteral("Backup/OnOpen"), true);
		auto* window = new MainWindow();
		window->setAttribute(Qt::WA_DontShowOnScreen);  // never on the screen
		QVERIFY(window->openPath(path));
		auto const backups = openBackups(path);
		QCOMPARE(backups.size(), 1);
		QCOMPARE(readFile(backups.front()), readFile(path));
		window->close();
		window->deleteLater();
		QSettings().remove(QStringLiteral("Backup/OnOpen"));
	}

private:
	QTemporaryDir settings_dir;
};


QTEST_MAIN(SettingsBackupTest)
#include "settings_backup_t.moc"
