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


#ifndef OPENORIENTEERING_GEODATA_IMPORT_DIALOG_H
#define OPENORIENTEERING_GEODATA_IMPORT_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "gdal/geodata_import.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * foom: The options dialog of the geodata import (`import-geodata`).
 *
 * See the design notes, "Import options".
 */
class GeodataImportDialog : public QDialog
{
Q_OBJECT
public:
	/**
	 * Constructs the dialog for the given files.
	 *
	 * view_rect is the area of the map which the main view shows, in map coordinates.
	 */
	GeodataImportDialog(const QStringList& paths, const Map& map, const QRectF& view_rect, QWidget* parent = nullptr);
	
	~GeodataImportDialog() override;
	
	/**
	 * Returns the options as chosen.
	 */
	GeodataImportOptions options() const;
	
private:
	void fillLayers();
	void updateWidgets();
	void browseCrtFile();
	
	const Map& map;
	QStringList paths;
	QRectF view_rect;
	
	QListWidget* layer_list;
	QRadioButton* detected_radio;
	QRadioButton* override_radio;
	QRadioButton* unreferenced_radio;
	QLineEdit* crs_edit;
	QComboBox* units_combo;
	QDoubleSpinBox* custom_spin;
	QDoubleSpinBox* offset_x_spin;
	QDoubleSpinBox* offset_y_spin;
	QDoubleSpinBox* rotation_spin;
	QComboBox* clip_combo;
	QDoubleSpinBox* box_spins[4];
	QComboBox* layer_tag_combo;
	QComboBox* text_combo;
	QCheckBox* blocks_check;
	QDoubleSpinBox* simplify_spin;
	QComboBox* symbols_combo;
	QLineEdit* crt_edit;
	QPushButton* crt_button;
	QDialogButtonBox* buttons;
};


}  // namespace OpenOrienteering

#endif
