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


#ifndef OPENORIENTEERING_NEW_MAP_WIZARD_H
#define OPENORIENTEERING_NEW_MAP_WIZARD_H

#include <QObject>
#include <QString>
#include <QWizard>

#include "gdal/new_map.h"

class QWidget;

namespace OpenOrienteering {

class AreaPage;
class DataPage;
class GeoreferencingPage;
class RunPage;


/**
 * foom: Pages 2 to 5 of the new-map flow (`file-new-wizard`): area,
 * georeferencing, data, run. Page 1, the symbol set and the scale, is OOM's
 * New map dialog, shown before.
 *
 * See the design notes, "New map flow".
 */
class NewMapWizard : public QWizard
{
Q_OBJECT
public:
	NewMapWizard(const QString& symbol_set_path, unsigned int scale, QWidget* parent = nullptr);
	
	~NewMapWizard() override;
	
	/** The parameters as chosen on the pages. */
	NewMapParams params() const;
	
	/** The path of the created map, after the wizard was accepted. */
	QString mapPath() const { return map_path; }
	
	/** Runs createNewMap() with the chosen parameters; called by the last page. */
	bool run();
	
private:
	QString symbol_set_path;
	unsigned int scale;
	QString map_path;
	
	AreaPage* area_page;
	GeoreferencingPage* georef_page;
	DataPage* data_page;
	RunPage* run_page;
};


}  // namespace OpenOrienteering

#endif
