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


#ifndef FOOM_COURSE_CANVAS_H
#define FOOM_COURSE_CANVAS_H

#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

struct Extent;


/**
 * Canvases: for which courses which extent of the map is printed, at
 * which scale, and with which course symbols and background maps.
 *
 * The model is that of the OCD record; see the design notes,
 * "Canvases: the model, the files and the export".
 */

/** A course for which a canvas is used: the whole course, or one of its parts. */
struct CanvasCourse
{
	/** The name of the course; empty for all controls. */
	QString course;
	/** The part of a course with a map exchange or flip, counted from 0; -1 for the course as a whole. */
	int part = -1;
	/** The part is behind a map flip, not behind a map exchange. The file says which; nothing depends on it. */
	bool flip = false;
};

bool operator==(const CanvasCourse& lhs, const CanvasCourse& rhs);
inline bool operator!=(const CanvasCourse& lhs, const CanvasCourse& rhs) { return !(lhs == rhs); }

struct CourseCanvas
{
	QString name;
	/** The name of an extent of the map. */
	QString extent;
	/** The page status of the extent in the file; kept as it is read. */
	QString page_status = QStringLiteral("psSingle");
	/** The denominator of the export scale; 0 for the scale of the map. */
	unsigned int scale = 0;
	/** The size of a box of the control descriptions in mm; 0 for that of the options of the course project. */
	double box_size = 0.0;
	/** The rows after which the block of the descriptions wraps; 0 for those of the options. */
	int max_rows = 0;
	/** Of the used symbols and background maps those which the canvas hides. */
	QStringList hidden_symbols;
	QStringList hidden_templates;
	std::vector<CanvasCourse> courses;
};

bool operator==(const CourseCanvas& lhs, const CourseCanvas& rhs);
inline bool operator!=(const CourseCanvas& lhs, const CourseCanvas& rhs) { return !(lhs == rhs); }

struct CourseCanvases
{
	/** The canvases are used in the preview and in the export. */
	bool active = false;
	/** The canvases are given to classes and not to courses. */
	bool by_classes = false;
	/** The course symbols which the canvases govern, by their numbers as the format has them ("704.000"). */
	QStringList used_symbols;
	/** The background maps which the canvases govern, by their file names. */
	QStringList used_templates;
	std::vector<CourseCanvas> canvases;
	
	bool isEmpty() const { return canvases.empty() && used_symbols.isEmpty() && used_templates.isEmpty(); }
};

bool operator==(const CourseCanvases& lhs, const CourseCanvases& rhs);
inline bool operator!=(const CourseCanvases& lhs, const CourseCanvases& rhs) { return !(lhs == rhs); }


/** The canvas of a part of a course, or of the course as a whole when the part has none of its own; nullptr without one. */
const CourseCanvas* canvasFor(const CourseCanvases& canvases, const QString& course, int part);

/** The number of a symbol as the OCD canvases have it: "704.000", "722.001". */
QString canvasSymbolNumber(int first, int second);


/** Writes the element <canvases> of the map file. */
void saveCanvases(const CourseCanvases& canvases, QXmlStreamWriter& xml);

/** Reads the element <canvases> on which the reader stands. */
CourseCanvases loadCanvases(QXmlStreamReader& xml);


/**
 * Reads the text of the canvases: the parameter string 1093 of an OCD
 * file, which is UTF-8, or a file written by Save as XML, which is in
 * Windows-1252. Bytes which are no UTF-8 are read as Windows-1252.
 * Returns false for a text without the element CsCanvas.
 */
bool canvasesFromOcdXml(const QByteArray& bytes, CourseCanvases& canvases);

/** Writes the text of the canvases, as it stands in the parameter string 1093. */
QString canvasesToOcdXml(const CourseCanvases& canvases);

/** Reads a named extent, the parameter string 29 of an OCD file. */
bool extentFromOcdRecord(const QString& record, Extent& extent);

/** Writes a named extent as the parameter string 29. */
QString extentToOcdRecord(const Extent& extent);


}  // namespace OpenOrienteering

#endif
