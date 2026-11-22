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


#ifndef FOOM_COURSE_MAP_H
#define FOOM_COURSE_MAP_H

#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "course/course_data.h"
#include "course/course_description.h"
#include "course/course_relay.h"

class QPainter;
class QPrinter;

namespace OpenOrienteering {

struct CourseRoutes;
class Map;
class MapView;
class Object;
class Symbol;


/**
 * The maps of the courses: what a sheet shows of a course, and the PDF
 * files of the sheets.
 *
 * See the design notes, "The maps of the courses as PDF files".
 */

/** What one side of a sheet shows of a course. */
struct CourseMapSide
{
	QString course;
	/** The part of the course, counted from 0; -1 for the whole course. */
	int part = -1;
	/** For a relay the variant; the leg 0 for a course which is none. */
	RelayVariant variant;
	/** The start number of the runner whose map it is, "101.1"; empty for a map of all runners of the variant. */
	QString start_number;
};

/** A sheet is one file; its sides are its pages. */
struct CourseMapSheet
{
	/** What names the sheet in the name of its file: "Map Exchange_1", "Relay Women.1AA". */
	QString name;
	std::vector<CourseMapSide> sides;
};

/** A relay has a map for every variant, or one for every runner. */
enum class RelayMaps { Variants, Runners };

/** The sheets of a course. None for a course which is not there and for a relay with a fault. */
std::vector<CourseMapSheet> courseMapSheets(const CourseData& data, const QString& course, RelayMaps relay_maps = RelayMaps::Variants);

/** The text of the course title on a side: the name and the classes, by the option of the course project. */
QString courseTitleText(const CourseData& data, const CourseMapSide& side);

/** The text of the variant code on a side; empty for a course which is no relay. */
QString courseVariantText(const CourseData& data, const CourseMapSide& side);


/** What a side draws in place of the course objects of the map. */
struct CourseMapDrawing
{
	CourseMapDrawing();
	CourseMapDrawing(CourseMapDrawing&&) noexcept;
	CourseMapDrawing& operator=(CourseMapDrawing&&) noexcept;
	~CourseMapDrawing();
	
	/** False for a side whose course or part is not there. */
	bool drawn = false;
	/** The codes of the items whose objects were copied, in their order. */
	QStringList items;
	/** The texts of the title, the variant code and the start number which were made, in this order. */
	QStringList texts;
	int legs = 0;
	int numbers = 0;
	bool has_description = false;
	/** The description of the side at the description objects of the map. */
	CourseDescriptionDrawing description;
	/** Copies of symbols of the map, which are never hidden; the objects refer to them. */
	std::vector<std::unique_ptr<Symbol>> symbols;
	/** For every copy in symbols the symbol of the map of which it is a copy. */
	std::vector<const Symbol*> symbol_sources;
	std::vector<std::unique_ptr<Object>> objects;
};

/**
 * Draws a side. Objects of the symbols with the given numbers, written as
 * the canvases write them ("704.000"), are left out: what a canvas hides.
 */
CourseMapDrawing drawCourseMap(const Map& map, const CourseData& data, const CourseMapSide& side, const QStringList& hidden_numbers = {});

/**
 * Paints a side with a painter which is set up for map coordinates.
 *
 * @param bounding_box  the part of the map which is painted, in mm
 * @param scaling       pixels per mm
 */
void paintCourseMap(QPainter* painter, Map* map, const CourseMapDrawing& drawing, const QRectF& bounding_box, qreal scaling);

/**
 * Paints the routes of a course with a painter which is set up for map
 * coordinates: the fastest route of every leg and the path of a marked
 * route as a red line, the routes over via points dashed in blue.
 *
 * See the design notes, "Route analysis: the routes on the
 * maps of the courses".
 */
void paintCourseRoutes(QPainter* painter, const CourseRoutes& routes);


/** The symbol has a course role whose objects a course map draws itself: start, control, finish, marked route, description, title, start number, variant code. */
bool isDrawnByCourseMap(const Symbol& symbol);


/**
 * Hides, as long as it lives, the symbols whose objects a course map
 * draws itself: start, control, finish, marked route, description, title,
 * start number and variant code. The map is not told and not marked as changed.
 */
class CourseSymbolsHider
{
public:
	/** Hides the symbols of course objects, and those with the given numbers, written as the canvases write them ("704.000"). */
	explicit CourseSymbolsHider(Map& map, const QStringList& numbers = {});
	~CourseSymbolsHider();
	CourseSymbolsHider(const CourseSymbolsHider&) = delete;
	CourseSymbolsHider& operator=(const CourseSymbolsHider&) = delete;
	
