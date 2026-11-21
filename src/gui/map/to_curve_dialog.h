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


#ifndef OPENORIENTEERING_TO_CURVE_DIALOG_H
#define OPENORIENTEERING_TO_CURVE_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/ops/to_curve.h"

class QCheckBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {


/**
 * Asks for the options of the to-curve command.
 *
 * See the design notes, "Polyline to curve".
 */
class ToCurveDialog : public QDialog
{
Q_OBJECT
public:
	ToCurveDialog(const ops::ToCurveParams& initial, QWidget* parent);
	~ToCurveDialog() override;

	ops::ToCurveParams params() const;

private:
	QRadioButton* all_button;
	QRadioButton* straight_button;
	QDoubleSpinBox* factor_edit;
	QCheckBox* ends_check;
	QCheckBox* corners_check;
	QDoubleSpinBox* angle_edit;
};


}  // namespace OpenOrienteering

#endif
