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


#ifndef FOOM_KMZ_ARCHIVE_H
#define FOOM_KMZ_ARCHIVE_H

#include <QByteArray>
#include <QString>

namespace OpenOrienteering {

/**
 * Reads a file of a zip archive, such as a KMZ, through GDAL's virtual
 * file system. With no name, the first KML document of the archive:
 * doc.kml when it has one, else the first which it lists. Empty when the
 * archive or the file is not there.
 *
 * See the design notes, "Interchange: GPX and KML in, and a KMZ with the map".
 */
QByteArray readZipEntry(const QString& archive_path, const QString& entry = {});

}  // namespace OpenOrienteering

#endif
