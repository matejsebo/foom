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


#ifndef OPENORIENTEERING_EXTENTS_H
#define OPENORIENTEERING_EXTENTS_H

#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QPageSize>
#include <QRectF>
#include <QSizeF>
#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering { class Map; }

namespace OpenOrienteering {

/**
 * A named extent: a rectangle on the map with its print settings.
 *
 * See the design notes, "Named extents and the paper frame".
 */
struct Extent
{
	QString name;
	QRectF rect_mm;            ///< In map coordinates.
	unsigned int scale = 0;    ///< The print scale denominator, 0 for the map scale.
	QString paper;             ///< A QPageSize key, or empty.
	qreal margin_mm = 0;
	QString back_of;           ///< The name of the front extent this one backs, or empty.
};

bool operator==(const Extent& lhs, const Extent& rhs);
inline bool operator!=(const Extent& lhs, const Extent& rhs) { return !(lhs == rhs); }

/** Writes the extents as the element <extents>. */
void saveExtents(const std::vector<Extent>& extents, QXmlStreamWriter& xml);

/** Reads the element <extents> the reader stands on. */
std::vector<Extent> loadExtents(QXmlStreamReader& xml);

/** Returns the <extents> element as a document, for the undo step. */
QByteArray extentsToXml(const std::vector<Extent>& extents);

/** Reads the extents from such a document. */
std::vector<Extent> extentsFromXml(const QByteArray& xml);

/**
 * Returns the size on the map, in mm, which a paper of the size covers when
 * the map of the map scale is printed at the print scale.
 */
QSizeF extentSizeFromPaper(QPageSize::PageSizeId paper, bool landscape, unsigned int print_scale, unsigned int map_scale);

/** Returns the page size id for a QPageSize key, or Custom when unknown. */
QPageSize::PageSizeId pageSizeFromKey(const QString& key);

/**
 * Gives a map which has a stored print area and no extents the extent
 * "default", made from the print area, the print scale and the paper.
 * 
 * Returns true if the extent was added. The map is not marked as changed.
 */
bool addDefaultExtent(Map& map);

/**
 * Adds the extents to the map, as one undo step. An extent with the name of
 * an existing one replaces it. Returns the number of extents taken.
 */
int importExtents(Map& map, const std::vector<Extent>& extents);

/** Reads the map file and takes its extents. Returns -1 with a message when the file cannot be read. */
int importExtentsFromFile(Map& map, const QString& path, QString* error);


}  // namespace OpenOrienteering

#endif
