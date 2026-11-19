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


#include "shortcuts_settings_page.h"

#include <algorithm>

#include <Qt>
#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/widgets/shortcut_settings.h"


namespace OpenOrienteering {

ShortcutsSettingsPage::ShortcutsSettingsPage(QWidget* parent)
: SettingsPage(parent)
, editor(findEditor())
{
	auto* layout = new QVBoxLayout(this);
	table = new QTableWidget(0, 3);
	table->setHorizontalHeaderLabels({ tr("Id"), tr("Command"), tr("Shortcut") });
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->hide();
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(table);
	
	auto* edit_row = new QHBoxLayout();
	sequence_edit = new QKeySequenceEdit();
	assign_button = new QPushButton(tr("Assign"));
	reassign_button = new QPushButton(tr("Reassign"));
	reassign_button->hide();
	clear_button = new QPushButton(tr("Clear"));
	default_button = new QPushButton(tr("Reset to default"));
	edit_row->addWidget(new QLabel(tr("New shortcut:")));
	edit_row->addWidget(sequence_edit, 1);
	edit_row->addWidget(assign_button);
	edit_row->addWidget(reassign_button);
	edit_row->addWidget(clear_button);
	edit_row->addWidget(default_button);
	layout->addLayout(edit_row);
	conflict_label = new QLabel();
	conflict_label->setWordWrap(true);
	layout->addWidget(conflict_label);
	
	auto* file_row = new QHBoxLayout();
	reset_all_button = new QPushButton(tr("Reset all"));
	export_button = new QPushButton(tr("Export..."));
	import_button = new QPushButton(tr("Import..."));
	file_row->addWidget(reset_all_button);
	file_row->addStretch();
	file_row->addWidget(export_button);
	file_row->addWidget(import_button);
	layout->addLayout(file_row);
	
	connect(table, &QTableWidget::itemSelectionChanged, this, &ShortcutsSettingsPage::selectionChanged);
	connect(assign_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::assign);
	connect(sequence_edit, &QKeySequenceEdit::editingFinished, this, &ShortcutsSettingsPage::assign);
	connect(reassign_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::reassign);
	connect(clear_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::clearShortcut);
	connect(default_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::resetToDefault);
	connect(reset_all_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::resetAll);
	connect(export_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::exportShortcuts);
	connect(import_button, &QPushButton::clicked, this, &ShortcutsSettingsPage::importShortcuts);
	
	if (!editor)
	{
		conflict_label->setText(tr("Open a map to edit the shortcuts."));
		for (auto* widget : { static_cast<QWidget*>(table), static_cast<QWidget*>(sequence_edit), static_cast<QWidget*>(assign_button), static_cast<QWidget*>(clear_button), static_cast<QWidget*>(default_button), static_cast<QWidget*>(reset_all_button), static_cast<QWidget*>(export_button), static_cast<QWidget*>(import_button) })
			widget->setEnabled(false);
	}
	fillTable();
}

ShortcutsSettingsPage::~ShortcutsSettingsPage() = default;

QString ShortcutsSettingsPage::title() const
{
	return tr("Shortcuts");
}

void ShortcutsSettingsPage::apply()
{
	// Every change is stored and applied at once.
}

void ShortcutsSettingsPage::reset()
{
	fillTable();
}

MapEditorController* ShortcutsSettingsPage::findEditor() const
{
	for (auto* widget = parentWidget(); widget; widget = widget->parentWidget())
	{
		if (auto* main_window = qobject_cast<MainWindow*>(widget))
			return dynamic_cast<MapEditorController*>(main_window->getController());
	}
	return nullptr;
}

QByteArray ShortcutsSettingsPage::currentId() const
{
	auto const row = table->currentRow();
	if (row < 0 || !table->item(row, 0))
		return {};
	return table->item(row, 0)->text().toLatin1();
}

void ShortcutsSettingsPage::fillTable()
{
	auto const selected = currentId();
	table->setRowCount(0);
	if (!editor)
		return;
	auto const& actions = editor->actions();
	auto ids = actions.keys();
	std::sort(ids.begin(), ids.end());
	for (auto const& id : ids)
	{
		auto const* action = actions.value(id);
		if (id.isEmpty() || !action)
			continue;
		auto const row = table->rowCount();
		table->insertRow(row);
		auto text = action->text();
		text.remove(QLatin1Char('&'));
		auto* items = new QTableWidgetItem*[3] { new QTableWidgetItem(QString::fromLatin1(id)), new QTableWidgetItem(text), new QTableWidgetItem(action->shortcut().toString(QKeySequence::NativeText)) };
		for (int column = 0; column < 3; ++column)
		{
			items[column]->setFlags(items[column]->flags() & ~Qt::ItemIsEditable);
			table->setItem(row, column, items[column]);
		}
		delete[] items;
		if (id == selected)
			table->selectRow(row);
	}
	table->resizeColumnsToContents();
	selectionChanged();
}

void ShortcutsSettingsPage::selectionChanged()
{
	auto const id = currentId();
	auto const* action = editor && !id.isEmpty() ? editor->actions().value(id) : nullptr;
	sequence_edit->setKeySequence(action ? action->shortcut() : QKeySequence());
	showConflict(QString{}, false);
	auto const has_action = action != nullptr;
	for (auto* button : { assign_button, clear_button, default_button })
		button->setEnabled(has_action);
}

void ShortcutsSettingsPage::showConflict(const QString& text, bool offer_reassign)
{
	conflict_label->setText(text);
	reassign_button->setVisible(offer_reassign);
}

void ShortcutsSettingsPage::setShortcut(const QByteArray& id, const QKeySequence& sequence, bool store)
{
	auto* action = editor ? editor->actions().value(id) : nullptr;
	if (!action)
		return;
	action->setShortcut(sequence);
	QSettings settings;
	if (store)
		ShortcutSettings::store(settings, id, sequence);
	else
		ShortcutSettings::remove(settings, id);
	fillTable();
}

void ShortcutsSettingsPage::assign()
{
	auto const id = currentId();
	if (!editor || id.isEmpty())
		return;
	auto const sequence = sequence_edit->keySequence();
	auto* current = editor->actions().value(id);
	if (sequence.isEmpty() || !current || current->shortcut() == sequence)
		return;
	if (auto const* other = ShortcutSettings::conflict(editor->actions(), sequence, current))
	{
		auto text = other->text();
		text.remove(QLatin1Char('&'));
		showConflict(tr("%1 is used by %2.").arg(sequence.toString(QKeySequence::NativeText), text), true);
		return;
	}
	setShortcut(id, sequence, true);
}

void ShortcutsSettingsPage::reassign()
{
	auto const id = currentId();
	if (!editor || id.isEmpty())
		return;
	auto const sequence = sequence_edit->keySequence();
	auto* current = editor->actions().value(id);
	if (auto* other = ShortcutSettings::conflict(editor->actions(), sequence, current))
		setShortcut(other->objectName().toLatin1(), QKeySequence(), true);
	setShortcut(id, sequence, true);
	showConflict(QString{}, false);
}

void ShortcutsSettingsPage::clearShortcut()
{
	auto const id = currentId();
	if (!id.isEmpty())
		setShortcut(id, QKeySequence(), true);
}

void ShortcutsSettingsPage::resetToDefault()
{
	auto const id = currentId();
	if (!editor || id.isEmpty())
		return;
	setShortcut(id, editor->defaultShortcut(id), false);
}

void ShortcutsSettingsPage::resetAll()
{
	if (!editor)
		return;
	QSettings settings;
	ShortcutSettings::removeAll(settings);
	auto const& actions = editor->actions();
	for (auto it = actions.constBegin(); it != actions.constEnd(); ++it)
	{
		if (!it.key().isEmpty() && it.value())
			it.value()->setShortcut(editor->defaultShortcut(it.key()));
	}
	fillTable();
}

void ShortcutsSettingsPage::exportShortcuts()
{
	if (!editor)
		return;
	auto const path = QFileDialog::getSaveFileName(this, tr("Export shortcuts"), QString{}, tr("JSON files (*.json)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(ShortcutSettings::toJson(editor->actions())) < 0)
		QMessageBox::warning(this, tr("Error"), tr("Cannot write the file %1.").arg(path));
}

void ShortcutsSettingsPage::importShortcuts()
{
	if (!editor)
		return;
	auto const path = QFileDialog::getOpenFileName(this, tr("Import shortcuts"), QString{}, tr("JSON files (*.json)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(this, tr("Error"), tr("Cannot read the file %1.").arg(path));
		return;
	}
	QSettings settings;
	if (ShortcutSettings::fromJson(settings, file.readAll()) < 0)
	{
		QMessageBox::warning(this, tr("Error"), tr("The file %1 does not hold shortcuts.").arg(path));
		return;
	}
	ShortcutSettings::apply(settings, editor->actions());
	fillTable();
}


}  // namespace OpenOrienteering
