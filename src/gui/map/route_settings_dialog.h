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


#ifndef FOOM_ROUTE_SETTINGS_DIALOG_H
#define FOOM_ROUTE_SETTINGS_DIALOG_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>

#include "course/route_settings.h"

class QCheckBox;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The settings of the route analysis: the base pace, whether runnability
 * and elevation are used, and the runnability of the symbols.
 *
 * See the design notes, "Route analysis: the settings and
 * the elevation".
 */
class RouteSettingsDialog : public QDialog
{
Q_OBJECT
public:
	/** has_elevation says whether the map has a terrain model. */
	RouteSettingsDialog(const Map* map, bool has_elevation, QWidget* parent = nullptr);
	~RouteSettingsDialog() override;
	
	/** The settings as they are in the dialog; a symbol is in them when it differs from its default. */
	RouteSettings settings() const;
	
	/** Sets the runnability of the row of a symbol; false when the table has no such symbol. */
	bool setRunnability(const QString& number, const Runnability& runnability);
	
	/** Puts every row back to its default. */
	void resetSymbols();
	
private:
	void updateRow(int row);
	
	RouteSettings initial;
	/** For every row the number of its symbol and its default. */
	std::vector<QString> numbers;
	std::vector<Runnability> defaults;
	bool updating = false;
	
	QSpinBox* pace_edit;
	QCheckBox* runnability_check;
	QCheckBox* elevation_check;
	QTableWidget* table;
};


}  // namespace OpenOrienteering

#endif
