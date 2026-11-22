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

#ifndef OPENORIENTEERING_BACKUP_SETTINGS_PAGE_H
#define OPENORIENTEERING_BACKUP_SETTINGS_PAGE_H

#include <QList>
#include <QObject>
#include <QString>

#include "settings_page.h"

class QCheckBox;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The settings page of the backup on open and of the settings transfer.
 *
 * See the design notes, "Backup on open: as built"
 * and "Settings transfer: as built".
 */
class BackupSettingsPage : public SettingsPage
{
Q_OBJECT
public:
	explicit BackupSettingsPage(QWidget* parent = nullptr);
	~BackupSettingsPage() override;

	QString title() const override;
	void apply() override;
	void reset() override;
	
	/**
	 * Applies the pages of the dialog and exports all settings to the file.
	 * Returns the number of keys, or -1 with error set.
	 */
	int exportSettings(const QString& path, QString* error = nullptr);
	
	/**
	 * Imports the settings of the file and shows them again on all pages of
	 * the dialog. Returns the number of keys, or -1 with error set.
	 */
	int importSettings(const QString& path, QString* error = nullptr);

private slots:
	void exportClicked();
	void importClicked();

private:
	/** The settings pages of this page's dialog, or this page alone. */
	QList<SettingsPage*> dialogPages();
	
	void updateWidgets();

	QCheckBox* backup_check;
	QSpinBox* keep_box;
	QCheckBox* compress_check;
};


}  // namespace OpenOrienteering

#endif
