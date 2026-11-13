/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "find_replace_text_dialog.h"

#include <Qt>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "gui/map/map_editor.h"


namespace OpenOrienteering {

FindReplaceTextDialog::FindReplaceTextDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Find and replace text"));
	setModal(false);
	setAttribute(Qt::WA_DeleteOnClose);
	
	find_edit = new QLineEdit();
	replace_edit = new QLineEdit();
	case_check = new QCheckBox(tr("Case sensitive"));
	word_check = new QCheckBox(tr("Whole word"));
	all_button = new QRadioButton(tr("All text objects"));
	all_button->setChecked(true);
	auto* symbols_button = new QRadioButton(tr("Text objects of the selected symbols"));
	
	auto* buttons = new QDialogButtonBox();
	auto* find_button = buttons->addButton(tr("Find next"), QDialogButtonBox::ActionRole);
	auto* replace_button = buttons->addButton(tr("Replace"), QDialogButtonBox::ActionRole);
	auto* replace_all_button = buttons->addButton(tr("Replace all"), QDialogButtonBox::ActionRole);
	auto* delete_button = buttons->addButton(tr("Delete object"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	find_button->setDefault(true);
	connect(find_button, &QPushButton::clicked, this, &FindReplaceTextDialog::findNext);
	connect(replace_button, &QPushButton::clicked, this, &FindReplaceTextDialog::replace);
	connect(replace_all_button, &QPushButton::clicked, this, &FindReplaceTextDialog::replaceAll);
	connect(delete_button, &QPushButton::clicked, this, &FindReplaceTextDialog::deleteObjects);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Find:"), find_edit);
	form->addRow(tr("Replace with:"), replace_edit);
	form->addRow(QString{}, case_check);
	form->addRow(QString{}, word_check);
	form->addRow(tr("Scope:"), all_button);
	form->addRow(QString{}, symbols_button);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

FindReplaceTextDialog::~FindReplaceTextDialog() = default;

ops::ReplaceTextParams FindReplaceTextDialog::params() const
{
	ops::ReplaceTextParams params;
	params.find = find_edit->text();
	params.replacement = replace_edit->text();
	params.case_sensitive = case_check->isChecked();
	params.whole_word = word_check->isChecked();
	return params;
}

bool FindReplaceTextDialog::selectedSymbolsOnly() const
{
	return !all_button->isChecked();
}

void FindReplaceTextDialog::findNext()
{
	editor->findNextText(params(), selectedSymbolsOnly());
}

void FindReplaceTextDialog::replace()
{
	editor->replaceText(params(), selectedSymbolsOnly(), true);
	editor->findNextText(params(), selectedSymbolsOnly());
}

void FindReplaceTextDialog::replaceAll()
{
	editor->replaceText(params(), selectedSymbolsOnly(), false);
}

void FindReplaceTextDialog::deleteObjects()
{
	editor->deleteMatchingText(params(), selectedSymbolsOnly(), true);
}


}  // namespace OpenOrienteering
