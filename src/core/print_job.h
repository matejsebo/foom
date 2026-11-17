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


#ifndef FOOM_PRINT_JOB_H
#define FOOM_PRINT_JOB_H

#ifdef QT_PRINTSUPPORT_LIB

#include <QJsonObject>
#include <QString>

namespace OpenOrienteering {

class MapPrinter;


/**
 * A job for the runner which repeats the printer's output.
 *
 * The job opens the map file and exports the print area with the printer's
 * scale, resolution, mode and options: to a PNG image for the image target,
 * to a PDF on the printer's paper for every other target. The output is
 * named after the job, name.pdf or name.png, and lies beside it.
 *
 * See the design notes, "Print settings as a job".
 */
QJsonObject printJob(const MapPrinter& printer, const QString& map_path, const QString& name);

/** Writes the job to the path; the output name is the path's base name. */
bool savePrintJob(const MapPrinter& printer, const QString& map_path, const QString& job_path, QString* error);


}  // namespace OpenOrienteering

#endif  // QT_PRINTSUPPORT_LIB

#endif
