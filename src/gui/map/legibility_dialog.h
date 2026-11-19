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


#ifndef OPENORIENTEERING_LEGIBILITY_DIALOG_H
#define OPENORIENTEERING_LEGIBILITY_DIALOG_H

#include <QDialog>
#include <QObject>

#include "check/legibility.h"

class QCheckBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The dialog of the check-legibility command.
 *
 * See the design notes, "Legibility".
 */
class LegibilityDialog : public QDialog
{
Q_OBJECT
public:
	LegibilityDialog(MapEditorController* editor, QWidget* parent);
	~LegibilityDialog() override;
	
	/** Returns the parameters as set in the dialog, the scope and extent resolved. */
	LegibilityParams params() const;
	
private:
	MapEditorController* editor;
	QCheckBox* length_check;
	QCheckBox* area_check;
	QCheckBox* gap_check;
	QDoubleSpinBox* gap_edit;
	QCheckBox* same_group_check;
	QCheckBox* contours_check;
	QRadioButton* all_objects_button;
	QRadioButton* selected_symbols_button;
	QRadioButton* whole_map_button;
	QRadioButton* current_view_button;
};


}  // namespace OpenOrienteering

#endif
