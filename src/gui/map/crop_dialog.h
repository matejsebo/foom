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

#ifndef FOOM_CROP_DIALOG_H
#define FOOM_CROP_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QRectF>

class QCheckBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {


/**
 * Asks for the objects, the shape and the options of the crop command.
 *
 * See the design notes, "Crop".
 */
class CropDialog : public QDialog
{
Q_OBJECT
public:
	/**
	 * Constructs the dialog. Without shapes, the rectangle is chosen and the
	 * choice of the selected shapes is greyed. The rectangle starts as rect.
	 */
	CropDialog(bool have_shapes, const QRectF& rect, QWidget* parent);
	~CropDialog() override;
	
	bool selectedSymbolsOnly() const;
	bool useRectangle() const;
	QRectF rectangle() const;
	bool hole() const;
	bool deleteShapes() const;
	
private:
	void updateWidgets();
	
	QRadioButton* all_button;
	QRadioButton* symbols_button;
	QRadioButton* shapes_button;
	QRadioButton* rect_button;
	QDoubleSpinBox* left_edit;
	QDoubleSpinBox* top_edit;
	QDoubleSpinBox* right_edit;
	QDoubleSpinBox* bottom_edit;
	QCheckBox* delete_check;
	QCheckBox* hole_check;
};


}  // namespace OpenOrienteering

#endif
