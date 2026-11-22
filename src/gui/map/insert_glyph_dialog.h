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

#ifndef OPENORIENTEERING_INSERT_GLYPH_DIALOG_H
#define OPENORIENTEERING_INSERT_GLYPH_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

class QDialogButtonBox;
class QFont;
class QLabel;
class QLineEdit;
class QWidget;

namespace OpenOrienteering {


/**
 * Asks for a character by its hexadecimal code, or from a choice of
 * special characters.
 * 
 * See the design notes, "Insert glyphs".
 */
class InsertGlyphDialog : public QDialog
{
Q_OBJECT
public:
	InsertGlyphDialog(const QFont& font, QWidget* parent = nullptr);
	~InsertGlyphDialog() override;
	
	/** The chosen character, or an empty string. */
	QString glyph() const;
	
	/** The special characters offered as buttons. */
	static QString specialCharacters();
	
private:
	void updateGlyph();
	
	QLineEdit* code_edit;
	QLabel* preview;
	QLabel* message;
	QDialogButtonBox* buttons;
	QString current;
};


}  // namespace OpenOrienteering

#endif
