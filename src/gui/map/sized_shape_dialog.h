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


#ifndef OPENORIENTEERING_SIZED_SHAPE_DIALOG_H
#define OPENORIENTEERING_SIZED_SHAPE_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/ops/sized_shape.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QWidget;

namespace OpenOrienteering {


/**
 * Asks for the kind, the size and the angle of a shape.
 *
 * See the design notes, "Shapes of a given size".
 */
class SizedShapeDialog : public QDialog
{
Q_OBJECT
public:
	SizedShapeDialog(const ops::SizedShapeParams& initial, QWidget* parent);
	~SizedShapeDialog() override;

	ops::SizedShapeParams params() const;

private:
	void kindChanged();

	QComboBox* kind_combo;
	QComboBox* unit_combo;
	QLabel* width_label;
	QDoubleSpinBox* width_edit;
	QLabel* height_label;
	QDoubleSpinBox* height_edit;
	QDoubleSpinBox* angle_edit;
};


}  // namespace OpenOrienteering

#endif
