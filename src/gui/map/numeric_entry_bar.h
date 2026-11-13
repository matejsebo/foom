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


#ifndef OPENORIENTEERING_NUMERIC_ENTRY_BAR_H
#define OPENORIENTEERING_NUMERIC_ENTRY_BAR_H

#include <QObject>
#include <QString>
#include <QToolBar>

#include "core/map_coord.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace OpenOrienteering {

class Georeferencing;
class Map;
class MapEditorController;
class MapEditorTool;


/**
 * The numeric entry bar: vertices typed as a position or as length and angle.
 *
 * See the design notes, "Numeric entry bar".
 */
class NumericEntryBar : public QToolBar
{
Q_OBJECT
public:
	enum Mode
	{
		Position = 0,
		LengthAndAngle = 1
	};
	
	NumericEntryBar(MapEditorController* editor, QWidget* parent);
	~NumericEntryBar() override;
	
	Mode mode() const;
	void setMode(Mode mode);
	
	/** Sets the two fields' texts. */
	void setValues(const QString& first, const QString& second);
	
	bool addGridAngle() const;
	void setAddGridAngle(bool add);
	
	/**
	 * Computes the vertex for the entry.
	 *
	 * In the position mode, first and second are x and y in mm (y up) for a
	 * map in the Local georeferencing state, else easting and northing. In
	 * the length-and-angle mode, first is the length in mm (Local) or in m on
	 * the ground, second the angle in degrees clockwise from the top of the
	 * paper, measured from the reference vertex, which must be given.
	 * Returns false and sets error when the entry cannot be used.
	 */
	static bool computeVertex(const Map& map, Mode mode, double first, double second, bool add_grid_angle, const MapCoordF* reference, MapCoordF& out, QString& error);
	
	/** Returns the clockwise angle in degrees from the top of the paper to grid north. */
	static double gridNorthAngle(const Georeferencing& georef);

public slots:
	/** Passes the entered vertex to the active tool. Returns false, after a status bar message, when it cannot. */
	bool accept();

private slots:
	void updateForMode();
	void toolChanged(OpenOrienteering::MapEditorTool* tool);

private:
	void showMessage(const QString& text);
	
	MapEditorController* editor;
	QComboBox* mode_combo;
	QLabel* first_label;
	QLineEdit* first_edit;
	QLabel* second_label;
	QLineEdit* second_edit;
	QCheckBox* grid_check;
	QPushButton* add_button;
};


}  // namespace OpenOrienteering

#endif
