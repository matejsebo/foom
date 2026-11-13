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


#ifndef OPENORIENTEERING_MOVE_PARALLEL_DIALOG_H
#define OPENORIENTEERING_MOVE_PARALLEL_DIALOG_H

#include <QDialog>
#include <QObject>

class QComboBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The dialog of the move-parallel-by command.
 *
 * It stays open: Move and Duplicate apply to the current selection.
 * See the design notes, "Parallel offset".
 */
class MoveParallelDialog : public QDialog
{
Q_OBJECT
public:
	MoveParallelDialog(MapEditorController* editor, QWidget* parent);
	~MoveParallelDialog() override;
	
	/** Returns the entered distance in mm, negative for the right side. */
	double signedDistance() const;

private slots:
	void move();
	void duplicate();

private:
	MapEditorController* editor;
	QDoubleSpinBox* distance_edit;
	QComboBox* unit_combo;
	QRadioButton* left_button;
};


}  // namespace OpenOrienteering

#endif
