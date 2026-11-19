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


#include "bookmark_dialog.h"

#include <cstddef>
#include <vector>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include "core/bookmarks.h"
#include "core/map.h"
#include "core/map_view.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

BookmarkDialog::BookmarkDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Bookmarks"));
	list = new QListWidget();
	goto_button = new QPushButton(tr("Go to"));
	rename_button = new QPushButton(tr("Rename..."));
	update_button = new QPushButton(tr("Update to current view"));
	delete_button = new QPushButton(tr("Delete"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	
	auto* side = new QVBoxLayout();
	side->addWidget(goto_button);
	side->addWidget(rename_button);
	side->addWidget(update_button);
	side->addWidget(delete_button);
	side->addStretch();
	auto* row = new QHBoxLayout();
	row->addWidget(list);
	row->addLayout(side);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(row);
	layout->addWidget(buttons);
	
	connect(goto_button, &QPushButton::clicked, this, &BookmarkDialog::goTo);
	connect(list, &QListWidget::itemDoubleClicked, this, &BookmarkDialog::goTo);
	connect(rename_button, &QPushButton::clicked, this, &BookmarkDialog::rename);
	connect(update_button, &QPushButton::clicked, this, &BookmarkDialog::updateToCurrentView);
	connect(delete_button, &QPushButton::clicked, this, &BookmarkDialog::remove);
	connect(list, &QListWidget::currentRowChanged, this, &BookmarkDialog::updateButtons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	fillList(0);
}

BookmarkDialog::~BookmarkDialog() = default;

void BookmarkDialog::fillList(int select_row)
{
	list->clear();
	for (auto const& bookmark : editor->getMap()->bookmarks())
		list->addItem(bookmark.name);
	if (list->count() > 0)
		list->setCurrentRow(qBound(0, select_row, list->count() - 1));
	updateButtons();
}

void BookmarkDialog::updateButtons()
{
	auto const has_row = list->currentRow() >= 0;
	for (auto* button : { goto_button, rename_button, update_button, delete_button })
		button->setEnabled(has_row);
}

void BookmarkDialog::goTo()
{
	if (list->currentRow() >= 0)
		editor->jumpToBookmark(list->currentRow());
}

void BookmarkDialog::rename()
{
	auto const row = list->currentRow();
	auto bookmarks = editor->getMap()->bookmarks();
	if (row < 0 || std::size_t(row) >= bookmarks.size())
		return;
	bool ok = false;
	auto const name = QInputDialog::getText(this, tr("Rename bookmark"), tr("Name:"), QLineEdit::Normal, bookmarks[row].name, &ok);
	if (!ok || name.isEmpty())
		return;
	bookmarks[row].name = name;
	editor->getMap()->setBookmarks(bookmarks);
	editor->getMap()->setHasUnsavedChanges(true);
	fillList(row);
}

void BookmarkDialog::updateToCurrentView()
{
	auto const row = list->currentRow();
	auto bookmarks = editor->getMap()->bookmarks();
	if (row < 0 || std::size_t(row) >= bookmarks.size())
		return;
	auto const* view = editor->getMainWidget()->getMapView();
	bookmarks[row].center = view->center();
	bookmarks[row].zoom = view->getZoom();
	editor->getMap()->setBookmarks(bookmarks);
	editor->getMap()->setHasUnsavedChanges(true);
}

void BookmarkDialog::remove()
{
	auto const row = list->currentRow();
	auto bookmarks = editor->getMap()->bookmarks();
	if (row < 0 || std::size_t(row) >= bookmarks.size())
		return;
	bookmarks.erase(begin(bookmarks) + row);
	editor->getMap()->setBookmarks(bookmarks);
	editor->getMap()->setHasUnsavedChanges(true);
	fillList(row);
}


}  // namespace OpenOrienteering
