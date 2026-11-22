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

#ifndef OPENORIENTEERING_MAP_IMPORT_DIALOG_H
#define OPENORIENTEERING_MAP_IMPORT_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QPointF>
#include <QString>

#include "core/map_import_policy.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The choices for importing another map file: which symbols and colours,
 * where the colours go, protection, and placement.
 *
 * See the design notes, "Map import policies".
 */
class MapImportDialog : public QDialog
{
Q_OBJECT
public:
	MapImportDialog(const Map& imported, const Map& map, QWidget* parent = nullptr);
	~MapImportDialog() override;

	/** The point for the placement by mouse: the centre of the view, map millimetres. */
	void setViewCenter(const QPointF& center) { view_center = center; }

	/** The policy as chosen. */
	MapImportPolicy policy() const;

	/** Stores the choices of symbols, colours and protection for the next import, then accepts. */
	void accept() override;

private:
	void updateWidgets();

	QPointF view_center;

	QRadioButton* none_radio;
	QRadioButton* absent_radio;
	QRadioButton* renumber_radio;
	QRadioButton* all_radio;
	QComboBox* colours_combo;
	QCheckBox* protect_check;
	QCheckBox* rescale_check;
	QRadioButton* georef_radio;
	QRadioButton* offset_radio;
	QRadioButton* mouse_radio;
	QDoubleSpinBox* offset_x_spin;
	QDoubleSpinBox* offset_y_spin;
};


}  // namespace OpenOrienteering

#endif
