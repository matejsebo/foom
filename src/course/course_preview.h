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


#ifndef FOOM_COURSE_PREVIEW_H
#define FOOM_COURSE_PREVIEW_H

#include <memory>

#include <QString>
#include <QStringList>

#include "course/course_data.h"
#include "course/course_map.h"

namespace OpenOrienteering {

class Map;
class MapView;
struct CourseCanvas;


/**
 * The preview of a course map in the editor: the map shows the chosen
 * course alone, as its map shows it.
 * While a course is set, the symbols whose objects a course map draws
 * itself are hidden, and with the canvases active what the canvas of the
 * course hides; draw() gives what is painted over the map in their place.
 * Everything is put back when the course is cleared and when the preview
 * ends. The map is not marked as changed.
 *
 * See the design notes, "The preview of a course map".
 */
class CourseMapPreview
{
public:
	explicit CourseMapPreview(Map& map, MapView* view = nullptr);
	~CourseMapPreview();
	CourseMapPreview(const CourseMapPreview&) = delete;
	CourseMapPreview& operator=(const CourseMapPreview&) = delete;

	/**
	 * Previews a course, or one of its parts; an empty name shows the map as
	 * it is. The data are those which are drawn, a relay by its variant.
	 * Returns true when what is hidden changed, so that the map is to be drawn anew.
	 */
	bool setSide(const CourseData& data, const QString& course, int part);

	const QString& course() const { return side.course; }
	int part() const { return side.part; }

	/** The canvas of the side when the canvases are active; nullptr without one. */
	const CourseCanvas* canvas() const;

	/** The numbers of the symbols which the canvas hides ("704.000"). */
	const QStringList& canvasSymbols() const { return numbers; }

	/** Draws the side as the map is now. */
	CourseMapDrawing draw() const;

private:
	void release();

	Map& map;
	MapView* view;
	CourseData data;
	CourseMapSide side;
	QStringList numbers;
	std::unique_ptr<CourseSymbolsHider> hider;
};


}  // namespace OpenOrienteering

#endif
