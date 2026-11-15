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


#ifndef OPENORIENTEERING_IMPORT_POINTS_DIALOG_H
#define OPENORIENTEERING_IMPORT_POINTS_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/ops/import_points.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class Map;
class Symbol;
class SymbolDropDown;


/**
 * foom: The dialog of the point import from delimited text (`import-points`).
 *
 * See the design notes, "Import options", Delimited text.
 */
class ImportPointsDialog : public QDialog
{
Q_OBJECT
public:
	ImportPointsDialog(const QString& text, const Map& map, const Symbol* initial_symbol, QWidget* parent = nullptr);
	
	~ImportPointsDialog() override;
	
	/**
	 * Returns the parameters as chosen.
	 */
	ops::ImportPointsParams params() const;
	
private:
	QChar delimiter() const;
	void updateColumns();
	void updateWidgets();
	
	const Map& map;
	QStringList lines;
	
	QComboBox* delimiter_combo;
	QCheckBox* header_check;
	QTableWidget* preview;
	QComboBox* x_combo;
	QComboBox* y_combo;
	QComboBox* z_combo;
	QComboBox* text_combo;
	QComboBox* code_combo;
	QComboBox* crs_combo;
	QLineEdit* crs_edit;
	SymbolDropDown* point_symbol_edit;
	SymbolDropDown* text_symbol_edit;
	QDialogButtonBox* buttons;
	bool updating = false;
};


}  // namespace OpenOrienteering

#endif
