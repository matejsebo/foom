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

#include <QByteArray>
#include <QString>
#include <QStringList>

class QIODevice;

namespace OpenOrienteering {

class Map;
class MapView;


/**
 * The interchange of courses with other programs: the CourseData of the
 * IOF Data Standard 3.0 out and in, GPX and KML out and in, and KMZ with
 * a picture of the map.
 *
 * See the design notes, "Interchange: IOF XML, GPX and KML",
 * and "Interchange: GPX and KML in, and a KMZ with the map".
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


/** What the KMZ of the courses holds besides the KML of the courses. */
struct CourseKmzOptions
{
	/** The course whose map is the picture, and which alone is in the file; all courses and the map as it is when empty. */
	QString course;
	/** The resolution of the picture. */
	double dpi = 300.0;
	/** Tiles of 512 pixels; else one picture. */
	bool tiles = true;
	/**
	 * The templates are in the picture, as the view shows them or, without one, all which are loaded,
	 * and the picture has their extent too: as on the maps of the courses.
	 */
	bool templates = true;
	const MapView* view = nullptr;
};

/**
 * Writes a KMZ archive: a ground overlay of the map, or of the map of the
 * course, and the placemarks and lines of the KML. The map must be
 * georeferenced; a file which cannot be written whole is not left behind.
 */
CourseExportReport writeCoursesKmz(Map& map, const QString& path, const CourseKmzOptions& options = {});

/** The bytes of a file in a KMZ (zip) archive; with no name, of its first KML document. Empty when there is none. */
QByteArray readKmzEntry(const QString& path, const QString& entry = {});


enum class CourseFileKind { IofXml, Gpx, Kml, Kmz };

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
CourseFilesReport writeCourseFiles(Map& map, const QString& path, CourseFileKind kind, bool per_course, const CourseKmzOptions& kmz = {});


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


/** What is taken from a GPX or a KML file. */
struct CourseGeodataImportOptions
{
	bool objects = true;
	bool courses = true;
};

/** What importCoursesGpx() and importCoursesKml() did. */
struct CourseGeodataImportReport
{
	bool ok = false;
	/** Why the file was refused. */
	QString error;
	/** The course objects which were made of waypoints, and those which the map had. */
	int objects_added = 0;
	int objects_found = 0;
	int courses_added = 0;
	/** The tracks which are variants of a relay of the map; they were not imported. */
	int relay_variants = 0;
	/** The waypoints which were not made: their names are no codes, or the map has no symbol for their kind. */
	QStringList objects_not_made;
	/** The tracks whose names the map has as courses; they were not touched. */
	QStringList courses_existing;
	/** The tracks which do not start and end at course objects. */
	QStringList tracks_not_courses;
};

/**
 * Takes waypoints as course objects and tracks which start and end at
 * course objects as courses, from GPX 1.0 or 1.1, as one undo step. The
 * map must be georeferenced; a file which is refused changes nothing.
 */
CourseGeodataImportReport importCoursesGpx(Map& map, QIODevice& device, const CourseGeodataImportOptions& options = {});

/** As importCoursesGpx(), from the points and line strings of the placemarks of KML. */
CourseGeodataImportReport importCoursesKml(Map& map, QIODevice& device, const CourseGeodataImportOptions& options = {});

/** Reads a GPX, KML or KMZ file, by its extension. */
CourseGeodataImportReport importCourseFile(Map& map, const QString& path, const CourseGeodataImportOptions& options = {});


}  // namespace OpenOrienteering

#endif
