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
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/open_backup.h"


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

void BackupSettingsPage::updateWidgets()
{
	keep_box->setEnabled(backup_check->isChecked());
	compress_check->setEnabled(backup_check->isChecked());
}


}  // namespace OpenOrienteering
