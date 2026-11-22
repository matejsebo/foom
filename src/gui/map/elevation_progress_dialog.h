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

#ifndef OPENORIENTEERING_ELEVATION_PROGRESS_DIALOG_H
#define OPENORIENTEERING_ELEVATION_PROGRESS_DIALOG_H

#include <QObject>
#include <QProgressDialog>
#include <QString>

#include "elevation/grid_tiles.h"

class QWidget;

namespace OpenOrienteering {


/**
 * The progress of a run of the elevation commands, a row of tiles at a time,
 * with Cancel.
 *
 * See the design notes, "Grids on disk, a tile cache, and a progress bar".
 */
class ElevationProgressDialog : public QProgressDialog
{
Q_OBJECT
public:
	explicit ElevationProgressDialog(const QString& title, QWidget* parent = nullptr);
	~ElevationProgressDialog() override;

	/**
	 * The callback for the run: sets the label and the bar, lets the dialog
	 * handle its events, and returns false once Cancel was pressed.
	 */
	TileProgress callback();
};


}  // namespace OpenOrienteering

#endif
