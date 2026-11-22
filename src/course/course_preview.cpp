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


#include "course_preview.h"

#include <QLatin1Char>

#include "core/map.h"
#include "course/course_canvas.h"

// the design notes, "The preview of a course map"

namespace OpenOrienteering {

CourseMapPreview::CourseMapPreview(Map& map, MapView* view)
: map(map)
, view(view)
{
	side.part = -1;
}


CourseMapPreview::~CourseMapPreview()
{
	release();
}


void CourseMapPreview::release()
{
	hider.reset();
}


const CourseCanvas* CourseMapPreview::canvas() const
{
	if (side.course.isEmpty() || !data.canvases.active)
		return nullptr;
	// All parts are seen with the canvas of the course, else with that of the first part.
	auto const* found = canvasFor(data.canvases, side.course, side.part);
	if (!found && side.part < 0)
		found = canvasFor(data.canvases, side.course, 0);
	return found;
}


bool CourseMapPreview::setSide(const CourseData& new_data, const QString& course, int part)
{
	auto const had = bool(hider);
	auto const old_numbers = numbers;
	data = new_data;
	side = {};
	side.course = data.findCourse(course) ? course : QString{};
	side.part = part;
	auto const* shown_canvas = canvas();
	numbers = shown_canvas ? shown_canvas->hidden_symbols : QStringList{};

	auto const wanted = !side.course.isEmpty();
	if (had == wanted && (!wanted || numbers == old_numbers))
		return false;
	release();
	if (wanted)
		hider = std::make_unique<CourseSymbolsHider>(map, numbers);
	return true;
}


CourseMapDrawing CourseMapPreview::draw() const
{
	if (side.course.isEmpty())
		return {};
	auto sheet_data = data;
	if (auto const* shown_canvas = canvas())
	{
		if (shown_canvas->box_size > 0.0)
			sheet_data.options.setValue(QLatin1Char('s'), QString::number(shown_canvas->box_size, 'f', 2));
		if (shown_canvas->max_rows > 0)
			sheet_data.options.setValue(QLatin1Char('i'), QString::number(shown_canvas->max_rows));
	}
	return drawCourseMap(map, sheet_data, side, numbers);
}


}  // namespace OpenOrienteering
