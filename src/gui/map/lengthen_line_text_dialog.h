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


#ifndef OPENORIENTEERING_LENGTHEN_LINE_TEXT_DIALOG_H
#define OPENORIENTEERING_LENGTHEN_LINE_TEXT_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/ops/line_text.h"

class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {


/**
 * Asks how far to lengthen the paths of line text objects.
 *
 * See the design notes, "Lengthen line text".
 */
class LengthenLineTextDialog : public QDialog
{
Q_OBJECT
public:
	LengthenLineTextDialog(const ops::LengthenLineTextParams& initial, QWidget* parent);
	~LengthenLineTextDialog() override;

	ops::LengthenLineTextParams params() const;

private:
	QRadioButton* distance_radio;
	QDoubleSpinBox* distance_edit;
	QRadioButton* percent_radio;
	QDoubleSpinBox* percent_edit;
};


}  // namespace OpenOrienteering

#endif
