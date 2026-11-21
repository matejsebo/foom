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


#ifndef OPENORIENTEERING_PRJ_FILE_H
#define OPENORIENTEERING_PRJ_FILE_H

#include <QByteArray>
#include <QString>

namespace OpenOrienteering {


/** The coordinate reference system of a .prj file. */
struct PrjCrs
{
	/** The EPSG code when PROJ identifies the CRS with 90 % or more; else empty. */
	QString epsg;
	/** The CRS as a PROJ.4 string, without +type=crs. */
	QString proj;
	/** The name in the file. */
	QString name;
	/** Why the text was refused; empty on success. */
	QString error;
};


/**
 * Reads WKT (1 of GDAL or ESRI, or 2) of a projected CRS.
 *
 * See the design notes, "Projection file".
 */
PrjCrs crsFromWkt(const QByteArray& text);

/** Reads the file and its text with crsFromWkt(). */
PrjCrs readPrjFile(const QString& path);


}  // namespace OpenOrienteering

#endif
