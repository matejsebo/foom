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


#ifndef OPENORIENTEERING_IMPORT_RULE_CHOICE_H
#define OPENORIENTEERING_IMPORT_RULE_CHOICE_H

#include <QString>
#include <QStringList>

#include "gdal/geodata_import.h"

namespace OpenOrienteering {

class Map;


/**
 * foom: The shipped rule table chosen for a set of geodata files.
 *
 * See the design notes, "Choosing the table from the dataset".
 */
struct ImportRuleChoice
{
	QString dataset;   ///< The recognised dataset ("OSM", ...); empty when none.
	QString crt_file;  ///< The table for the map's symbol set; empty when there is none.
	GeodataImportOptions::LayerTag layer_tag = GeodataImportOptions::NoLayerTag;  ///< What the table's rules need.
};


/**
 * foom: Names the dataset which the files are, from their driver, layer
 * names and fields, or returns an empty string.
 *
 * Every file must be recognised, and all as the same dataset.
 */
QString recogniseGeodataDataset(const QStringList& paths);

/**
 * foom: A readable name of a dataset as recogniseGeodataDataset() names it.
 */
QString geodataDatasetTitle(const QString& dataset);

/**
 * foom: Chooses the shipped rule table for the files and the map's symbol set.
 *
 * The table is `<dataset>-<symbol set id>.crt` in the folder `symbol sets`
 * of the first data search path that has it.
 */
ImportRuleChoice chooseImportRules(const QStringList& paths, const Map& map);


}  // namespace OpenOrienteering

#endif
