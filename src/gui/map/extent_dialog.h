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


#ifndef OPENORIENTEERING_EXTENT_DIALOG_H
#define OPENORIENTEERING_EXTENT_DIALOG_H

#include <QDialog>
#include <QObject>

#include "core/extents.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The dialog of the extent-define command.
 *
 * See the design notes, "Named extents and the paper frame".
 */
class ExtentDialog : public QDialog
{
Q_OBJECT
public:
	ExtentDialog(MapEditorController* editor, const Extent& extent, QWidget* parent);
	~ExtentDialog() override;
	
	Extent extent() const;
	void setExtent(const Extent& extent);

private slots:
	void fromPaperFormat();
	void fromCurrentView();
	void fromPrintArea();

private:
	void setRect(const QRectF& rect);
	
	MapEditorController* editor;
	QLineEdit* name_edit;
	QDoubleSpinBox* left_edit;
	QDoubleSpinBox* top_edit;
	QDoubleSpinBox* width_edit;
	QDoubleSpinBox* height_edit;
	QSpinBox* scale_edit;
	QComboBox* paper_combo;
	QRadioButton* portrait_button;
	QDoubleSpinBox* margin_edit;
	QComboBox* back_of_combo;
};


}  // namespace OpenOrienteering

#endif
