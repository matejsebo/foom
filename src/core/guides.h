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


#ifndef OPENORIENTEERING_GUIDES_H
#define OPENORIENTEERING_GUIDES_H

#include <vector>

#include <QtGlobal>
#include <QByteArray>

#include "core/map_coord.h"

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

/**
 * A guide: a horizontal or vertical line across the map, for alignment.
 *
 * A horizontal guide lies at a y coordinate, a vertical one at an x coordinate,
 * both in native map units (1/1000 mm, y down). Guides are not printed.
 *
 * See the design notes, "Rulers, guides and shortcut editor: detail".
 */
struct Guide
{
	Qt::Orientation orientation = Qt::Horizontal;
	qint32 position = 0;
	
	/** Returns the position in mm. */
	double positionMM() const { return position / 1000.0; }
};

inline bool operator==(const Guide& lhs, const Guide& rhs)
{
	return lhs.orientation == rhs.orientation && lhs.position == rhs.position;
}

inline bool operator!=(const Guide& lhs, const Guide& rhs)
{
	return !(lhs == rhs);
}


/**
 * The guides of a map, and whether they are shown and locked.
 */
struct Guides
{
	std::vector<Guide> list;
	bool visible = true;
	bool locked = false;
	
	/** True when there is nothing to save: no guide, shown and not locked. */
	bool isDefault() const { return list.empty() && visible && !locked; }
};

bool operator==(const Guides& lhs, const Guides& rhs);
inline bool operator!=(const Guides& lhs, const Guides& rhs) { return !(lhs == rhs); }


/** Writes the guides as the element <guides>. */
void saveGuides(const Guides& guides, QXmlStreamWriter& xml);

/** Reads the element <guides> at which the reader stands. */
Guides loadGuides(QXmlStreamReader& xml);

/** Returns the guides serialised as <guides>, for the undo step. */
QByteArray guidesToXml(const Guides& guides);

/** Reads guides from the output of guidesToXml(). */
Guides guidesFromXml(const QByteArray& data);


/**
 * Snaps a position to the guides.
 * 
 * Where a horizontal and a vertical guide both lie within max_distance (mm),
 * the result is their crossing; else it is the nearest point on the nearest guide
 * within max_distance. Returns false, leaving out unchanged, when no guide is
 * close enough.
 */
bool snapToGuides(const std::vector<Guide>& guides, const MapCoordF& position, double max_distance, MapCoordF& out);

/**
 * Returns the index of the guide nearest to the position within max_distance (mm),
 * or -1.
 */
int guideAt(const std::vector<Guide>& guides, const MapCoordF& position, double max_distance);

/** Returns the guide of the orientation through the position. */
Guide guideThrough(Qt::Orientation orientation, const MapCoordF& position);


}  // namespace OpenOrienteering

#endif
