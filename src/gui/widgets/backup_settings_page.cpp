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


#include "backup_settings_page.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLatin1String>
#include <QMessageBox>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "settings.h"
#include "core/open_backup.h"
#include "gui/file_dialog.h"
#include "gui/settings_transfer.h"


namespace OpenOrienteering {

BackupSettingsPage::BackupSettingsPage(QWidget* parent)
: SettingsPage(parent)
{
	auto* layout = new QVBoxLayout(this);

	auto* backup_group = new QGroupBox(tr("Backup on open"));
	auto* backup_layout = new QFormLayout(backup_group);
	backup_check = new QCheckBox(tr("Copy a file to the folder \"backup\" beside it when it is opened"));
	backup_layout->addRow(backup_check);
	keep_box = new QSpinBox();
	keep_box->setRange(1, 1000);
	backup_layout->addRow(tr("Backups kept per file:"), keep_box);
	compress_check = new QCheckBox(tr("Compress the backups (gzip)"));
	backup_layout->addRow(compress_check);
	layout->addWidget(backup_group);

	auto* transfer_group = new QGroupBox(tr("Settings transfer"));
	auto* transfer_layout = new QVBoxLayout(transfer_group);
	auto* transfer_label = new QLabel(tr("All settings, the shortcuts among them, as one file, for another computer or for safekeeping."));
	transfer_label->setWordWrap(true);
	transfer_layout->addWidget(transfer_label);
	auto* buttons = new QHBoxLayout();
	auto* export_button = new QPushButton(tr("Export settings..."));
	auto* import_button = new QPushButton(tr("Import settings..."));
	buttons->addWidget(export_button);
	buttons->addWidget(import_button);
	buttons->addStretch(1);
	transfer_layout->addLayout(buttons);
	layout->addWidget(transfer_group);
	connect(export_button, &QPushButton::clicked, this, &BackupSettingsPage::exportClicked);
	connect(import_button, &QPushButton::clicked, this, &BackupSettingsPage::importClicked);

	layout->addStretch(1);

	connect(backup_check, &QCheckBox::toggled, this, &BackupSettingsPage::updateWidgets);
	reset();
}

BackupSettingsPage::~BackupSettingsPage() = default;

QString BackupSettingsPage::title() const
{
	return tr("Backup");
}

void BackupSettingsPage::apply()
{
	QSettings settings;
	OpenBackupOptions options;
	options.enabled = backup_check->isChecked();
	options.keep = keep_box->value();
	options.compress = compress_check->isChecked();
	options.store(settings);
}

void BackupSettingsPage::reset()
{
	auto const options = OpenBackupOptions::load(QSettings());
	backup_check->setChecked(options.enabled);
	keep_box->setValue(options.keep);
	compress_check->setChecked(options.compress);
	updateWidgets();
}

QList<SettingsPage*> BackupSettingsPage::dialogPages()
{
	auto pages = window()->findChildren<SettingsPage*>();
	if (!pages.contains(this))
		pages.push_back(this);
	return pages;
}

int BackupSettingsPage::exportSettings(const QString& path, QString* error)
{
	// What the pages show is what is exported.
	for (auto* page : dialogPages())
		page->apply();
	QSettings settings;
	settings.sync();
	return SettingsTransfer::exportTo(settings, path, error);
}

int BackupSettingsPage::importSettings(const QString& path, QString* error)
{
	QSettings settings;
	auto const count = SettingsTransfer::importFrom(path, settings, error);
	if (count < 0)
		return count;
	Settings::getInstance().applySettings();
	for (auto* page : dialogPages())
		page->reset();
	return count;
}

void BackupSettingsPage::exportClicked()
{
	auto path = FileDialog::getSaveFileName(this, tr("Export settings"), QString(), tr("Settings (*.ini)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".ini"), Qt::CaseInsensitive))
		path += QLatin1String(".ini");
	QString error;
	if (exportSettings(path, &error) < 0)
		QMessageBox::warning(this, tr("Export settings"), error);
}

void BackupSettingsPage::importClicked()
{
	auto const path = FileDialog::getOpenFileName(this, tr("Import settings"), QString(), tr("Settings (*.ini)"));
	if (path.isEmpty())
		return;
	if (QMessageBox::question(this, tr("Import settings"),
	                          tr("The settings in the file replace the current ones. Continue?"))
	    != QMessageBox::Yes)
		return;
	QString error;
	if (importSettings(path, &error) < 0)
		QMessageBox::warning(this, tr("Import settings"), error);
	else
		QMessageBox::information(this, tr("Import settings"), tr("The settings were imported. Some take effect when the program is started again."));
}

void BackupSettingsPage::updateWidgets()
{
	keep_box->setEnabled(backup_check->isChecked());
	compress_check->setEnabled(backup_check->isChecked());
}


}  // namespace OpenOrienteering