	/** The number of symbols which it hid. */
	int count() const { return int(hidden.size()); }
	
private:
	std::vector<Symbol*> hidden;
};


struct CourseMapExportOptions
{
	/** The folder into which the files are written. */
	QString folder;
	/** The first part of the names of the files; that of the map file. */
	QString base_name;
	/** The courses; all courses of the map when empty. */
	QStringList courses;
	RelayMaps relay_maps = RelayMaps::Variants;
	/** The name of an extent of the map; empty for the print area, or without one the drawn map. */
	QString extent;
	/** The scale of the print; 0 for that of the extent or of the map. */
	unsigned int scale = 0;
	/**
	 * The maps are those of the canvases of the course data: each sheet with the extent, the scale,
	 * the description box and the hidden symbols of its canvas, named by it; extent and scale above are not used.
	 */
	bool use_canvases = false;
	/** Every side has the routes which the map keeps for its course painted over the course. */
	bool with_routes = false;
	/** The view whose templates are printed as it shows them; without one every template which is loaded. */
	const MapView* view = nullptr;
	/**
	 * Map files in place of PDF files: the id of a file format which can be saved ("XML", "OCD2018").
	 * Every side is a file of its own; extent, scale, canvases and routes are not used.
	 * See the design notes, "Course maps as map files".
	 */
	QString map_format;
};

struct CourseMapExportReport
{
	bool ok = false;
	QString error;
	/** The files which were written, with their folders. */
	QStringList files;
	/** The courses which have no map: not there, or relays with a fault; with canvases also the sheets which no canvas has. */
	QStringList skipped;
};

/** A name of a sheet as a part of a file name. */
QString courseMapFileName(const QString& base_name, const QString& sheet);

/** The same with another extension than "pdf". */
QString courseMapFileName(const QString& base_name, const QString& sheet, const QString& extension);

/**
 * The name of the map file of a side:
 * the course, the course and the number of its part ("Map Exchange.1"),
 * or for a relay the name of its sheet ("Relay Women.1AA").
 */
QString courseMapFileSide(const CourseMapSheet& sheet, const CourseMapSide& side);

/**
 * Makes a map into the map file of a side: draws the side, removes the
 * objects of the symbols which a course map draws itself, puts in what the
 * side draws, and removes the course data. False for a side which is not there.
 */
bool makeCourseMapFile(Map& map, const CourseMapSide& side);

/** Writes the maps of the courses as PDF files, or as map files when the options have a map format. */
CourseMapExportReport exportCourseMaps(Map& map, const CourseMapExportOptions& options);

/**
 * Prints the maps of the courses on a printer: every sheet is a print job,
 * its sides the pages, on the paper of the printer. The folder, the base name
 * and the map format of the options are not used; the report names the sheets
 * which were printed in place of files.
 *
 * See the design notes, "Printing the course maps".
 */
CourseMapExportReport printCourseMaps(Map& map, const CourseMapExportOptions& options, QPrinter* printer);

/** Writes the maps of the courses as map files; exportCourseMaps() calls it for options with a map format. */
CourseMapExportReport exportCourseMapFiles(Map& map, const CourseMapExportOptions& options);


}  // namespace OpenOrienteering

#endif
