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


#ifndef FOOM_FIX_GATE_ROWS_H
#define FOOM_FIX_GATE_ROWS_H

#include <QObject>
#include <QString>

#include "sensors/fix_gate.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The rows of the sensors settings page which edit the quality gate.
 *
 * The widgets carry the object names gate-min-class, gate-min-satellites,
 * gate-max-hdop, gate-max-error and gate-policy.
 *
 * See the design notes, "GNSS sources: detail".
 */
class FixGateRows : public QObject
{
Q_OBJECT
public:
	/** Appends the rows to the layout. */
	FixGateRows(QFormLayout* layout, QWidget* parent);
	~FixGateRows() override;
	
	FixGateSettings settings() const;
	void setSettings(const FixGateSettings& settings);
	
private:
	QComboBox* class_box;
	QSpinBox* satellites_box;
	QDoubleSpinBox* hdop_box;
	QDoubleSpinBox* error_box;
	QComboBox* policy_box;
};


}  // namespace OpenOrienteering

#endif
