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

#ifndef FOOM_GUI_SELECTION_DIALOGS_H
#define FOOM_GUI_SELECTION_DIALOGS_H

#include <QtGlobal>
#include <QDialog>
#include <QObject>

#include "core/selection_queries.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {


/**
 * The select-by-date dialog: created or modified, from and to.
 *
 * See the design notes, "Selection queries".
 */
class SelectByDateDialog : public QDialog
{
Q_OBJECT
public:
	explicit SelectByDateDialog(QWidget* parent);
	~SelectByDateDialog() override;
	
	DateKind kind() const;
	qint64 from() const;
	qint64 to() const;
	
private:
	QRadioButton* created_button;
	QRadioButton* modified_button;
	QDateTimeEdit* from_edit;
	QDateTimeEdit* to_edit;
};


/**
 * The select-by-property dialog: property, operator, value, and the symbol scope.
 */
class SelectByPropertyDialog : public QDialog
{
Q_OBJECT
public:
	explicit SelectByPropertyDialog(QWidget* parent);
	~SelectByPropertyDialog() override;
	
	PropertyQuery query() const;
	bool onlySelectedSymbols() const;
	
private:
	QComboBox* property_combo;
	QComboBox* compare_combo;
	QDoubleSpinBox* value_edit;
	QCheckBox* symbols_check;
};


}  // namespace OpenOrienteering

#endif
