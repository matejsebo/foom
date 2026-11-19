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


#ifndef OPENORIENTEERING_BOOKMARK_DIALOG_H
#define OPENORIENTEERING_BOOKMARK_DIALOG_H

#include <QDialog>
#include <QObject>

class QListWidget;
class QPushButton;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The dialog of the bookmark-manage command: go to, rename, update, delete.
 *
 * See the design notes, "Bookmarks and view history".
 */
class BookmarkDialog : public QDialog
{
Q_OBJECT
public:
	BookmarkDialog(MapEditorController* editor, QWidget* parent);
	~BookmarkDialog() override;

private slots:
	void goTo();
	void rename();
	void updateToCurrentView();
	void remove();
	void updateButtons();

private:
	void fillList(int select_row);
	
	MapEditorController* editor;
	QListWidget* list;
	QPushButton* goto_button;
	QPushButton* rename_button;
	QPushButton* update_button;
	QPushButton* delete_button;
};


}  // namespace OpenOrienteering

#endif
