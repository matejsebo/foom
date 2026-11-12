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


#ifndef OPENORIENTEERING_NUMERIC_TRANSFORM_DIALOGS_H
#define OPENORIENTEERING_NUMERIC_TRANSFORM_DIALOGS_H

#include <QCoreApplication>

class QWidget;

namespace OpenOrienteering {

namespace ops {
struct MoveParams;
struct RotateParams;
struct StretchParams;
}

/**
 * foom: The dialogs of the numeric transforms move-by, rotate-by and stretch.
 *
 * Each function shows the parameters, returns true when the dialog was
 * accepted, and then holds the entered values in the parameters.
 *
 * See the design notes, "Numeric transforms".
 */
class NumericTransformDialogs
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::NumericTransformDialogs)
public:
	static bool askMoveBy(QWidget* parent, ops::MoveParams& params);
	static bool askRotateBy(QWidget* parent, ops::RotateParams& params);
	static bool askStretch(QWidget* parent, ops::StretchParams& params);
};


}  // namespace OpenOrienteering

#endif
