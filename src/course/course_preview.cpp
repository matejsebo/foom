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

#include <algorithm>
#include <iterator>

#include <Qt>
#include <QColor>
#include <QFont>
#include <QLatin1Char>
#include <QPainter>
#include <QPen>
#include <QPointF>

#include "core/extents.h"
#include "core/map.h"
#include "core/map_view.h"
#include "course/course_canvas.h"
#include "templates/template.h"

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
	if (view)
	{
		for (auto const& entry : hidden_templates)
		{
			// A template which was deleted meanwhile is no longer asked for.
			if (map.findTemplateIndex(entry.first) >= 0)
				view->setTemplateVisibility(entry.first, entry.second);
		}
	}
	hidden_templates.clear();
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
	auto const old_templates = template_names;
	data = new_data;
	side = {};
	side.course = data.findCourse(course) ? course : QString{};
	side.part = part;
	auto const* shown_canvas = canvas();
	numbers = shown_canvas ? shown_canvas->hidden_symbols : QStringList{};
	template_names = shown_canvas ? shown_canvas->hidden_templates : QStringList{};

	auto const wanted = !side.course.isEmpty();
	if (had == wanted && (!wanted || (numbers == old_numbers && template_names == old_templates)))
		return false;
	release();
	if (wanted)
	{
		hider = std::make_unique<CourseSymbolsHider>(map, numbers);
		for (int i = 0; view && i < map.getNumTemplates(); ++i)
		{
			auto* temp = map.getTemplate(i);
			auto const visibility = view->getTemplateVisibility(temp);
			if (!visibility.visible || !template_names.contains(temp->getTemplateFilename()))
				continue;
			hidden_templates.emplace_back(temp, visibility);
			view->setTemplateVisibility(temp, { visibility.opacity, false });
		}
	}
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


// the design notes, "Canvases: the frame of the extent"
CanvasFrame courseCanvasFrame(const Map& map, const CourseData& data, const QString& course, int part)
{
	CanvasFrame frame;
	if (!data.canvases.active || course.isEmpty() || !data.findCourse(course))
		return frame;
	auto const* canvas = canvasFor(data.canvases, course, part);
	if (!canvas && part < 0)
		canvas = canvasFor(data.canvases, course, 0);
	if (!canvas || canvas->extent.isEmpty())
		return frame;
	auto const& extents = map.extents();
	auto const named = std::find_if(begin(extents), end(extents), [canvas](const Extent& extent) { return extent.name == canvas->extent; });
	if (named == end(extents))
		return frame;
	frame.shown = true;
	frame.name = named->name;
	frame.canvas = canvas->name;
	frame.rect = named->rect_mm;
	return frame;
}


void paintCanvasFrame(QPainter* painter, const CanvasFrame& frame, qreal scaling)
{
	if (!frame.shown || scaling <= 0.0)
		return;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, false);
	painter->setBrush(Qt::NoBrush);
	QPen pen(QColor(230, 0, 0), 2.0);
	pen.setCosmetic(true);
	painter->setPen(pen);
	painter->drawRect(frame.rect);
	// The name in pixels of the screen, inside the upper left corner.
	painter->translate(frame.rect.topLeft());
	painter->scale(1.0 / scaling, 1.0 / scaling);
	auto font = painter->font();
	font.setPixelSize(13);
	painter->setFont(font);
	painter->drawText(QPointF(5.0, 17.0), frame.name);
	painter->restore();
}


}  // namespace OpenOrienteering
