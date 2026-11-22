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

#include "insert_glyph_dialog.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/glyph_code.h"


namespace OpenOrienteering {

QString InsertGlyphDialog::specialCharacters()
{
	// FOOM's choice; the design notes, "Insert glyphs"
	static const char16_t codes[] = {
	    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B5, 0x00D7, 0x00F7, 0x2013, 0x2014,
	    0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2022, 0x00B7, 0x20AC, 0x00A3,
	    0x00A9, 0x00AE, 0x2122, 0x00A7, 0x00B6, 0x2190, 0x2191, 0x2192, 0x2193,
	    0x2248, 0x2260, 0x2264, 0x2265, 0x221E, 0x00BD, 0x00BC, 0x00BE, 0x2030,
	};
	QString result;
	for (auto code : codes)
		result.append(QChar(code));
	return result;
}


InsertGlyphDialog::InsertGlyphDialog(const QFont& font, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Insert Glyph"));
	
	code_edit = new QLineEdit();
	code_edit->setPlaceholderText(tr("e.g. 00E9"));
	
	preview = new QLabel();
	auto preview_font = font;
	preview_font.setPointSize(36);
	preview->setFont(preview_font);
	preview->setAlignment(Qt::AlignCenter);
	preview->setMinimumSize(80, 80);
	
	message = new QLabel();
	message->setWordWrap(true);
	
	auto* grid = new QGridLayout();
	grid->setSpacing(2);
	auto const specials = specialCharacters();
	for (int i = 0; i < specials.size(); ++i)
	{
		auto* button = new QToolButton();
		button->setText(specials.mid(i, 1));
		button->setFont(font);
		button->setAutoRaise(true);
		auto const code = QString::number(specials.at(i).unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0'));
		button->setToolTip(QStringLiteral("U+") + code);
		connect(button, &QAbstractButton::clicked, this, [this, code]() { code_edit->setText(code); });
		grid->addWidget(button, i / 9, i % 9);
	}
	
	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Character code (hex):"), code_edit);
	form->addRow(tr("Character:"), preview);
	form->addRow(message);
	form->addRow(tr("Special characters:"), grid);
	
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
	
	connect(code_edit, &QLineEdit::textChanged, this, &InsertGlyphDialog::updateGlyph);
	updateGlyph();
}

InsertGlyphDialog::~InsertGlyphDialog() = default;


QString InsertGlyphDialog::glyph() const
{
	return current;
}


void InsertGlyphDialog::updateGlyph()
{
	QString error;
	current = glyphFromHex(code_edit->text(), &error);
	preview->setText(current);
	message->setText(code_edit->text().trimmed().isEmpty() ? QString{} : error);
	buttons->button(QDialogButtonBox::Ok)->setEnabled(!current.isEmpty());
}


}  // namespace OpenOrienteering
