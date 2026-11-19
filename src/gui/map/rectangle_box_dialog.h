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


#ifndef OPENORIENTEERING_RECTANGLE_BOX_DIALOG_H
#define OPENORIENTEERING_RECTANGLE_BOX_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/ops/rectangle_box.h"

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class Map;
class SymbolDropDown;


/**
 * Asks for the parameters of a rectangle box.
 *
 * The frame symbol is the active symbol and the group number is given by the
 * tool, so neither is asked. See the design notes, "Rectangle
 * objects".
 */
class RectangleBoxDialog : public QDialog
{
Q_OBJECT
public:
	RectangleBoxDialog(const Map* map, QWidget* parent);
	~RectangleBoxDialog() override;
	
	ops::RectangleBoxParams params() const;
	
private:
	QDoubleSpinBox* radius_edit;
	QDoubleSpinBox* cell_width_edit;
	QDoubleSpinBox* cell_height_edit;
	SymbolDropDown* grid_symbol_edit;
	QCheckBox* numbering_check;
	QCheckBox* from_bottom_check;
	SymbolDropDown* text_symbol_edit;
	QSpinBox* unnumbered_edit;
	QLineEdit* unnumbered_text_edit;
};


}  // namespace OpenOrienteering

#endif
