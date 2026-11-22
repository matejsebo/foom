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
#include <QAction>

#include "global.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/open_backup.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/settings_transfer.h"
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

/** Writes an empty map to the path in FOOM's format. */
bool writeEmptyMap(const QString& path)
{
	Map map;
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

/** Adds a point symbol and one point object to the map. */
void addPoint(Map& map)
{
	auto* symbol = new PointSymbol();
	map.addSymbol(symbol, map.getNumSymbols());
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoord(10, 10));
	map.addObject(object);
}

/** Returns the number of objects of the map in a backup, or -1 when it does not load. */
int objectsInBackup(const QString& backup_path)
{
	bool ok = false;
	auto const data = readBackup(backup_path, &ok);
	if (!ok)
		return -1;
	QTemporaryDir dir;
	auto name = QFileInfo(backup_path).fileName();
	if (name.endsWith(QLatin1String(".gz")))
		name.chop(3);
	auto const path = dir.filePath(name);
	if (!writeFile(path, data))
		return -1;
	Map map;
	return map.loadFrom(path) ? map.getNumObjects() : -1;
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

	
	// ### Manual backup ###
	
	void manualBackup()
	{
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("forest.foom"));
		QVERIFY(writeEmptyMap(path));
		auto const on_disk = readFile(path);
		
		Map map;
		QVERIFY(map.loadFrom(path));
		addPoint(map);
		auto const* format = FileFormats.findFormat("XML");
		QVERIFY(format);
		
		// The map as it is, not the file; the file stays.
		OpenBackupOptions options;  // off: the manual backup does not care
		QString error;
		auto const backup = makeMapBackup(map, nullptr, path, format, options, at(10, 15, 0), &error);
		QVERIFY2(!backup.isEmpty(), qPrintable(error));
		QCOMPARE(backup, QDir(openBackupFolder(path)).filePath(QStringLiteral("forest_20261121-101500.foom")));
		QCOMPARE(objectsInBackup(backup), 1);
		QCOMPARE(readFile(path), on_disk);
		{
			Map disk;
			QVERIFY(disk.loadFrom(path));
			QCOMPARE(disk.getNumObjects(), 0);
		}
		
		// Compressed.
		options.compress = true;
		auto const compressed = makeMapBackup(map, nullptr, path, format, options, at(10, 15, 0), &error);
		QCOMPARE(QFileInfo(compressed).fileName(), QStringLiteral("forest_20261121-101500.foom.gz"));
		QVERIFY(readFile(compressed).startsWith("\x1f\x8b"));
		QCOMPARE(objectsInBackup(compressed), 1);
		
		// One series with the backups on open: keeping 2 leaves the two newest manual ones.
		QDir(openBackupFolder(path)).removeRecursively();
		options = {};
		options.enabled = true;
		options.keep = 2;
		QVERIFY(!makeOpenBackup(path, options, at(11, 0, 0)).isEmpty());
		auto const first = makeMapBackup(map, nullptr, path, format, options, at(12, 0, 0));
		auto const second = makeMapBackup(map, nullptr, path, format, options, at(13, 0, 0));
		QCOMPARE(openBackups(path), (QStringList{ first, second }));
		
		// No file: no backup, no folder, and a message.
		QTemporaryDir other;
		QVERIFY(makeMapBackup(map, nullptr, QString(), format, options, at(14, 0, 0), &error).isEmpty());
		QVERIFY(!error.isEmpty());
		QVERIFY(QDir(other.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
	}
	
	void manualBackupFormats()
	{
		QTemporaryDir dir;
		Map map;
		addPoint(map);
		OpenBackupOptions options;
		QString error;
		
		// The format of the file: OCD.
		auto const ocd_path = dir.filePath(QStringLiteral("forest.ocd"));
		auto const* ocd = FileFormats.findFormatForFilename(ocd_path, &FileFormat::supportsFileSave);
		QVERIFY(ocd);
		auto const ocd_backup = makeMapBackup(map, nullptr, ocd_path, ocd, options, at(10, 15, 0), &error);
		QVERIFY2(!ocd_backup.isEmpty(), qPrintable(error));
		QCOMPARE(QFileInfo(ocd_backup).fileName(), QStringLiteral("forest_20261121-101500.ocd"));
		QCOMPARE(objectsInBackup(ocd_backup), 1);
		
		// No format which saves: FOOM's own, named .foom.
		auto const gpx_path = dir.filePath(QStringLiteral("forest.gpx"));
		auto const native = makeMapBackup(map, nullptr, gpx_path, nullptr, options, at(10, 15, 0), &error);
		QVERIFY2(!native.isEmpty(), qPrintable(error));
		QCOMPARE(QFileInfo(native).fileName(), QStringLiteral("forest_20261121-101500.foom"));
		QCOMPARE(objectsInBackup(native), 1);
	}
	
	void manualBackupWindow()
	{
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("edited.foom"));
		QVERIFY(writeEmptyMap(path));
		QSettings().remove(QStringLiteral("Backup"));  // backup on open off, keep 10
		
		auto* window = new MainWindow();
		window->setAttribute(Qt::WA_DontShowOnScreen);  // never on the screen
		QVERIFY(window->openPath(path));
		QVERIFY(openBackups(path).isEmpty());
		
		// In the File menu, after "Save as...".
		auto* action = window->findChild<QAction*>(QStringLiteral("file-backup"));
		QVERIFY(action);
		QVERIFY(action->isEnabled());
		
		auto* editor = qobject_cast<MapEditorController*>(window->getController());
		QVERIFY(editor);
		auto* map = editor->getMap();
		QVERIFY(map);
		addPoint(*map);
		map->setHasUnsavedChanges(true);
		QString error;
		auto const backup = window->makeManualBackup(&error);
		QVERIFY2(!backup.isEmpty(), qPrintable(error));
		QCOMPARE(openBackups(path), QStringList{ backup });
		QCOMPARE(objectsInBackup(backup), 1);
		QVERIFY(map->hasUnsavedChanges());
		{
			Map disk;
			QVERIFY(disk.loadFrom(path));
			QCOMPARE(disk.getNumObjects(), 0);
		}
		map->setHasUnsavedChanges(false);  // no question on closing
		window->close();
		window->deleteLater();
		
		// A new map which was never saved.
		auto* fresh = new MainWindow();
		fresh->setAttribute(Qt::WA_DontShowOnScreen);
		fresh->setController(new MapEditorController(MapEditorController::MapEditor, new Map()), QString(), nullptr);
		QVERIFY(fresh->makeManualBackup(&error).isEmpty());
		QVERIFY(!error.isEmpty());
		fresh->close();
		fresh->deleteLater();
	}
	
	
	// ### Settings transfer ###
	
	void settingsTransfer()
	{
		QTemporaryDir dir;
		QSettings from(dir.filePath(QStringLiteral("from.ini")), QSettings::IniFormat);
		from.setValue(QStringLiteral("MapDisplay/antialiasing"), false);
		from.setValue(QStringLiteral("MapEditor/click_tolerance"), 7);
		from.setValue(QStringLiteral("General/language"), QStringLiteral("de"));
		from.setValue(QStringLiteral("Shortcuts/drawpath"), QStringLiteral("Ctrl+Shift+P"));
		from.setValue(QStringLiteral("recentFileList"), QStringList{ QStringLiteral("a.foom"), QStringLiteral("b.foom") });
		from.setValue(QStringLiteral("MainWindow/state"), QByteArray("\x00\x01\xff", 3));
		from.setValue(QStringLiteral("open_in_progress"), QStringLiteral("crashing.foom"));
		from.sync();
		
		auto const path = dir.filePath(QStringLiteral("exported.ini"));
		QString error;
		QCOMPARE(SettingsTransfer::exportTo(from, path, &error), 6);  // all but the crash blocker
		
		// Into empty settings: the same values and types, no marker, no crash blocker.
		QSettings to(dir.filePath(QStringLiteral("to.ini")), QSettings::IniFormat);
		QCOMPARE(SettingsTransfer::importFrom(path, to, &error), 6);
		QCOMPARE(to.value(QStringLiteral("MapDisplay/antialiasing")).toBool(), false);
		QCOMPARE(to.value(QStringLiteral("MapEditor/click_tolerance")).toInt(), 7);
		QCOMPARE(to.value(QStringLiteral("General/language")).toString(), QStringLiteral("de"));
		QCOMPARE(to.value(QStringLiteral("Shortcuts/drawpath")).toString(), QStringLiteral("Ctrl+Shift+P"));
		QCOMPARE(to.value(QStringLiteral("recentFileList")).toStringList(), (QStringList{ QStringLiteral("a.foom"), QStringLiteral("b.foom") }));
		QCOMPARE(to.value(QStringLiteral("MainWindow/state")).toByteArray(), QByteArray("\x00\x01\xff", 3));
		QVERIFY(!to.contains(QStringLiteral("open_in_progress")));
		QVERIFY(to.childGroups().indexOf(SettingsTransfer::markerGroup()) < 0);
		
		// Into settings with values: those in the file replace, the others stay.
		QSettings other(dir.filePath(QStringLiteral("other.ini")), QSettings::IniFormat);
		other.setValue(QStringLiteral("MapEditor/click_tolerance"), 3);
		other.setValue(QStringLiteral("Only/here"), 1);
		QCOMPARE(SettingsTransfer::importFrom(path, other, &error), 6);
		QCOMPARE(other.value(QStringLiteral("MapEditor/click_tolerance")).toInt(), 7);
		QCOMPARE(other.value(QStringLiteral("Only/here")).toInt(), 1);
		
		// Exporting again replaces the file.
		from.remove(QStringLiteral("Shortcuts"));
		QCOMPARE(SettingsTransfer::exportTo(from, path, &error), 5);
		
		// Not a settings file: refused, nothing changed.
		auto const plain = dir.filePath(QStringLiteral("plain.ini"));
		QVERIFY(writeFile(plain, "[MapEditor]\nclick_tolerance=99\n"));
		QCOMPARE(SettingsTransfer::importFrom(plain, other, &error), -1);
		QVERIFY(!error.isEmpty());
		QCOMPARE(other.value(QStringLiteral("MapEditor/click_tolerance")).toInt(), 7);
		QCOMPARE(SettingsTransfer::importFrom(dir.filePath(QStringLiteral("missing.ini")), other, &error), -1);
	}
	
	void settingsTransferPage()
	{
		// The page exports what it shows, and shows what it imported.
		QTemporaryDir dir;
		QSettings().remove(QStringLiteral("Backup"));
		BackupSettingsPage page;
		auto checks = page.findChildren<QCheckBox*>();
		checks[0]->setChecked(true);
		auto const path = dir.filePath(QStringLiteral("all.ini"));
		QString error;
		QVERIFY(page.exportSettings(path, &error) > 0);
		QSettings exported(path, QSettings::IniFormat);
		QCOMPARE(exported.value(QStringLiteral("Backup/OnOpen")).toBool(), true);
		
		QSettings().setValue(QStringLiteral("Backup/OnOpen"), false);
		page.reset();
		QVERIFY(!checks[0]->isChecked());
		QVERIFY(page.importSettings(path, &error) > 0);
		QVERIFY(checks[0]->isChecked());
		QCOMPARE(QSettings().value(QStringLiteral("Backup/OnOpen")).toBool(), true);
		QVERIFY(page.importSettings(dir.filePath(QStringLiteral("missing.ini")), &error) < 0);
		QSettings().remove(QStringLiteral("Backup"));
		
		// The test settings are a file in the test folder, not FOOM's settings.
		QVERIFY(QSettings().fileName().startsWith(settings_dir.path()));
	}

private:
	QTemporaryDir settings_dir;
};


QTEST_MAIN(SettingsBackupTest)
#include "settings_backup_t.moc"
