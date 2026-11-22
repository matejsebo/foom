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

#include <QObject>
#include <QString>

#include "settings_page.h"

class QCheckBox;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The settings page of the backup on open.
 *
 * See the design notes, "Backup on open: as built".
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

private:
	void updateWidgets();

	QCheckBox* backup_check;
	QSpinBox* keep_box;
	QCheckBox* compress_check;
};


}  // namespace OpenOrienteering

#endif
