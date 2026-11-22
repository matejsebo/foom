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


#ifndef FOOM_OBJECT_STYLE_DIALOG_H
#define FOOM_OBJECT_STYLE_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/symbols/object_style.h"

class QDoubleSpinBox;
class QFontComboBox;
class QWidget;

namespace OpenOrienteering {

class ColorDropDown;
class Map;


/**
 * The reduced panel of `object-style`: colour, width, fill, font and size of
 * a private symbol.
 *
 * See the design notes, "Object style panel".
 */
class ObjectStyleDialog : public QDialog
{
Q_OBJECT
public:
	ObjectStyleDialog(const Map& map, const ObjectStyle& style, QWidget* parent = nullptr);

	~ObjectStyleDialog() override;

	/** The style as chosen. */
	ObjectStyle style() const;

	/** Returns true if the dialog was closed by the button which asks for the symbol editor. */
	bool symbolEditorRequested() const { return editor_requested; }

private:
	ObjectStyle initial;
	ColorDropDown* line_color = nullptr;
	QDoubleSpinBox* line_width = nullptr;
	ColorDropDown* fill_color = nullptr;
	ColorDropDown* text_color = nullptr;
	QFontComboBox* font_family = nullptr;
	QDoubleSpinBox* font_size = nullptr;
	bool editor_requested = false;
};


}  // namespace OpenOrienteering

#endif
