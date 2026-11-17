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


#ifndef FOOM_GENERATOR_H
#define FOOM_GENERATOR_H

#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/map_coord.h"

namespace OpenOrienteering {

class Map;
class Object;


/**
 * Generators: commands which emit a group of ordinary objects from a set of
 * parameters and can rebuild that group later.
 *
 * Every object of a generated group carries the tags `generator` (the id) and
 * `params` (the parameters as JSON).
 *
 * See the design notes, "Generators".
 */

/** One parameter of a generator, as the dialog shows it. */
struct GeneratorField
{
	enum Kind { Bool, Int, Double, Choice, LineSymbol, AreaSymbol, TextSymbol };
	
	QString key;
	QString label;
	Kind kind = Bool;
	double minimum = 0;
	double maximum = 0;
	int decimals = 0;
	QString suffix;
	QStringList choices;        ///< The stored values of a Choice.
	QStringList choice_labels;  ///< Their labels.
};

/** The ids of the generators which are built. */
QStringList generatorIds();

/** The name of a generator, which is also the name of its groups. */
QString generatorTitle(const QString& id);

/** The parameters of a generator, in dialog order. */
std::vector<GeneratorField> generatorFields(const QString& id);

/** The default parameters of a generator for the map. */
QJsonObject defaultGeneratorParams(const QString& id, const Map& map);

/**
 * The automatic length of a scale bar in metres: the largest of 1, 2 and 5
 * times a power of ten which is at most 60 mm on paper.
 */
double scaleBarAutoLength(unsigned int scale_denominator);

/** The default spacing of north lines on the ground, in metres. */
double northLinesDefaultSpacing(const Map& map);

/**
 * Runs a generator: adds its objects as one new group, as one undo step.
 *
 * The scale bar and the legend go to the layout part, which is created when
 * the map has none, with the anchor as their top left corner. North lines go
 * to the first part with the role map.
 *
 * Returns the group, or 0 with a message in error.
 */
int runGenerator(Map& map, const QString& id, const QJsonObject& params, const MapCoordF& anchor, QString* error);

/** The generated groups of the map, or those which the selection touches. */
std::vector<int> generatedGroups(const Map& map, bool selected_only);

/** The generated groups which depend on the map scale: the scale bars. */
std::vector<int> scaleDependentGroups(const Map& map);

/** The objects of a group, in object order, and the index of their part. */
std::vector<Object*> groupObjects(const Map& map, int group, int* part_index = nullptr);

/**
 * Rebuilds the groups from their stored parameters, as one undo step.
 * Returns the number of groups rebuilt.
 */
int regenerateGroups(Map& map, const std::vector<int>& groups, QString* error);


}  // namespace OpenOrienteering

#endif
