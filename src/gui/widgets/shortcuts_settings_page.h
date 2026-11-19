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


#ifndef OPENORIENTEERING_SHORTCUTS_SETTINGS_PAGE_H
#define OPENORIENTEERING_SHORTCUTS_SETTINGS_PAGE_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include "settings_page.h"

class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The settings page which edits the keyboard shortcuts of the editor actions.
 *
 * See the design notes, "Shortcut editor".
 */
class ShortcutsSettingsPage : public SettingsPage
{
Q_OBJECT
public:
	explicit ShortcutsSettingsPage(QWidget* parent = nullptr);
	~ShortcutsSettingsPage() override;
	
	QString title() const override;
	void apply() override;
	void reset() override;

private slots:
	void selectionChanged();
	void assign();
	void reassign();
	void clearShortcut();
	void resetToDefault();
	void resetAll();
	void exportShortcuts();
	void importShortcuts();

private:
	MapEditorController* findEditor() const;
	QByteArray currentId() const;
	void fillTable();
	void setShortcut(const QByteArray& id, const QKeySequence& sequence, bool store);
	void showConflict(const QString& text, bool offer_reassign);
	
	MapEditorController* editor;
	QTableWidget* table;
	QKeySequenceEdit* sequence_edit;
	QLabel* conflict_label;
	QPushButton* assign_button;
	QPushButton* reassign_button;
	QPushButton* clear_button;
	QPushButton* default_button;
	QPushButton* reset_all_button;
	QPushButton* export_button;
	QPushButton* import_button;
};


}  // namespace OpenOrienteering

#endif
