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

#ifndef FOOM_ELEVATION_OCD_COURSE_DEM_H
#define FOOM_ELEVATION_OCD_COURSE_DEM_H

#include <memory>

#include <QtGlobal>
#include <QByteArray>
#include <QString>

namespace OpenOrienteering {

class ElevationModel;

/// The type of the parameter string which describes the DEM of an OCD file.
constexpr int ocd_dem_string_type = 1045;


/**
 * The grid as the DEM for course setting: the block which an OCD
 * file of version 2018 embeds, and the whole of an .ocdCsDem file.
 *
 * One zlib stream of "CS_DEM", a 16-bit version, twelve 32-bit integers
 * (columns, rows, x of the first and the last cell centre, y of the south and
 * the north one, the lowest and the highest height, the cell size, the base
 * height, the height step, the cell size again; all in centimetres) and 16
 * bits a cell, rows from the north: the height above the base in steps, 0xFFFF
 * for a void cell.
 *
 * See the design notes, "The embedded DEM in OCD files".
 */
QByteArray encodeOcdCourseDem(const ElevationModel& grid);

/**
 * The grid of a block read from an OCD file or made by encodeOcdCourseDem(), in the
 * given system (the block names none).
 *
 * Returns nullptr, with a message in error, for a block which is not one.
 */
std::unique_ptr<ElevationModel> decodeOcdCourseDem(const QByteArray& block, const QString& crs_spec = {}, QString* error = nullptr);


/**
 * What the parameter string of the DEM (type 1045) says about an embedded
 * DEM, and the string without it.
 */
struct OcdDemString
{
	QByteArray rest;        ///< The string without the fields P, S and e, and without a terminating zero.
	qint64 offset = -1;     ///< P: where the block begins in the file.
	qint64 size = -1;       ///< S: its size.
	bool embedded = false;  ///< e1

	/// True when the string names a place for a block.
	bool hasBlock() const { return embedded && offset > 0 && size > 0; }
	/// True when nothing but the flag "loaded" is left: a string which only carried the block.
	bool restIsEmpty() const;
};

/// Takes the string of type 1045 apart (the raw bytes; terminating zeros are ignored).
OcdDemString parseOcdDemString(const QByteArray& data);

/**
 * The string of type 1045 for a block of the given size: rest (or the fields of
 * a string which names no file) with P, S and e1 appended. The offset is
 * written with at least the given number of digits, zeros first, so that it
 * can be written again in place once it is known; offset_position is where its
 * digits begin.
 */
QByteArray formatOcdDemString(const QByteArray& rest, qint64 offset, int digits, qint64 size, int* offset_position = nullptr);


}  // namespace OpenOrienteering

#endif
