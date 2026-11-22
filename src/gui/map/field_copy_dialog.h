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


#ifndef FOOM_FIELD_COPY_DIALOG_H
#define FOOM_FIELD_COPY_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QRectF>
#include <QString>

#include "sync/field_copy.h"

class QCheckBox;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The options of a field copy: its extent, cached templates, and whether it
 * is a review copy.
 *
 * See the design notes, "Merge changes from file: detail", third change.
 */
class FieldCopyDialog : public QDialog
{
Q_OBJECT
public:
	/**
	 * The visible area and the extent of the selected objects are offered
	 * as extents; a null selection rectangle is not offered.
	 */
	FieldCopyDialog(const QRectF& visible_area, const QRectF& selection_area, QWidget* parent = nullptr);
	~FieldCopyDialog() override;
	
	FieldCopyOptions options() const;
	
private:
	QRectF visible_area;
	QRectF selection_area;
	QRadioButton* whole_button;
	QRadioButton* visible_button;
	QRadioButton* selection_button;
	QCheckBox* templates_check;
	QSpinBox* dpi_spin;
	QCheckBox* review_check;
};


}  // namespace OpenOrienteering

#endif
