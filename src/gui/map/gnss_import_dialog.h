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


#ifndef OPENORIENTEERING_GNSS_IMPORT_DIALOG_H
#define OPENORIENTEERING_GNSS_IMPORT_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>

#include "sensors/gnss_file.h"

class QComboBox;
class QLabel;
class QWidget;

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * Asks how the data of a GNSS file is loaded: as a track, as waypoints or
 * as positions, and with which symbol.
 *
 * See the design notes, "NMEA and FRWD files".
 */
class GnssImportDialog : public QDialog
{
Q_OBJECT
public:
	GnssImportDialog(const Map& map, const GnssFileData& data, const Symbol* current_symbol, QWidget* parent = nullptr);
	~GnssImportDialog() override;

	GnssImportKind kind() const;
	const Symbol* symbol() const;

	/// Chooses the kind, for the test.
	void setKind(GnssImportKind kind);

private:
	void kindChanged();

	const Map& map;
	const GnssFileData& data;
	const Symbol* current_symbol;
	QComboBox* kind_box;
	QComboBox* symbol_box;
	QLabel* summary;
};


}  // namespace OpenOrienteering

#endif
