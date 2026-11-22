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


#ifndef FOOM_GRADIENT_DIALOG_H
#define FOOM_GRADIENT_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/ops/gradient.h"

class QCheckBox;
class QDoubleSpinBox;
class QWidget;

namespace OpenOrienteering {

class ColorDropDown;
class Map;


/**
 * The dialog of `gradient-create`.
 *
 * See the design notes, "Image object", second change.
 */
class GradientDialog : public QDialog
{
Q_OBJECT
public:
	/** line_selected enables the option to take the direction from the selected line. */
	GradientDialog(const Map& map, bool line_selected, QWidget* parent = nullptr);

	~GradientDialog() override;

	/** The parameters as chosen. */
	ops::GradientParams params() const;

private:
	ColorDropDown* from_color;
	ColorDropDown* to_color;
	QDoubleSpinBox* angle;
	QCheckBox* use_line;
	QCheckBox* keep_areas;
};


}  // namespace OpenOrienteering

#endif
