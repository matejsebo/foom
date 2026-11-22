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

// foom: the glyphs of TextObjectEditorHelper, the design notes, "Insert glyphs"

#include "text_object_editor_helper.h"

#include <QAction>
#include <QDialog>
#include <QFont>
#include <QString>
#include <QToolBar>

#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "gui/main_window.h"
#include "gui/map/insert_glyph_dialog.h"
#include "gui/map/map_editor.h"


namespace OpenOrienteering {

QToolBar* TextObjectEditorHelper::toolBar() const
{
	return alignment_toolbar;
}


void TextObjectEditorHelper::insertGlyph(const QString& glyph)
{
	if (glyph.isEmpty())
		return;
	TextObjectEditorHelper::BatchEdit transaction(this);
	replaceSelectionText(glyph);
}


void TextObjectEditorHelper::addInsertGlyphAction(QToolBar* toolbar)
{
	if (!toolbar->actions().isEmpty())
		toolbar->addSeparator();
	auto* action = toolbar->addAction(tr("Insert glyph..."));
	action->setObjectName(QStringLiteral("insert-glyph"));
	action->setToolTip(tr("Insert a character by its code"));
	connect(action, &QAction::triggered, this, &TextObjectEditorHelper::insertGlyphClicked);
}


void TextObjectEditorHelper::insertGlyphClicked()
{
	auto font = QFont();
	if (auto const* symbol = text_object->getSymbol())
	{
		if (symbol->getType() == Symbol::Text)
			font = symbol->asText()->getQFont();
	}
	InsertGlyphDialog dialog(font, editor->getWindow());
	auto const accepted = dialog.exec() == QDialog::Accepted;
	claimFocus();
	if (accepted)
		insertGlyph(dialog.glyph());
}


}  // namespace OpenOrienteering
