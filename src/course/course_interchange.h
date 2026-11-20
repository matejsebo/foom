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


#ifndef FOOM_COURSE_INTERCHANGE_H
#define FOOM_COURSE_INTERCHANGE_H

#include <QString>
#include <QStringList>

class QIODevice;

namespace OpenOrienteering {

class Map;


/**
 * The interchange of courses with other programs: the CourseData of the
 * IOF Data Standard 3.0 out and in, and GPX and KML out.
 *
 * See the design notes, "Interchange: IOF XML, GPX and KML".
 */

/** What a writer did. */
struct CourseExportReport
{
	bool ok = false;
	/** Why nothing was written. */
	QString error;
	/** The numbers of starts, controls and finishes, of courses and of classes which were written. */
	int controls = 0;
	int courses = 0;
	int classes = 0;
	/** The number of teams and runners of relays whose courses were assigned. */
	int teams = 0;
	/** The courses which were left out: those whose variations have a fault, and for IOF XML those with fewer than two controls. */
	QStringList skipped_courses;
};

/**
 * Writes the course objects, the courses without variations and their
 * classes as CourseData of the IOF Data Standard 3.0.
 */
CourseExportReport writeIofCourseData(const Map& map, QIODevice& device);

/**
 * Writes start, controls and finish as waypoints and the courses without
 * variations as tracks, in GPX 1.1. With the name of a course, that
 * course alone and its objects. The map must be georeferenced; when it
 * is refused, nothing is written.
 */
CourseExportReport writeCoursesGpx(const Map& map, QIODevice& device, const QString& course = {});

/** As writeCoursesGpx(), in KML 2.2: placemarks with points, and with line strings for the courses. */
CourseExportReport writeCoursesKml(const Map& map, QIODevice& device, const QString& course = {});


enum class CourseFileKind { IofXml, Gpx, Kml };

/** What writeCourseFiles() did. */
struct CourseFilesReport
{
	bool ok = false;
	QString error;
	/** The files which were written. */
	QStringList files;
	QStringList skipped_courses;
};

/**
 * Writes the courses into the file of the path, or, for GPX and KML, one
 * file for every course without variations, as <name>.<course>.<extension>
 * beside it. A file which cannot be written whole is not left behind.
 */
CourseFilesReport writeCourseFiles(const Map& map, const QString& path, CourseFileKind kind, bool per_course);


/** What is taken from a file of the IOF. */
struct IofImportOptions
{
	bool objects = true;
	bool courses = true;
	bool classes = true;
};

/** What importIofCourseData() did. */
struct IofImportReport
{
	bool ok = false;
	/** Why the file was refused. */
	QString error;
	/** The course objects which were made, and those which the map had. */
	int objects_added = 0;
	int objects_found = 0;
	int courses_added = 0;
	int classes_added = 0;
	/** The controls which were not made: without a usable place, without a symbol for their kind, or of a type which is no course object. */
	QStringList objects_not_made;
	/** The courses of the file whose names the map has; they were not touched. */
	QStringList courses_existing;
	/** The codes which courses of the file name and which are no course objects. */
	QStringList codes_missing;
	/** The classes which were not added: the map has them, or their course is not there. */
	QStringList classes_skipped;
	/** The courses of the file which are variants of a course of the map (their CourseFamily is its name); they were not imported. */
	int relay_variants = 0;
	/** The number of taped routes of the file; they have no objects and were left out. */
	int taped_routes = 0;
};

/**
 * Takes course objects, courses and classes from CourseData of the IOF
 * Data Standard 3.0, as one undo step. A file which is not such a file
 * is refused, and nothing is changed.
 */
IofImportReport importIofCourseData(Map& map, QIODevice& device, const IofImportOptions& options = {});


}  // namespace OpenOrienteering

#endif
