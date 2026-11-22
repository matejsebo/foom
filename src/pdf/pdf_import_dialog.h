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

#ifndef FOOM_PDF_IMPORT_DIALOG_H
#define FOOM_PDF_IMPORT_DIALOG_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include "pdf/pdf_import.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The dialog "Import PDF": page, scale of the PDF, placement and text.
 *
 * See the design notes, "PDF import".
 */
class PdfImportDialog : public QDialog
{
Q_OBJECT
public:
	PdfImportDialog(const QString& path, const Map& map, const QPointF& view_centre, QWidget* parent = nullptr);
	~PdfImportDialog() override;

	/// The pages of the file; empty when it cannot be read.
	const std::vector<QSizeF>& pageSizes() const { return sizes; }

	/// The error when the file cannot be read.
	const QString& error() const { return read_error; }

	PdfImportOptions options() const;

private:
	void updateWidgets();

	const Map& map;
	QPointF view_centre;
	std::vector<QSizeF> sizes;
	QString read_error;

	QComboBox* page_box;
	QSpinBox* scale_box;
	QRadioButton* place_origin;
	QRadioButton* place_view;
	QRadioButton* place_typed;
	QDoubleSpinBox* centre_x;
	QDoubleSpinBox* centre_y;
	QCheckBox* text_box;
	QLabel* extent_label;
};


}  // namespace OpenOrienteering

#endif
